/******************************************************************************
 * Enflame Video Process Platform SDK
 * Copyright (C) [2025] by Enflame, Inc. All rights reserved
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *******************************************************************************/
#include <dlfcn.h>
#include <errno.h>
#include <memory.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "libavcodec/version.h"
#include "libavformat/avformat.h"
#include "libavutil/avassert.h"
#include "libavutil/buffer.h"
#include "libavutil/fifo.h"
#include "libavutil/hwcontext.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"
#include "libavutil/thread.h"
#include "libavutil/time.h"
#include "libavutil/version.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/ff_topscodec_buffers.h"
#include "libavcodec/ff_topscodec_enc.h"
#include "libavcodec/ff_topscodec_utils.h"
#include "libavcodec/internal.h"
#include "libavutil/hwcontext_topscodec.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"

#define MAX_STREAM_NUM 16
#define TIMESTAMP_QUEUE_SIZE 100

const enum AVPixelFormat ff_topscodec_pix_fmts[] = {
    AV_PIX_FMT_YUV420P,     AV_PIX_FMT_YUV420P10LE, AV_PIX_FMT_NV12,
    AV_PIX_FMT_NV21,        AV_PIX_FMT_RGB24,       AV_PIX_FMT_RGB24P,
    AV_PIX_FMT_BGR24,       AV_PIX_FMT_BGR24P,      AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV444P10LE, AV_PIX_FMT_P010LE_LSB,  AV_PIX_FMT_P010LE,
    AV_PIX_FMT_GRAY8,       AV_PIX_FMT_TOPSCODEC,   AV_PIX_FMT_NONE};

static inline void topsenc_timestamp_queue_push(
    AVFifoBuffer* queue, int64_t timestamp) {
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
    av_fifo_write(queue, &timestamp, 1);
#else
    av_fifo_generic_write(queue, &timestamp, sizeof(timestamp), NULL);
#endif
    return;
}

static inline int64_t topsenc_timestamp_queue_pop(AVFifoBuffer* queue) {
    int64_t timestamp = AV_NOPTS_VALUE;
    if (av_fifo_size(queue) > 0) {
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
        av_fifo_read(queue, &timestamp, 1);
#else
        av_fifo_generic_read(queue, &timestamp, sizeof(timestamp), NULL);
#endif
    }
    return timestamp;
}

static inline int64_t topsenc_timestamp_queue_peek(AVFifoBuffer *queue,
                                                   size_t index) {
    int64_t timestamp = AV_NOPTS_VALUE;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
    av_fifo_peek(queue, &timestamp, 1, index);
#else
    int offset = index * sizeof(int64_t);
    av_fifo_generic_peek_at(queue, &timestamp, offset, sizeof(int64_t), NULL);
#endif
    return timestamp;
}

static void topsenc_set_packet_dts(EFCodecEncContext_t* ctx, AVPacket *pkt) {
    unsigned int delay;
    int64_t delay_time;
    AVCodecContext *avctx = ctx->avctx;

    // no B frame, DTS = PTS.
    if (!avctx->max_b_frames) {
        pkt->dts = pkt->pts;
        return;
    }

    delay = FFMAX(avctx->max_b_frames, 0);
    if (ctx->output_frame_num >= delay) {
        pkt->dts = topsenc_timestamp_queue_pop(ctx->frame_timestamp_queue);
        ctx->output_frame_num++;
        return;
    }

    delay_time = ctx->initial_delay_time;
    if (!delay_time) {
        int64_t t1, t2, t3;
        t1 = topsenc_timestamp_queue_peek(ctx->frame_timestamp_queue, delay);
        t2 = topsenc_timestamp_queue_peek(ctx->frame_timestamp_queue, 0);
        t3 = (delay > 1) ?
             topsenc_timestamp_queue_peek(ctx->frame_timestamp_queue, 1) : t1;

        if (t1 != AV_NOPTS_VALUE) {
            delay_time = t1 - t2;
        } else if (avctx->framerate.num > 0 && avctx->framerate.den > 0) {
            delay_time = av_rescale_q(delay,
                (AVRational) {avctx->framerate.den, avctx->framerate.num},
                avctx->time_base);
        } else if (t3 != AV_NOPTS_VALUE) {
            delay_time = delay * (t3 - t2);
        } else {
            delay_time = delay;
        }
        ctx->initial_delay_time = delay_time;
    }
    pkt->dts = topsenc_timestamp_queue_peek(ctx->frame_timestamp_queue, 0) -
               delay_time * (delay - ctx->output_frame_num) / delay;
    ctx->output_frame_num++;
}

#define DEFAULTS_ENTRIES \
    {"b", "2M"},      \
    {"qmin", "-1"},   \
    {"qmax", "-1"},   \
    {"qdiff", "-1"},  \
    {"qblur", "-1"},  \
    {"qcomp", "-1"},  \
    {"g", "-1"},      \
    {"bf", "-1"},     \
    {"refs", "0"},    \
    {NULL, NULL}

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
#include "libavutil/hdr_dynamic_metadata.h"
static const FFCodecDefault defaults[] = { DEFAULTS_ENTRIES };
const AVCodecHWConfigInternal* const ff_topscodec_hw_configs[] = {
    HW_CONFIG_ENCODER_FRAMES(TOPSCODEC, TOPSCODEC),
    HW_CONFIG_ENCODER_DEVICE(NONE, TOPSCODEC),
    NULL,
};

static void dump_content_light_metadata(AVCodecContext*  avctx,
                                        AVFrameSideData* sd) {
    const AVContentLightMetadata* metadata =
        (const AVContentLightMetadata*)sd->data;

    av_log(avctx, AV_LOG_INFO, "MaxCLL=%d, MaxFALL=%d", metadata->MaxCLL,
           metadata->MaxFALL);
}
static void dump_mastering_display(AVCodecContext*        avctx,
                                   const AVFrameSideData* sd) {
    const AVMasteringDisplayMetadata* mastering_display;

    if (sd->size < sizeof(*mastering_display)) {
        av_log(avctx, AV_LOG_ERROR, "invalid data\n");
        return;
    }

    mastering_display = (const AVMasteringDisplayMetadata*)sd->data;

    av_log(avctx, AV_LOG_INFO,
           "has_primaries:%d has_luminance:%d "
           "r(%5.4f,%5.4f) g(%5.4f,%5.4f) b(%5.4f %5.4f) wp(%5.4f, %5.4f) "
           "min_luminance=%f, max_luminance=%f",
           mastering_display->has_primaries, mastering_display->has_luminance,
           av_q2d(mastering_display->display_primaries[0][0]),
           av_q2d(mastering_display->display_primaries[0][1]),
           av_q2d(mastering_display->display_primaries[1][0]),
           av_q2d(mastering_display->display_primaries[1][1]),
           av_q2d(mastering_display->display_primaries[2][0]),
           av_q2d(mastering_display->display_primaries[2][1]),
           av_q2d(mastering_display->white_point[0]),
           av_q2d(mastering_display->white_point[1]),
           av_q2d(mastering_display->min_luminance),
           av_q2d(mastering_display->max_luminance));
}

static int handle_content_light_metadata(AVCodecContext*               avctx,
                                         EFCodecEncContext_t*          enc_ctx,
                                         const AVContentLightMetadata* cll) {
    enc_ctx->hdr_matrix_range = avctx->color_range;
    enc_ctx->hdr_matrix       = avctx->color_primaries;
    enc_ctx->hdr_transfer     = avctx->color_trc;
    enc_ctx->hdr_primaries    = avctx->colorspace;

    enc_ctx->hdr_content_lum_max = cll->MaxCLL;
    enc_ctx->hdr_content_lum_avg = cll->MaxFALL;
    enc_ctx->hdr_flags |=
        EFCODEC_BUFFER_PARAM_COLOUR_FLAG_CONTENT_LIGHT_DATA_VALID;
    av_log(avctx, AV_LOG_INFO, "Content Light Level: MaxCLL=%d, MaxFALL=%d\n",
           cll->MaxCLL, cll->MaxFALL);
    return 0;
}

static int handle_mdcv(AVCodecContext*                   avctx,
                       EFCodecEncContext_t*              enc_ctx,  //
                       const AVMasteringDisplayMetadata* mdcv) {
    enc_ctx->hdr_matrix_range = avctx->color_range;
    enc_ctx->hdr_matrix       = avctx->color_primaries;
    enc_ctx->hdr_transfer     = avctx->color_trc;
    enc_ctx->hdr_primaries    = avctx->colorspace;
    // G(gx,gy)B(bx,by)R(rx,ry)WP(wx,wy)L(max,min)
    // av_rescale_q = (a * b 分子 * c 分母) / (b 分母 * c 分子)
    if (mdcv->has_primaries) {
        enc_ctx->hdr_display_r_x =
            av_rescale_q(1, mdcv->display_primaries[0][0], (AVRational){1, 1});
        enc_ctx->hdr_display_r_y =
            av_rescale_q(1, mdcv->display_primaries[0][1], (AVRational){1, 1});
        enc_ctx->hdr_display_g_x =
            av_rescale_q(1, mdcv->display_primaries[1][0], (AVRational){1, 1});
        enc_ctx->hdr_display_g_y =
            av_rescale_q(1, mdcv->display_primaries[1][1], (AVRational){1, 1});
        enc_ctx->hdr_display_b_x =
            av_rescale_q(1, mdcv->display_primaries[2][0], (AVRational){1, 1});
        enc_ctx->hdr_display_b_y =
            av_rescale_q(1, mdcv->display_primaries[2][1], (AVRational){1, 1});
        enc_ctx->hdr_display_w_x =
            av_rescale_q(1, mdcv->white_point[0], (AVRational){1, 1});
        enc_ctx->hdr_display_w_y =
            av_rescale_q(1, mdcv->white_point[1], (AVRational){1, 1});
        enc_ctx->hdr_flags |=
            EFCODEC_BUFFER_PARAM_COLOUR_FLAG_MASTERING_DISPLAY_DATA_VALID;
    }

    if (mdcv->has_luminance) {
        enc_ctx->hdr_display_lum_max =
            av_rescale_q(1, mdcv->max_luminance, (AVRational){1, 1});
        enc_ctx->hdr_display_lum_min =
            av_rescale_q(1, mdcv->min_luminance, (AVRational){1, 1});
    }

    return 0;
}

static int handle_hdr10_frame_side_data(AVCodecContext*      avctx,
                                        EFCodecEncContext_t* enc_ctx) {
    int                               ret = 0;
    int                               nb_decoded_side_data;
    AVFrameSideData**                 sd;
    const AVContentLightMetadata*     cll;
    const AVMasteringDisplayMetadata* mdcv;

    sd                   = avctx->decoded_side_data;
    nb_decoded_side_data = avctx->nb_decoded_side_data;
    for (int i = 0; i < nb_decoded_side_data; i++) {
        if (sd[i]->type == AV_FRAME_DATA_CONTENT_LIGHT_LEVEL) {
            cll = (AVContentLightMetadata*)sd[i]->data;
            ret = handle_content_light_metadata(avctx, enc_ctx, cll);
        } else if (sd[i]->type == AV_FRAME_DATA_MASTERING_DISPLAY_METADATA) {
            mdcv = (AVMasteringDisplayMetadata*)sd[i]->data;
            ret  = handle_mdcv(avctx, enc_ctx, mdcv);
        } else {
            av_log(avctx, AV_LOG_WARNING, "Skipping unsupported side data type: %s\n", av_frame_side_data_name(sd[i]->type));
        }
    }
    if (nb_decoded_side_data > 0)
        av_log(avctx, AV_LOG_DEBUG, "hdr side data dealt successfully\n");
    return ret;
}
static void dump_av_dynamic_hdr10_plus(AVCodecContext*   avctx,
                                       AVDynamicHDRPlus* hdr_plus) {
    av_log(avctx, AV_LOG_INFO, "HDR10+ metadata: \n");
    av_log(avctx, AV_LOG_INFO, "application version: %d, \n",
           hdr_plus->application_version);
    av_log(avctx, AV_LOG_INFO, "num_windows: %d, ", hdr_plus->num_windows);

    for (int w = 0; w < hdr_plus->num_windows; w++) {
        AVHDRPlusColorTransformParams* params = &hdr_plus->params[w];
        av_log(avctx, AV_LOG_INFO, "window [%d] {maxscl: {", w);
        for (int i = 0; i < 3; i++) {
            av_log(avctx, AV_LOG_INFO, i ? ",%d/%d" : "%d/%d",
                   params->maxscl[i].num, params->maxscl[i].den);
        }
        av_log(avctx, AV_LOG_INFO, "}, average_maxrgb: %d/%d, ",
               params->average_maxrgb.num, params->average_maxrgb.den);
        av_log(avctx, AV_LOG_INFO, "distribution_maxrgb: {");
        for (int i = 0; i < params->num_distribution_maxrgb_percentiles; i++) {
            av_log(avctx, AV_LOG_INFO, "(%d,%d/%d)",
                   params->distribution_maxrgb[i].percentage,
                   params->distribution_maxrgb[i].percentile.num,
                   params->distribution_maxrgb[i].percentile.den);
        }
        av_log(avctx, AV_LOG_INFO, "}, fraction_bright_pixels: %d/%d",
               params->fraction_bright_pixels.num,
               params->fraction_bright_pixels.den);
        if (params->tone_mapping_flag) {
            av_log(avctx, AV_LOG_INFO, ", knee_point: (%d/%d,%d/%d), ",
                   params->knee_point_x.num, params->knee_point_x.den,
                   params->knee_point_y.num, params->knee_point_y.den);
            av_log(avctx, AV_LOG_INFO, "bezier_curve_anchors: {");
            for (int i = 0; i < params->num_bezier_curve_anchors; i++) {
                av_log(avctx, AV_LOG_INFO, i ? ",%d/%d" : "%d/%d",
                       params->bezier_curve_anchors[i].num,
                       params->bezier_curve_anchors[i].den);
            }
            av_log(avctx, AV_LOG_INFO, "}");
        }
        if (params->color_saturation_mapping_flag) {
            av_log(avctx, AV_LOG_INFO, ", color_saturation_weight: %d/%d",
                   params->color_saturation_weight.num,
                   params->color_saturation_weight.den);
        }
        av_log(avctx, AV_LOG_INFO, "}");
    }

    for (int w = 1; w < hdr_plus->num_windows; w++) {
        AVHDRPlusColorTransformParams* params = &hdr_plus->params[w];
        av_log(avctx, AV_LOG_INFO, w > 1 ? ", window %d { " : "window %d { ",
               w);
        av_log(avctx, AV_LOG_INFO, "window_upper_left_corner: (%d/%d,%d/%d),",
               params->window_upper_left_corner_x.num,
               params->window_upper_left_corner_x.den,
               params->window_upper_left_corner_y.num,
               params->window_upper_left_corner_y.den);
        av_log(avctx, AV_LOG_INFO, "window_lower_right_corner: (%d/%d,%d/%d), ",
               params->window_lower_right_corner_x.num,
               params->window_lower_right_corner_x.den,
               params->window_lower_right_corner_y.num,
               params->window_lower_right_corner_y.den);
        av_log(avctx, AV_LOG_INFO, "center_of_ellipse_x: (%d,%d), ",
               params->center_of_ellipse_x, params->center_of_ellipse_y);
        av_log(avctx, AV_LOG_INFO, "rotation_angle: %d, ",
               params->rotation_angle);
        av_log(avctx, AV_LOG_INFO, "semimajor_axis_internal_ellipse: %d, ",
               params->semimajor_axis_internal_ellipse);
        av_log(avctx, AV_LOG_INFO, "semimajor_axis_external_ellipse: %d, ",
               params->semimajor_axis_external_ellipse);
        av_log(avctx, AV_LOG_INFO, "semiminor_axis_external_ellipse: %d, ",
               params->semiminor_axis_external_ellipse);
        av_log(avctx, AV_LOG_INFO, "overlap_process_option: %d}",
               params->overlap_process_option);
    }

    av_log(avctx, AV_LOG_INFO,
           "targeted_system_display_maximum_luminance: %d/%d, ",
           hdr_plus->targeted_system_display_maximum_luminance.num,
           hdr_plus->targeted_system_display_maximum_luminance.den);
    if (hdr_plus->targeted_system_display_actual_peak_luminance_flag) {
        av_log(avctx, AV_LOG_INFO,
               "targeted_system_display_actual_peak_luminance: {");
        for (int i = 0;
             i <
             hdr_plus->num_rows_targeted_system_display_actual_peak_luminance;
             i++) {
            av_log(avctx, AV_LOG_INFO, "(");
            for (int j = 0;
                 j <
                 hdr_plus
                     ->num_cols_targeted_system_display_actual_peak_luminance;
                 j++) {
                av_log(
                    avctx, AV_LOG_INFO, i ? ",%d" : "%d",
                    (hdr_plus
                         ->targeted_system_display_actual_peak_luminance[i][j])
                        .num);
            }
            av_log(avctx, AV_LOG_INFO, ")");
        }
        av_log(avctx, AV_LOG_INFO, "}, ");
    }

    if (hdr_plus->mastering_display_actual_peak_luminance_flag) {
        av_log(avctx, AV_LOG_INFO,
               ", mastering_display_actual_peak_luminance: {");
        for (int i = 0;
             i < hdr_plus->num_rows_mastering_display_actual_peak_luminance;
             i++) {
            av_log(avctx, AV_LOG_INFO, "(");
            for (int j = 0;
                 j < hdr_plus->num_cols_mastering_display_actual_peak_luminance;
                 j++) {
                av_log(
                    avctx, AV_LOG_INFO, i ? ",%d/%d" : "%d/%d",
                    hdr_plus->mastering_display_actual_peak_luminance[i][j].num,
                    hdr_plus->mastering_display_actual_peak_luminance[i][j]
                        .den);
            }
            av_log(avctx, AV_LOG_INFO, ")");
        }
        av_log(avctx, AV_LOG_INFO, "}");
    }
    av_log(avctx, AV_LOG_INFO, "\n");
}

static int parse_payload_size(const uint8_t* dataStream,
                              int*           position_on_stream) {
    int     payload_size = 0;
    uint8_t byte;

    do {
        byte = dataStream[*position_on_stream];
        *position_on_stream += 1;
        payload_size += byte;
    } while (byte == 0xFF);

    return payload_size;
}

static int get_frame_t35_data(AVCodecContext* avctx, AVFrame* frame,
                              uint8_t** t35_data, int* t35_size) {
    int              ret = 0;
    AVDynamicHDRPlus hdr_plus;
    AVFrameSideData* sd;
    uint8_t*         t35_data_ptr;
    int              t35_data_size;

    sd = av_frame_get_side_data(frame, AV_FRAME_DATA_SEI_UNREGISTERED);
    if (!sd) {
        // av_log(avctx, AV_LOG_ERROR, "no dynamic hdr plus side data\n");
        *t35_data = NULL;
        *t35_size = 0;
        return 0;
    }

    int position_on_stream = 0;
    int payload_size       = parse_payload_size(sd->data, &position_on_stream);
    av_log(avctx, AV_LOG_INFO, "T.35 seipayloadSize: %d\n", payload_size);
    *t35_size = payload_size;
    *t35_data = sd->data + position_on_stream;

    uint8_t country_code           = (*t35_data)[0];
    int16_t provider_code          = (*t35_data)[1] << 8 | (*t35_data)[2];
    int16_t provider_oriented_code = (*t35_data)[3] << 8 | (*t35_data)[4];
    int16_t application_identifier = (*t35_data)[5];

    av_log(avctx, AV_LOG_INFO,
           "get_frame_t35_data success, t35_size: %d, "
           "HDR10+ header info: country_code: 0x%02x, provider_code: 0x%04x, "
           "provider_oriented_code: 0x%04x, application_identifier: 0x%02x\n",
           *t35_size, country_code, provider_code, provider_oriented_code,
           application_identifier);
    t35_data_ptr  = *t35_data + 6;
    t35_data_size = payload_size - 6;
    memset(&hdr_plus, 0, sizeof(AVDynamicHDRPlus));
    // skip the first 6 bytes of the t35 data，begin with the application mode
    ret = av_dynamic_hdr_plus_from_t35(&hdr_plus, t35_data_ptr, t35_data_size);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "av_dynamic_hdr_plus_from_t35 failed, ret(%d)\n", ret);
        return AVERROR(EINVAL);
    }
    dump_av_dynamic_hdr10_plus(avctx, &hdr_plus);
    return ret;
}
#elif LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 18, 100)  // n4.x
static const AVCodecDefault defaults[] = { DEFAULTS_ENTRIES };
const AVCodecHWConfigInternal* const ff_topscodec_hw_configs[] = {
    HW_CONFIG_ENCODER_FRAMES(TOPSCODEC, TOPSCODEC),
    HW_CONFIG_ENCODER_DEVICE(NONE, TOPSCODEC),
    NULL,
};
#else
static const AVCodecDefault defaults[] = { DEFAULTS_ENTRIES };
#endif

static topscodecType_t get_codec_type(AVCodecContext* avctx) {
    topscodecType_t ret = TOPSCODEC_NUM_CODECS;
    av_log(avctx, AV_LOG_DEBUG, "get_codec_type: %s\n", avctx->codec->name);
    switch (avctx->codec->id) {
#if CONFIG_H264_TOPSCODEC_ENC_ENCODER
        case AV_CODEC_ID_H264:
            ret = TOPSCODEC_H264;
            break;
#endif
#if CONFIG_HEVC_TOPSCODEC_ENC_ENCODER
        case AV_CODEC_ID_HEVC:
            ret = TOPSCODEC_HEVC;
            break;
#endif
#if CONFIG_MJPEG_TOPSCODEC_ENC_ENCODER
        case AV_CODEC_ID_MJPEG:
            ret = TOPSCODEC_JPEG;
            break;
#endif
#if CONFIG_VP8_TOPSCODEC_ENC_ENCODER
        case AV_CODEC_ID_VP8:
            ret = TOPSCODEC_VP8;
            break;
#endif
#if CONFIG_VP9_TOPSCODEC_ENC_ENCODER
        case AV_CODEC_ID_VP9:
            ret = TOPSCODEC_VP9;
            break;
#endif
        default:
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
            // av_log(avctx, AV_LOG_ERROR, "Invalid tops codec %s\n",
            // avcodec_descriptor_get(avctx->codec->id)->name);
#else
            av_log(avctx, AV_LOG_ERROR, "Invalid tops codec %s\n",
                   avcodec_descriptor_get(avctx->codec->id)->long_name);
#endif
            return ret;
    }
    return ret;
}

static int topsenc_alloc_hdr10plus_t35_buff(EFCodecEncContext_t* ctx) {
    int                    ret   = 0;
    AVCodecContext*        avctx = ctx->avctx;
    topsPointerAttribute_t att;

    ctx->hdr10plus_t35_buff_alloc_size = 1024;
    ret = ctx->topsruntime_lib_ctx->lib_topsExtMallocWithFlags((void**)&ctx->hdr10plus_t35_buff_host_addr, ctx->hdr10plus_t35_buff_alloc_size, topsMallocHostAccessable);
    if (ret != topsSuccess) {
        av_log(avctx, AV_LOG_ERROR, "Error, hdr10plus_t35_buff topsExMallocWithFlags failed, ret(%d)\n", ret);
        ret = AVERROR(EINVAL);
        return ret;
    }
    memset(&att, 0, sizeof(topsPointerAttribute_t));
    ret = ctx->topsruntime_lib_ctx->lib_topsPointerGetAttributes(&att, (void*)(ctx->hdr10plus_t35_buff_host_addr));
    if (ret != topsSuccess) {
        av_log(avctx, AV_LOG_ERROR, "Error, topsPointerGetAttributes failed, ret(%d)\n", ret);
        ret = AVERROR(EINVAL);
        return ret;
    }
    ctx->hdr10plus_t35_buff_L3_addr = (char*)(att.device_pointer);

    ret = ctx->topsruntime_lib_ctx->lib_topsExtMallocWithFlags((void**)&ctx->sei_attr, sizeof(topscodecEncSeiAttr_t), topsMallocHostAccessable);
    if (ret != topsSuccess) {
        av_log(avctx, AV_LOG_ERROR, "Error, sei_attr topsExMallocWithFlags failed, ret(%d)\n", ret);
        ret = AVERROR(EINVAL);
        return ret;
    }
    memset(&att, 0, sizeof(topsPointerAttribute_t));
    ret = ctx->topsruntime_lib_ctx->lib_topsPointerGetAttributes(&att, (void*)(ctx->sei_attr));
    if (ret != topsSuccess) {
        av_log(avctx, AV_LOG_ERROR, "Error, sei_attr topsPointerGetAttributes failed, ret(%d)\n", ret);
        ret = AVERROR(EINVAL);
        return ret;
    }
    ctx->sei_payload_L3_addr = (char*)(att.device_pointer);
    av_log(avctx, AV_LOG_DEBUG, "sei_payload_L3_addr: 0x%lx\n", (uint64_t)ctx->sei_payload_L3_addr);

    // set the sei_payload_addr and sei_payload_size and sei_payload_type
    ctx->sei_attr->sei_payload_addr = (uint64_t)(ctx->hdr10plus_t35_buff_L3_addr);
    ctx->sei_attr->sei_payload_size = ctx->hdr10plus_t35_buff_L3_data_size;
    ctx->sei_attr->sei_payload_type = 4;
    return ret;
}

static void topsenc_free_hdr10plus_t35_buff(EFCodecEncContext_t* ctx) {
    if (!ctx->topsruntime_lib_ctx) return;

    if (ctx->hdr10plus_t35_buff_host_addr) {
        ctx->topsruntime_lib_ctx->lib_topsFree(ctx->hdr10plus_t35_buff_host_addr);
        ctx->hdr10plus_t35_buff_host_addr = NULL;
    }
    if (ctx->sei_attr) {
        ctx->topsruntime_lib_ctx->lib_topsFree(ctx->sei_attr);
        ctx->sei_attr = NULL;
    }
}

static void print_enc_caps(AVCodecContext* avctx, topscodecEncCaps_t* EncCaps) {
    av_log(avctx, AV_LOG_DEBUG,
           "\t topscodecEncGetCaps success(size %ld) {.  \n",
           sizeof(topscodecEncCaps_t));
    av_log(avctx, AV_LOG_DEBUG, "\t Caps supported(%d)            \n",
           EncCaps->supported);
    av_log(avctx, AV_LOG_DEBUG, "\t max_width(%d)                 \n",
           EncCaps->max_width);
    av_log(avctx, AV_LOG_DEBUG, "\t max_height(%d)                \n",
           EncCaps->max_height);
    av_log(avctx, AV_LOG_DEBUG, "\t min_width(%d)                 \n",
           EncCaps->min_width);
    av_log(avctx, AV_LOG_DEBUG, "\t min_height(%d)                \n",
           EncCaps->min_height);
    av_log(avctx, AV_LOG_DEBUG, "\t input_pixel_format_mask(0x%x) \n",
           EncCaps->input_pixel_format_mask);
    av_log(avctx, AV_LOG_DEBUG, "\t scale_up_supported(%d)        \n",
           EncCaps->color_space_mask);
    av_log(avctx, AV_LOG_DEBUG, "\t crop_supported(%d)            \n",
           EncCaps->crop_supported);
    av_log(avctx, AV_LOG_DEBUG, "\t rate_control_mask(0x%x)       \n",
           EncCaps->rate_control_mask);
    av_log(avctx, AV_LOG_DEBUG, "\t roi_supported(%d)             \n",
           EncCaps->roi_supported);
    av_log(avctx, AV_LOG_DEBUG, "\t                           }   \n");
}
static const char* get_run_mode_str(topscodecRunMode_t run_mode) {
    switch (run_mode) {
        case TOPSCODEC_RUN_MODE_SYNC:
            return "TOPSCODEC_RUN_MODE_SYNC";
        case TOPSCODEC_RUN_MODE_ASYNC:
            return "TOPSCODEC_RUN_MODE_ASYNC";
        default:
            return "UNKNOWN";
    }
}
static void print_create_info(AVCodecContext*           avctx,
                              topscodecEncCreateInfo_t* create_info) {
    const char* name =
        topscode_descriptor_get_by_type(create_info->output_codec_type)->name;
    av_log(avctx, AV_LOG_DEBUG, "\t topscodecEncCreateInfo_t info {.\n");
    av_log(avctx, AV_LOG_DEBUG, "\t session id(0x%lx)                \n",
           create_info->user_context);
    av_log(avctx, AV_LOG_DEBUG, "\t dev_id(%d)                      \n",
           create_info->vcu_id);
    av_log(avctx, AV_LOG_DEBUG, "\t card_id(%d)                     \n",
           create_info->card_id);
    av_log(avctx, AV_LOG_DEBUG, "\t hw_ctx_id(%d)                   \n",
           create_info->hw_ctx_id);
    av_log(avctx, AV_LOG_DEBUG, "\t sw_ctx_id(%d)                   \n",
           create_info->sw_ctx_id);
    av_log(avctx, AV_LOG_DEBUG, "\t codec(%s)                       \n", name);
    av_log(avctx, AV_LOG_DEBUG, "\t run_mode(%s)                    \n",
           get_run_mode_str(create_info->run_mode));
    av_log(avctx, AV_LOG_DEBUG, "\t callback(%p)                    \n",
           create_info->callback);
    av_log(avctx, AV_LOG_DEBUG, "\t                           }     \n");
}

// Helper function to print ROI attributes
static void print_roi_attr_ffmpeg(AVCodecContext*              avctx,
                                  const topscodecEncRoiAttr_t* roi, int index) {
    av_log(avctx, AV_LOG_DEBUG, "\t\t ROI[%d]:\n", index);
    av_log(avctx, AV_LOG_DEBUG, "\t\t   roi_enable: %u\n", roi->roi_enable);
    av_log(avctx, AV_LOG_DEBUG, "\t\t   index: %u\n", roi->index);
    av_log(avctx, AV_LOG_DEBUG, "\t\t   abs_mode: %u\n", roi->abs_mode);
    av_log(avctx, AV_LOG_DEBUG, "\t\t   roi_qp: %d\n", roi->roi_qp);
    av_log(avctx, AV_LOG_DEBUG,
           "\t\t   roi_rect: x=%d, y=%d, width=%u, height=%u\n",
           roi->roi_rect.x, roi->roi_rect.y, roi->roi_rect.width,
           roi->roi_rect.height);
}

// Helper function to print intra area attributes
static void print_intra_area_attr_ffmpeg(
    AVCodecContext* avctx, const topscodecEncIntraAreaAttr_t* intra) {
    av_log(avctx, AV_LOG_DEBUG, "\t\t intra_area_attr:\n");
    av_log(avctx, AV_LOG_DEBUG, "\t\t   intra_area_enable: %u\n",
           intra->intra_area_enable);
    av_log(avctx, AV_LOG_DEBUG,
           "\t\t   intra_area_rect: x=%u, y=%u, width=%u, height=%u\n",
           intra->intra_area_rect.x, intra->intra_area_rect.y,
           intra->intra_area_rect.width, intra->intra_area_rect.height);
}

// Helper function to print IPCM area attributes
static void print_ipcm_area_attr_ffmpeg(AVCodecContext*                   avctx,
                                        const topscodecEncIpcmAreaAttr_t* ipcm,
                                        int index) {
    av_log(avctx, AV_LOG_DEBUG, "\t\t IPCM[%d]:\n", index);
    av_log(avctx, AV_LOG_DEBUG, "\t\t   ipcm_area_enable: %u\n",
           ipcm->ipcm_area_enable);
    av_log(avctx, AV_LOG_DEBUG, "\t\t   index: %u\n", ipcm->index);
    av_log(avctx, AV_LOG_DEBUG,
           "\t\t   ipcm_area_rect: x=%d, y=%d, width=%u, height=%u\n",
           ipcm->ipcm_area_rect.x, ipcm->ipcm_area_rect.y,
           ipcm->ipcm_area_rect.width, ipcm->ipcm_area_rect.height);
}

// Helper function to print SEI attributes
static void print_sei_attr_ffmpeg(AVCodecContext*              avctx,
                                  const topscodecEncSeiAttr_t* sei, int index) {
    av_log(avctx, AV_LOG_DEBUG, "\t\t\t SEI[%d]:\n", index);
    av_log(avctx, AV_LOG_DEBUG, "\t\t\t   payload_size: %u\n",
           sei->sei_payload_size);
    av_log(avctx, AV_LOG_DEBUG, "\t\t\t   payload_type: %u\n",
           sei->sei_payload_type);
    av_log(avctx, AV_LOG_DEBUG, "\t\t\t   payload_addr: 0x%lx\n",
           (uint64_t)sei->sei_payload_addr);
}

// Helper function to print H.264 picture attributes
static void print_h264_pic_attr_ffmpeg(AVCodecContext*                  avctx,
                                       const topscodecEncPicAttrH264_t* h264) {
    av_log(avctx, AV_LOG_DEBUG, "\t\t H264 Picture Attributes:\n");
    av_log(avctx, AV_LOG_DEBUG, "\t\t   force_cyclic_intra_refresh: %u\n",
           h264->force_cyclic_intra_refresh);
    av_log(avctx, AV_LOG_DEBUG, "\t\t   cyclic_intra_refresh_start: %u\n",
           h264->cyclic_intra_refresh_start);
    av_log(avctx, AV_LOG_DEBUG, "\t\t   cyclic_intra_refresh_interval: %u\n",
           h264->cyclic_intra_refresh_interval);
    av_log(avctx, AV_LOG_DEBUG, "\t\t   ctu_row_per_slice: %u\n",
           h264->ctu_row_per_slice);
    av_log(avctx, AV_LOG_DEBUG, "\t\t   sei_payload_array_cnt: %u\n",
           h264->sei_payload_array_cnt);

    if (h264->sei_payload && h264->sei_payload_array_cnt > 0) {
        av_log(avctx, AV_LOG_DEBUG, "\t\t   SEI Payloads:\n");
        for (u32_t i = 0; i < h264->sei_payload_array_cnt && i < 10; i++) {
            print_sei_attr_ffmpeg(avctx, &h264->sei_payload[i], i);
        }
    }
}

// Helper function to print HEVC picture attributes
static void print_hevc_pic_attr_ffmpeg(AVCodecContext*                  avctx,
                                       const topscodecEncPicAttrHevc_t* hevc) {
    av_log(avctx, AV_LOG_DEBUG, "\t\t HEVC Picture Attributes:\n");
    av_log(avctx, AV_LOG_DEBUG, "\t\t   force_cyclic_intra_refresh: %u\n",
           hevc->force_cyclic_intra_refresh);
    av_log(avctx, AV_LOG_DEBUG, "\t\t   cyclic_intra_refresh_start: %u\n",
           hevc->cyclic_intra_refresh_start);
    av_log(avctx, AV_LOG_DEBUG, "\t\t   cyclic_intra_refresh_interval: %u\n",
           hevc->cyclic_intra_refresh_interval);
    av_log(avctx, AV_LOG_DEBUG, "\t\t   ctu_row_per_slice: %u\n",
           hevc->ctu_row_per_slice);
    av_log(avctx, AV_LOG_DEBUG, "\t\t   sei_payload_array_cnt: %u\n",
           hevc->sei_payload_array_cnt);

    if (hevc->sei_payload && hevc->sei_payload_array_cnt > 0) {
        av_log(avctx, AV_LOG_DEBUG, "\t\t   SEI Payloads:\n");
        for (u32_t i = 0; i < hevc->sei_payload_array_cnt && i < 10; i++) {
            print_sei_attr_ffmpeg(avctx, &hevc->sei_payload[i], i);
        }
    }
}

// Helper function to print codec picture attributes
static void print_codec_pic_attr_ffmpeg(
    AVCodecContext* avctx, const topscodecEncCodecPicAttr_t* codec_attr,
    topscodecType_t codec_type) {
    av_log(avctx, AV_LOG_DEBUG, "\t\t codec_pic_attr:\n");
    switch (codec_type) {
        case TOPSCODEC_H264:
            print_h264_pic_attr_ffmpeg(avctx, &codec_attr->h264_pic_attr);
            break;
        case TOPSCODEC_HEVC:
            print_hevc_pic_attr_ffmpeg(avctx, &codec_attr->hevc_pic_attr);
            break;
        default:
            av_log(avctx, AV_LOG_DEBUG, "\t\t   Unknown codec type: %d\n",
                   codec_type);
            break;
    }
}

// Helper function to print video picture attributes
static void print_vid_pic_attr_ffmpeg(AVCodecContext*                 avctx,
                                      const topscodecEncPicAttrVid_t* vid,
                                      topscodecType_t codec_type) {
    int roi_count  = 0;
    int ipcm_count = 0;

    av_log(avctx, AV_LOG_DEBUG, "\t Video Picture Attributes:\n");
    av_log(avctx, AV_LOG_DEBUG, "\t\t input_stride: %u\n", vid->input_stride);

    // Print ROI attributes
    av_log(avctx, AV_LOG_DEBUG, "\t\t ROI attributes:\n");
    for (int i = 0; i < 8; i++) {
        if (vid->roi_attr[i].roi_enable) {
            print_roi_attr_ffmpeg(avctx, &vid->roi_attr[i], i);
            roi_count++;
        }
    }
    if (roi_count == 0) {
        av_log(avctx, AV_LOG_DEBUG, "\t\t   No ROI enabled\n");
    }

    // Print intra area attributes
    print_intra_area_attr_ffmpeg(avctx, &vid->intra_area_attr);

    // Print IPCM area attributes
    av_log(avctx, AV_LOG_DEBUG, "\t\t IPCM area attributes:\n");
    for (int i = 0; i < 2; i++) {
        if (vid->ipcm_area_attr[i].ipcm_area_enable) {
            print_ipcm_area_attr_ffmpeg(avctx, &vid->ipcm_area_attr[i], i);
            ipcm_count++;
        }
    }
    if (ipcm_count == 0) {
        av_log(avctx, AV_LOG_DEBUG, "\t\t   No IPCM area enabled\n");
    }

    av_log(avctx, AV_LOG_DEBUG, "\t\t force_idr: %u\n", vid->force_idr);
    av_log(avctx, AV_LOG_DEBUG, "\t\t qp_map_addr: 0x%lx\n",
           (uint64_t)vid->qp_map_addr);
    av_log(avctx, AV_LOG_DEBUG, "\t\t qp_map_size: %u\n", vid->qp_map_size);

    print_codec_pic_attr_ffmpeg(avctx, &vid->codec_pic_attr, codec_type);
}

// Helper function to print JPEG picture attributes
static void print_jpg_pic_attr_ffmpeg(AVCodecContext*                  avctx,
                                      const topscodecEncPicAttrJpeg_t* jpg) {
    av_log(avctx, AV_LOG_DEBUG, "\t JPEG Picture Attributes:\n");
    av_log(avctx, AV_LOG_DEBUG, "\t\t jpeg_param:\n");
    av_log(avctx, AV_LOG_DEBUG, "\t\t   quality: %u\n",
           jpg->jpeg_param.quality);
    av_log(avctx, AV_LOG_DEBUG, "\t\t   restart_interval: %u\n",
           jpg->jpeg_param.restart_interval);

    av_log(avctx, AV_LOG_DEBUG, "\t\t usr_quant_table_en: %u\n",
           jpg->usr_quant_table_en);
    if (jpg->usr_quant_table_en) {
        av_log(avctx, AV_LOG_DEBUG,
               "\t\t   User quantization tables enabled\n");
        // Note: Full quantization table printing would be very verbose for
        // logs
        av_log(avctx, AV_LOG_DEBUG,
               "\t\t   Luma quant[0-7]: %d %d %d %d %d %d %d %d\n",
               jpg->usr_quant_table.luma_quant[0],
               jpg->usr_quant_table.luma_quant[1],
               jpg->usr_quant_table.luma_quant[2],
               jpg->usr_quant_table.luma_quant[3],
               jpg->usr_quant_table.luma_quant[4],
               jpg->usr_quant_table.luma_quant[5],
               jpg->usr_quant_table.luma_quant[6],
               jpg->usr_quant_table.luma_quant[7]);
    }

    av_log(avctx, AV_LOG_DEBUG, "\t\t usr_huff_table_en: %u\n",
           jpg->usr_huff_table_en);
    if (jpg->usr_huff_table_en) {
        av_log(avctx, AV_LOG_DEBUG, "\t\t   User Huffman tables enabled\n");
    }

    av_log(avctx, AV_LOG_DEBUG, "\t\t crop:\n");
    av_log(avctx, AV_LOG_DEBUG, "\t\t   crop_enable: %u\n", jpg->crop.enable);
    if (jpg->crop.enable) {
        av_log(avctx, AV_LOG_DEBUG,
               "\t\t   crop_rect: x=%u, y=%u, width=%u, height=%u\n",
               jpg->crop.tl_x, jpg->crop.tl_y, jpg->crop.br_x, jpg->crop.br_y);
    }
}

static void print_tops_codecenc_pic_attr(AVCodecContext*        avctx,
                                         topscodecEncPicAttr_t* pic_attr) {
    topscodecType_t codec_type = get_codec_type(avctx);
    if (!pic_attr) {
        av_log(avctx, AV_LOG_DEBUG, "tops_codecenc_pic_attr: NULL pointer\n");
        return;
    }

    av_log(avctx, AV_LOG_DEBUG, "=== tops_codecenc_pic_attr ===\n");

    switch (codec_type) {
        case TOPSCODEC_JPEG:
            print_jpg_pic_attr_ffmpeg(avctx, &pic_attr->jpg_pic_attr);
            break;
        case TOPSCODEC_H264:
        case TOPSCODEC_HEVC:
            print_vid_pic_attr_ffmpeg(avctx, &pic_attr->vid_pic_attr,
                                      codec_type);
            break;
        default:
            av_log(avctx, AV_LOG_WARNING,
                   "Unknown codec type: %d, printing as video attributes\n",
                   codec_type);
            print_vid_pic_attr_ffmpeg(avctx, &pic_attr->vid_pic_attr,
                                      TOPSCODEC_H264);
            break;
    }
    av_log(avctx, AV_LOG_DEBUG, "==============================\n");
}

static void debug_log_set(AVCodecContext* avctx) {
    int         debug_level     = 1;
    const char* debug_level_str = getenv("DYNLINK_DEBUG_LEVEL");
    if (debug_level_str != NULL) {
        debug_level = atoi(debug_level_str);
        av_log(avctx, AV_LOG_DEBUG, " DYNLINK_DEBUG_LEVEL level: %d\n",
               debug_level);
    } else {
        av_log(avctx, AV_LOG_DEBUG,
               "DYNLINK_DEBUG_LEVEL environment variable"
               " is not set, default DYN_DEBUG_LEVEL_ERR\n");
    }

    dynlink_set_debug_level(debug_level);
}

static double default_cr(topscodecType_t id) {
    switch (id) {
        case TOPSCODEC_H264:
            return 20;
        case TOPSCODEC_HEVC:
            return 40;
        default:
            return 10; /* fallback */
    }
}

static size_t estimate_stream_size(size_t          raw_frame_size,
                                   topscodecType_t codec) {
    double cr = default_cr(codec);
    if (raw_frame_size == 0) return 0;

    double encoded_per_frame = raw_frame_size / cr;
    return (size_t)(encoded_per_frame + 0.5);
}

static int topsenc_build_params(topscodecEncParams_t* codec_param,
                                EFCodecEncContext_t*  ctx) {
    int                      tmp   = 0;
    AVCodecContext*          avctx = (AVCodecContext*)ctx->avctx;
    topscodecHdrColorDesc_t* hdr   = &codec_param->hdr_color_desc;
    av_assert0(ctx);

    // hdr
    av_log(avctx, AV_LOG_DEBUG, "hdr_mode: %d\n", ctx->hdr_mode);
    if (ctx->hdr_mode < 0 || ctx->hdr_mode > 2) {
        av_log(avctx, AV_LOG_WARNING,
            "hdr_mode is invalid value, reset to 0.\n");
        ctx->hdr_mode = 0;
    }
    if (ctx->hdr_mode == TOPSCODEC_ENC_HDR_MODE_HDR10 ||
        ctx->hdr_mode == TOPSCODEC_ENC_HDR_MODE_HDR10_PLUS) {
        codec_param->hdr_mode = ctx->hdr_mode;
        if (ctx->extra_sei) {
            ctx->hdr_mode         = TOPSCODEC_ENC_HDR_MODE_HDR10_PLUS;
            codec_param->hdr_mode = TOPSCODEC_ENC_HDR_MODE_HDR10_PLUS;
        }
        hdr->flags     = ctx->hdr_flags;
        hdr->range     = ctx->hdr_matrix_range;
        hdr->matrix    = ctx->hdr_matrix;
        hdr->primaries = (uint8_t)ctx->hdr_primaries;  // EFCODEC_PRIMARIES_*
        hdr->transfer  = (uint8_t)ctx->hdr_transfer;   // EFCODEC_TRANSFER_*

        // display primaries & white point（x,y）
        hdr->display.r.x = (u16_t)ctx->hdr_display_r_x;
        hdr->display.r.y = (u16_t)ctx->hdr_display_r_y;
        hdr->display.g.x = (u16_t)ctx->hdr_display_g_x;
        hdr->display.g.y = (u16_t)ctx->hdr_display_g_y;
        hdr->display.b.x = (u16_t)ctx->hdr_display_b_x;
        hdr->display.b.y = (u16_t)ctx->hdr_display_b_y;
        hdr->display.w.x = (u16_t)ctx->hdr_display_w_x;
        hdr->display.w.y = (u16_t)ctx->hdr_display_w_y;

        // display luminance
        hdr->display.luminance_min = (u16_t)ctx->hdr_display_lum_min;
        hdr->display.luminance_max = (u16_t)ctx->hdr_display_lum_max;

        // content light level
        hdr->content.luminance_max     = (u16_t)ctx->hdr_content_lum_max;
        hdr->content.luminance_average = (u16_t)ctx->hdr_content_lum_avg;

        // SAR/time
        hdr->aspect_ratio_idc  = (u16_t)ctx->hdr_aspect_ratio_idc;
        hdr->sar_width         = (u16_t)ctx->hdr_sar_width;
        hdr->sar_height        = (u16_t)ctx->hdr_sar_height;
        hdr->num_units_in_tick = (u32_t)ctx->hdr_num_units_in_tick;
        hdr->time_scale        = (u32_t)ctx->hdr_time_scale;

        // flags：按照数据是否有效设置
        // - 如果有显示主数据（display
        // primaries/luminance）或矩阵/primaries/transfer 非 0，则认为 mastering
        // display data 有效
        // - 如果 content 最大/平均亮度非 0，则认为 content light data 有效
        if (hdr->display.luminance_min || hdr->display.luminance_max ||
            hdr->display.r.x || hdr->display.r.y || hdr->display.g.x ||
            hdr->display.g.y || hdr->display.b.x || hdr->display.b.y ||
            hdr->display.w.x || hdr->display.w.y || hdr->primaries ||
            hdr->transfer || hdr->matrix || hdr->range) {
            hdr->flags |=
                EFCODEC_BUFFER_PARAM_COLOUR_FLAG_MASTERING_DISPLAY_DATA_VALID;
        }

        if (hdr->content.luminance_max || hdr->content.luminance_average) {
            hdr->flags |=
                EFCODEC_BUFFER_PARAM_COLOUR_FLAG_CONTENT_LIGHT_DATA_VALID;
        }

        av_log(avctx, AV_LOG_DEBUG, "hdr_flags: %d\n", ctx->hdr_flags);
        av_log(avctx, AV_LOG_DEBUG, "hdr_matrix_range: %d\n",
            ctx->hdr_matrix_range);
        av_log(avctx, AV_LOG_DEBUG, "hdr_matrix: %d\n", ctx->hdr_matrix);
        av_log(avctx, AV_LOG_DEBUG, "hdr_primaries: %d\n", ctx->hdr_primaries);
        av_log(avctx, AV_LOG_DEBUG, "hdr_transfer: %d\n", ctx->hdr_transfer);
        av_log(avctx, AV_LOG_DEBUG, "hdr_display_r_x: %d\n",
            ctx->hdr_display_r_x);
        av_log(avctx, AV_LOG_DEBUG, "hdr_display_r_y: %d\n",
            ctx->hdr_display_r_y);
        av_log(avctx, AV_LOG_DEBUG, "hdr_display_g_x: %d\n",
            ctx->hdr_display_g_x);
        av_log(avctx, AV_LOG_DEBUG, "hdr_display_g_y: %d\n",
            ctx->hdr_display_g_y);
        av_log(avctx, AV_LOG_DEBUG, "hdr_display_b_x: %d\n",
            ctx->hdr_display_b_x);
        av_log(avctx, AV_LOG_DEBUG, "hdr_display_b_y: %d\n",
            ctx->hdr_display_b_y);
        av_log(avctx, AV_LOG_DEBUG, "hdr_display_w_x: %d\n",
            ctx->hdr_display_w_x);
        av_log(avctx, AV_LOG_DEBUG, "hdr_display_w_y: %d\n",
            ctx->hdr_display_w_y);
        av_log(avctx, AV_LOG_DEBUG, "hdr_display_lum_min: %d\n",
            ctx->hdr_display_lum_min);
        av_log(avctx, AV_LOG_DEBUG, "hdr_display_lum_max: %d\n",
            ctx->hdr_display_lum_max);
        av_log(avctx, AV_LOG_DEBUG, "hdr_content_lum_max: %d\n",
            ctx->hdr_content_lum_max);
        av_log(avctx, AV_LOG_DEBUG, "hdr_content_lum_avg: %d\n",
            ctx->hdr_content_lum_avg);
        av_log(avctx, AV_LOG_DEBUG, "hdr_aspect_ratio_idc: %d\n",
            ctx->hdr_aspect_ratio_idc);
        av_log(avctx, AV_LOG_DEBUG, "hdr_sar_width: %d\n",
            ctx->hdr_sar_width);
        av_log(avctx, AV_LOG_DEBUG, "hdr_sar_height: %d\n",
            ctx->hdr_sar_height);
        av_log(avctx, AV_LOG_DEBUG, "hdr_num_units_in_tick: %d\n",
            ctx->hdr_num_units_in_tick);
        av_log(avctx, AV_LOG_DEBUG, "hdr_time_scale: %d\n",
            ctx->hdr_time_scale);
    }

    // --- hdr end ---
    av_log(avctx, AV_LOG_DEBUG, "color_range: %d\n", ctx->color_range);
    av_log(avctx, AV_LOG_DEBUG, "color_space: %d\n", ctx->colorspace);
    av_log(avctx, AV_LOG_DEBUG, "color_trc: %d\n", ctx->color_trc);
    av_log(avctx, AV_LOG_DEBUG, "color_primaries: %d\n", ctx->color_primaries);

    // fps
    codec_param->frame_rate_den = 10;
    codec_param->frame_rate_num = 10 * ctx->enc_fps;

    // gop type
    codec_param->coding_attr.gop_type =
        (topscodecEncGopType_t)ctx->enc_gop_type;

    // profile
    codec_param->coding_attr.profile = (topscodecEncProfile_t)ctx->profile;
    codec_param->coding_attr.level   = (topscodecEncLevel_t)ctx->level;

    // 码率
    codec_param->coding_attr.rc_attr.rc_mode =
        (topscodecEncRateCtrlMode_t)ctx->enc_rate_control;
    codec_param->coding_attr.rc_attr.const_qp_i = ctx->qp_i;  // default 23
    codec_param->coding_attr.rc_attr.const_qp_p = ctx->qp_p;  // default 25
    codec_param->coding_attr.rc_attr.const_qp_b = ctx->qp_b;  // default 27

    codec_param->coding_attr.rc_attr.init_qp_i = ctx->init_qp_i;  // default -1
    codec_param->coding_attr.rc_attr.init_qp_p = ctx->init_qp_p;  // default -1
    codec_param->coding_attr.rc_attr.init_qp_b = ctx->init_qp_b;  // default -1

    codec_param->coding_attr.rc_attr.target_bitrate = ctx->enc_bitrate;

    codec_param->coding_attr.gop_size         = ctx->enc_gop;
    codec_param->coding_attr.frame_interval_p = ctx->enc_b_frame_num;

    codec_param->input_pixel_format = ctx->data_pix_fmt_topscodec;
    codec_param->pic_width          = ctx->in_width;
    codec_param->max_width          = ctx->in_width;
    codec_param->pic_height         = ctx->in_height;
    codec_param->max_height         = ctx->in_height;
    codec_param->color_space        = ctx->conv_mode;

    codec_param->output_stream_buf_source =
        TOPSCODEC_BUF_SOURCE_LIB;  // alloc by rpc
    codec_param->output_stream_buf_num       = ctx->out_port_num;
    codec_param->output_stream_buf_dev_addr  = (u64_t)ctx->L3_addr_output;
    codec_param->output_stream_buf_size_each = ctx->output_buf_size_each_frame;
    codec_param->output_stream_stride_align  = ctx->output_stream_stride_align;

    codec_param->input_frame_buf_source =
        TOPSCODEC_BUF_SOURCE_LIB;  // alloc by rpc
    codec_param->input_frame_buf_num       = ctx->in_port_num;
    codec_param->input_frame_buf_dev_addr  = (u64_t)ctx->L3_addr_input;
    codec_param->input_frame_buf_size_each = ctx->input_buf_size_each_frame;
    codec_param->input_frame_stride_align  = ctx->input_frame_stride_align;

    // codec_param->reserved[4] = info.in_port_num;

    av_log(avctx, AV_LOG_DEBUG, "codec_param->frame_rate_den: %d\n",
           codec_param->frame_rate_den);
    av_log(avctx, AV_LOG_DEBUG, "codec_param->frame_rate_num: %d\n",
           codec_param->frame_rate_num);
    av_log(avctx, AV_LOG_DEBUG, "codec_param->coding_attr.gop_type: %d\n",
           codec_param->coding_attr.gop_type);
    av_log(avctx, AV_LOG_DEBUG, "codec_param->coding_attr.profile: %d\n",
           codec_param->coding_attr.profile);
    av_log(avctx, AV_LOG_DEBUG, "codec_param->coding_attr.level: %d\n",
           codec_param->coding_attr.level);
    av_log(avctx, AV_LOG_DEBUG,
           "codec_param->coding_attr.rc_attr.rc_mode: %d\n",
           codec_param->coding_attr.rc_attr.rc_mode);
    av_log(avctx, AV_LOG_DEBUG,
           "codec_param->coding_attr.rc_attr.const_qp_i: %d\n",
           codec_param->coding_attr.rc_attr.const_qp_i);
    av_log(avctx, AV_LOG_DEBUG,
           "codec_param->coding_attr.rc_attr.const_qp_p: %d\n",
           codec_param->coding_attr.rc_attr.const_qp_p);
    av_log(avctx, AV_LOG_DEBUG,
           "codec_param->coding_attr.rc_attr.const_qp_b: %d\n",
           codec_param->coding_attr.rc_attr.const_qp_b);
    av_log(avctx, AV_LOG_DEBUG,
           "codec_param->coding_attr.rc_attr.init_qp_i: %d\n",
           codec_param->coding_attr.rc_attr.init_qp_i);
    av_log(avctx, AV_LOG_DEBUG,
           "codec_param->coding_attr.rc_attr.init_qp_p: %d\n",
           codec_param->coding_attr.rc_attr.init_qp_p);
    av_log(avctx, AV_LOG_DEBUG,
           "codec_param->coding_attr.rc_attr.init_qp_b: %d\n",
           codec_param->coding_attr.rc_attr.init_qp_b);
    av_log(avctx, AV_LOG_DEBUG,
           "codec_param->coding_attr.rc_attr.target_bitrate: %d\n",
           codec_param->coding_attr.rc_attr.target_bitrate);
    av_log(avctx, AV_LOG_DEBUG, "codec_param->coding_attr.gop_size: %d\n",
           codec_param->coding_attr.gop_size);
    av_log(avctx, AV_LOG_DEBUG,
           "codec_param->coding_attr.frame_interval_p: %d\n",
           codec_param->coding_attr.frame_interval_p);
    av_log(avctx, AV_LOG_DEBUG, "codec_param->input_pixel_format: %d(%s)\n",
           codec_param->input_pixel_format,
           get_topspixfmt_name(codec_param->input_pixel_format));
    av_log(avctx, AV_LOG_DEBUG, "codec_param->pic_width: %d\n",
           codec_param->pic_width);
    av_log(avctx, AV_LOG_DEBUG, "codec_param->max_width: %d\n",
           codec_param->max_width);
    av_log(avctx, AV_LOG_DEBUG, "codec_param->pic_height: %d\n",
           codec_param->pic_height);
    av_log(avctx, AV_LOG_DEBUG, "codec_param->max_height: %d\n",
           codec_param->max_height);
    av_log(avctx, AV_LOG_DEBUG, "codec_param->color_space: %d\n",
           codec_param->color_space);
    av_log(avctx, AV_LOG_DEBUG, "codec_param->output_stream_buf_source: %d\n",
           codec_param->output_stream_buf_source);
    av_log(avctx, AV_LOG_DEBUG, "codec_param->output_stream_buf_num: %d\n",
           codec_param->output_stream_buf_num);
    av_log(avctx, AV_LOG_DEBUG,
           "codec_param->output_stream_buf_dev_addr: 0x%lx\n",
           codec_param->output_stream_buf_dev_addr);
    av_log(avctx, AV_LOG_DEBUG,
           "codec_param->output_stream_buf_size_each: %d\n",
           codec_param->output_stream_buf_size_each);
    av_log(avctx, AV_LOG_DEBUG, "codec_param->output_stream_stride_align: %d\n",
           codec_param->output_stream_stride_align);
    av_log(avctx, AV_LOG_DEBUG, "codec_param->input_frame_buf_source: %d\n",
           codec_param->input_frame_buf_source);
    av_log(avctx, AV_LOG_DEBUG, "codec_param->input_frame_buf_num: %d\n",
           codec_param->input_frame_buf_num);
    av_log(avctx, AV_LOG_DEBUG,
           "codec_param->input_frame_buf_dev_addr: 0x%lx\n",
           codec_param->input_frame_buf_dev_addr);
    av_log(avctx, AV_LOG_DEBUG, "codec_param->input_frame_buf_size_each: %d\n",
           codec_param->input_frame_buf_size_each);
    av_log(avctx, AV_LOG_DEBUG, "codec_param->input_frame_stride_align: %d\n",
           codec_param->input_frame_stride_align);
    av_log(avctx, AV_LOG_DEBUG, "codec_param->reserved[4]: %d\n",
           codec_param->reserved[4]);

    if (ctx->enable_crop) {
        av_log(avctx, AV_LOG_DEBUG, "Open crop options.\n");
        if (ctx->crop.top > (uint32_t)ctx->in_height ||
            ctx->crop.left > (uint32_t)ctx->in_width ||
            ctx->crop.bottom > (uint32_t)ctx->in_height ||
            ctx->crop.right > (uint32_t)ctx->in_width ||
            ctx->crop.top >= ctx->crop.bottom ||
            ctx->crop.left >= ctx->crop.right ||
            ctx->crop.bottom - ctx->crop.top < 8 ||
            ctx->crop.right - ctx->crop.left < 8) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid crop dim(left:%d,top:%d)(right:%d,bottom:%d)\n",
                   ctx->crop.left, ctx->crop.top, ctx->crop.right,
                   ctx->crop.bottom);
            return AVERROR(EINVAL);
        }
        ctx->out_width  = ctx->crop.right - ctx->crop.left;
        ctx->out_height = ctx->crop.bottom - ctx->crop.top;
    }


    if (ctx->enable_rotation) {
        if (ctx->rotation == 90 || ctx->rotation == 180 ||
            ctx->rotation == 270) {
            av_log(avctx, AV_LOG_DEBUG, "Open rotation option:%d.\n",
                   ctx->rotation);
        } else {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid rotation value:[%d], only support 90/180/270\n",
                   ctx->rotation);
            return AVERROR(EINVAL);
        }
        if (ctx->rotation == 90 || ctx->rotation == 270) {
            tmp             = ctx->out_width;
            ctx->out_width  = ctx->out_height;
            ctx->out_height = tmp;
        }
    }

    if (ctx->enable_crop && ctx->enable_rotation) {
        av_log(avctx, AV_LOG_ERROR,
               "Set Parameter error, Rotation and Crop "
               "can not be set at the same time. \n");
        return AVERROR(EINVAL);
    }


    /* Set Rotation Parameter */
    if (ctx->enable_rotation) {
        codec_param->pp_attr.rotation.enable = 1;
        switch (ctx->rotation) {
            case 90:
                codec_param->pp_attr.rotation.rotation = TOPSCODEC_ROTATION_90;
                break;
            case 180:
                codec_param->pp_attr.rotation.rotation = TOPSCODEC_ROTATION_180;
                break;
            case 270:
                codec_param->pp_attr.rotation.rotation = TOPSCODEC_ROTATION_270;
                break;
            default:
                codec_param->pp_attr.rotation.rotation =
                    TOPSCODEC_ROTATION_NONE;
                break;
        }
        av_log(avctx, AV_LOG_DEBUG, "Setting rotation, rotation:%d\n",
               ctx->rotation);
    }


    /* Set Crop Parameter */
    if (ctx->enable_crop) {
        codec_param->pp_attr.crop.enable = 1;
        codec_param->pp_attr.crop.tl_x   = ctx->crop.left;
        codec_param->pp_attr.crop.tl_y   = ctx->crop.top;
        codec_param->pp_attr.crop.br_x   = ctx->crop.right;
        codec_param->pp_attr.crop.br_y   = ctx->crop.bottom;
        av_log(avctx, AV_LOG_DEBUG,
               "Setting crop,crop dim:"
               "((left,top)(right,bottom)):"
               "((%dx%d),(%dx%d))\n",
               ctx->crop.left, ctx->crop.top, ctx->crop.right,
               ctx->crop.bottom);
    }

    return 0;
}

static i32_t topsenc_process_event_new_frame(AVCodecContext*      avctx,
                                             EFCodecEncContext_t* ctx,
                                             topscodecStream_t*  stream) {
    topscodecStream_t stream_copy = *stream;

    ff_mutex_lock(&ctx->mid_avpacket_fifo_mutex);
    if (ctx->close_flag) {
        ff_mutex_unlock(&ctx->mid_avpacket_fifo_mutex);
        av_log(avctx, AV_LOG_VERBOSE, "[%p]close flag is 1, do not process callback event: NEW_FRAME\n", ctx->handle);
        return 0;
    }
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
    if (av_fifo_can_write(ctx->mid_avpacket_fifo) < 1) {
        av_fifo_grow2(ctx->mid_avpacket_fifo, MAX_STREAM_NUM);
    }
    av_fifo_write(ctx->mid_avpacket_fifo, &stream_copy, 1);
#else
    if (av_fifo_space(ctx->mid_avpacket_fifo) < (int)sizeof(topscodecStream_t)) {
        av_fifo_grow(ctx->mid_avpacket_fifo, MAX_STREAM_NUM * sizeof(topscodecStream_t));
    }
    av_fifo_generic_write(ctx->mid_avpacket_fifo, &stream_copy, sizeof(topscodecStream_t), NULL);
#endif
    ff_mutex_unlock(&ctx->mid_avpacket_fifo_mutex);

    pthread_cond_signal(&ctx->mid_avpacket_fifo_cond);

    return TOPSCODEC_SUCCESS;
}

static i32_t topsenc_process_event_frame_processed(AVCodecContext*      avctx,
                                                   EFCodecEncContext_t* ctx,
                                                   void*  event_data) {
    topscodecFrame_t frame;
    memcpy(&frame, event_data, sizeof(topscodecFrame_t));

    ff_mutex_lock(&ctx->import_frame_fifo_mutex);
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
    if (av_fifo_can_write(ctx->import_frame_fifo) < 1) {
        av_fifo_grow2(ctx->import_frame_fifo, MAX_STREAM_NUM);
    }
    av_fifo_write(ctx->import_frame_fifo, &frame, 1);
#else
    if (av_fifo_space(ctx->import_frame_fifo) < sizeof(topscodecFrame_t)) {
        av_fifo_grow(ctx->import_frame_fifo, MAX_STREAM_NUM * sizeof(topscodecFrame_t));
    }
    av_fifo_generic_write(ctx->import_frame_fifo, &frame, sizeof(topscodecFrame_t), NULL);
#endif
    ff_mutex_unlock(&ctx->import_frame_fifo_mutex);

    sem_post(&ctx->import_frame_fifo_sem);

    return TOPSCODEC_SUCCESS;
}

static i32_t topsenc_event_callback(topscodecHandle_t    handle,
                                    topscodecEventType_t event,
                                    void* event_data,
                                    void* user_data) {
    AVCodecContext*      avctx = (AVCodecContext*)user_data;
    EFCodecEncContext_t* ctx   = (EFCodecEncContext_t*)(avctx->priv_data);

    switch (event) {
        case TOPSCODEC_EVENT_NEW_FRAME:
            return topsenc_process_event_new_frame(avctx, ctx, (topscodecStream_t*)(event_data));
        case TOPSCODEC_EVENT_FRAME_PROCESSED:
            return topsenc_process_event_frame_processed(avctx, ctx, event_data);
        case TOPSCODEC_EVENT_EOS:
            atomic_store(&ctx->recv_outport_eos, 1);
            pthread_cond_signal(&ctx->mid_avpacket_fifo_cond);
            av_log(avctx, AV_LOG_VERBOSE, "callback event got TOPSCODEC_EVENT_EOS.\n");
            break;
        case TOPSCODEC_EVENT_SEQUENCE:
        case TOPSCODEC_EVENT_BITSTREAM_PROCESSED:
        case TOPSCODEC_EVENT_OUT_OF_MEMORY:
        case TOPSCODEC_EVENT_STREAM_CORRUPT:
        case TOPSCODEC_EVENT_STREAM_NOT_SUPPORTED:
        case TOPSCODEC_EVENT_BUFFER_OVERFLOW:
        case TOPSCODEC_EVENT_FATAL_ERROR:
            av_log(avctx, AV_LOG_VERBOSE, "callback event got: %s\n", get_event_type_string(event));
            break;
    }
    return TOPSCODEC_SUCCESS;
}

static int topsenc_config_pic_attr(AVCodecContext*        avctx,
                                   topscodecEncPicAttr_t* frame_attr) {
    EFCodecEncContext_t* ctx = avctx->priv_data;
    memset(frame_attr, 0, sizeof(topscodecEncPicAttr_t));

    if (ctx->codec_type == TOPSCODEC_H264 ||
        ctx->codec_type == TOPSCODEC_HEVC) {
        frame_attr->vid_pic_attr.input_stride = ctx->input_frame_stride_align;
        // hdr10plus
        if (ctx->hdr10plus_t35_buff_L3_data_size > 0) {
            frame_attr->vid_pic_attr.sei_attr_array_cnt = 1;
            frame_attr->vid_pic_attr.sei_attr_array_addr =
                (u64_t)ctx->sei_payload_L3_addr;
        }
        //av_log(avctx, AV_LOG_DEBUG, "hdr sei attr: payload: 0x%lx, num: %d\n",
        //     frame_attr->vid_pic_attr.sei_attr_array_addr,
        //     frame_attr->vid_pic_attr.sei_attr_array_cnt);
        // print_sei_attr_ffmpeg(avctx, ctx->sei_attr, 0);
    } else if (ctx->codec_type == TOPSCODEC_JPEG) {
        frame_attr->jpg_pic_attr.jpeg_param.quality = 80;
    } else {
        av_log(avctx, AV_LOG_ERROR, "codec_type not supported\n");
        return AVERROR_BUG;
    }
    return 0;
}

static int ff_topscodec_encode_init_internal(AVCodecContext* avctx) {
    int ret = TOPSCODEC_SUCCESS;

    EFCodecEncContext_t*     ctx             = NULL;
    AVHWDeviceContext*       av_device_ctx   = NULL;
    TOPSCodecDeviceContext*  tops_device_ctx = NULL;
    topsPointerAttribute_t   att;
    AVCodecDescriptor const* input_descriptor;
    char card_idx[16] = {0};

    if (NULL == avctx || NULL == avctx->priv_data) {
        av_log(avctx, AV_LOG_ERROR, "Early error in topscodec_encode_init func.\n");
        return AVERROR_BUG;
    }

    ctx        = avctx->priv_data;
    ctx->avctx = avctx;

    ff_ffmpeg_gcu_print_version();

    if (ctx->encoder_init_flag == 1) {
        av_log(avctx, AV_LOG_ERROR, "Error, encode double init. \n");
        return AVERROR_BUG;
    }

    // init params
    atomic_init(&ctx->recv_outport_eos, 0);
    ctx->close_flag         = 0;
    ctx->total_packet_count = 0;
    ctx->total_frame_count  = 0;
    ctx->draining           = 0;
    ctx->first_iframe       = 1;
    ctx->spspps_pkt         = NULL;
    ctx->output_frame_num   = 0;
    ctx->initial_delay_time = 0;
    if (avctx->bit_rate > 0) {
        ctx->enc_bitrate = avctx->bit_rate;
        av_log(avctx, AV_LOG_DEBUG, "Set enc_bitrate from avctx->bit_rate: %d\n", ctx->enc_bitrate);
    }
    if (avctx->gop_size > 0) {
        ctx->enc_gop = avctx->gop_size;
        av_log(avctx, AV_LOG_DEBUG, "Set enc_gop from avctx->gop_size: %d\n", ctx->enc_gop);
    }
    if (avctx->max_b_frames >= 0) {
        ctx->enc_b_frame_num = avctx->max_b_frames;
        av_log(avctx, AV_LOG_DEBUG, "Set enc_b_frame_num from avctx->max_b_frames: %d\n", ctx->enc_b_frame_num);
    }
    // ctx->gop_size = ((ctx->enc_gop - 1) / (ctx->enc_b_frame_num + 1)) * (ctx->enc_b_frame_num + 1) + 1;

    av_log(avctx, AV_LOG_DEBUG, "in_port_num:%d, out_port_num:%d\n", ctx->in_port_num, ctx->out_port_num);

    if (avctx->width <= 0 || avctx->height <= 0) {
        av_log(avctx, AV_LOG_ERROR, "Invalid width:%d, or height:%d\n", avctx->width, avctx->height);
        ret = AVERROR(EINVAL);
        goto error;
    }
    if (ctx->in_width <= 0 || ctx->in_height <= 0) {
        ctx->in_width  = avctx->width;
        ctx->in_height = avctx->height;
    }
    av_log(avctx, AV_LOG_DEBUG,
           "in_width:%d, in_height:%d, out_width:%d, out_height:%d\n",
           ctx->in_width, ctx->in_height, ctx->out_width, ctx->out_height);

    if (avctx->pix_fmt == AV_PIX_FMT_NONE) {
        av_log(avctx, AV_LOG_ERROR, "Invalid pixel format, pix_fmt:%s\n", av_get_pix_fmt_name(avctx->pix_fmt));
        ret = AVERROR(EINVAL);
        goto error;
    }

    if (avctx->pix_fmt == AV_PIX_FMT_TOPSCODEC) {
        ctx->data_pix_fmt_av = avctx->sw_pix_fmt;
    } else {
        ctx->data_pix_fmt_av = avctx->pix_fmt;
    }

    // hw create
    snprintf(card_idx, sizeof(card_idx), "%d", ctx->card_id);
    av_log(avctx, AV_LOG_VERBOSE, "avctx->hw_frames_ctx = %p\n", (void *)avctx->hw_frames_ctx);
    if (avctx->hw_frames_ctx) {  // if hw_frames_ctx set by user
        av_buffer_unref(&ctx->hwframe);
        ctx->hwframe = av_buffer_ref(avctx->hw_frames_ctx);
        if (!ctx->hwframe) {
            ret = AVERROR(EINVAL);
            goto error;
        }

        ctx->hwframe_ctx             = (AVHWFramesContext*)ctx->hwframe->data;
        ctx->hwframe_ctx->device_ctx = (AVHWDeviceContext*)ctx->hwframe_ctx->device_ref->data;
        ctx->hwdevice                = av_buffer_ref(ctx->hwframe_ctx->device_ref);
        if (!ctx->hwdevice) {
            av_log(avctx, AV_LOG_ERROR, "A hardware frames or device context is required for hardware accelerated encoding.\n");
            ret = AVERROR(EINVAL);
            goto error;
        }
    } else {
        ret = av_hwdevice_ctx_create(&ctx->hwdevice, AV_HWDEVICE_TYPE_TOPSCODEC, card_idx, NULL, 0);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Hardware device context create failed, ret: %d.\n", ret);
            goto error;
        }

        ctx->hwframe = av_hwframe_ctx_alloc(ctx->hwdevice);
        if (!ctx->hwframe) {
            av_log(avctx, AV_LOG_ERROR, "Error, av_hwframe_ctx_alloc failed.\n");
            ret = AVERROR(EINVAL);
            goto error;
        }
        ctx->hwframe_ctx     = (AVHWFramesContext*)ctx->hwframe->data;
        avctx->hw_frames_ctx = av_buffer_ref(ctx->hwframe);
    }
    ctx->data_pix_fmt_topscodec = avpixfmt_2_topspixfmt(ctx->data_pix_fmt_av);
    av_log(avctx, AV_LOG_DEBUG, "data_pix_fmt_av:%s\n", get_avpixfmt_name(ctx->data_pix_fmt_av));

    ctx->output_stream_stride_align = 0;

    if (ctx->input_frame_stride_align <= 0) {
        ctx->input_frame_stride_align = 1;
    }
    if (ctx->input_frame_stride_align > 2048) {
        av_log(avctx, AV_LOG_ERROR, "stride alignment must be in the range [0, 2048]\n");
        ret = AVERROR(EINVAL);
        goto error;
    }
    if ((ctx->input_frame_stride_align & (ctx->input_frame_stride_align - 1)) != 0) {
        av_log(avctx, AV_LOG_ERROR, "stride alignment must be power of 2\n");
        ret = AVERROR(EINVAL);
        goto error;
    }

    av_device_ctx                 = ctx->hwframe_ctx->device_ctx;
    tops_device_ctx               = av_device_ctx->hwctx;
    tops_device_ctx->stride_align = ctx->input_frame_stride_align;
    ctx->topsruntime_lib_ctx      = tops_device_ctx->topsruntime_lib_ctx;
    if (!ctx->topsruntime_lib_ctx) {
        av_log(avctx, AV_LOG_ERROR, "topsruntime_lib_ctx is NULL\n");
        ret = AVERROR(EINVAL);
        goto error;
    }

    if (!ctx->hwframe_ctx->pool) {
        ctx->hwframe_ctx->format    = AV_PIX_FMT_TOPSCODEC;
        ctx->hwframe_ctx->sw_format = ctx->data_pix_fmt_av;
        ctx->hwframe_ctx->width     = ctx->in_width;
        ctx->hwframe_ctx->height    = ctx->in_height;
        if (ctx->hwframe_ctx->width > 0 && ctx->hwframe_ctx->height > 0) {
            ctx->hwframe_ctx->initial_pool_size = 3;
            ctx->hwframe_ctx->pool              = NULL;
            if ((ret = av_hwframe_ctx_init(ctx->hwframe)) < 0) {
                av_log(avctx, AV_LOG_ERROR, "Error, av_hwframe_ctx_init failed, ret: %d\n", ret);
                ret = AVERROR(EINVAL);
                goto error;
            }
            av_log(avctx, AV_LOG_DEBUG, "hw frame init 1 success.\n");
        }
    }

    // get codec type
    ctx->codec_type   = get_codec_type(avctx);
    avctx->codec_type = AVMEDIA_TYPE_VIDEO;

    ctx->frame_size                 = av_image_get_buffer_size(ctx->data_pix_fmt_av, ctx->in_width, ctx->in_height, ctx->input_frame_stride_align);
    ctx->input_buf_size_each_frame  = ctx->frame_size;
    ctx->output_buf_size_each_frame = estimate_stream_size(ctx->frame_size, ctx->codec_type) * 2;

    av_log(avctx, AV_LOG_DEBUG,
           "frame_size:%d, input_buf_size_each_frame:%d, output_buf_size_each_frame:%d, output_stream_stride_align:%d, input_frame_stride_align:%d\n",
           ctx->frame_size, ctx->input_buf_size_each_frame, ctx->output_buf_size_each_frame, ctx->output_stream_stride_align, ctx->input_frame_stride_align);

    ctx->buffer_input_frame_size = FFALIGN(ctx->input_buf_size_each_frame * ctx->in_port_num, 4096);
    ctx->buffer_output_pkt_size  = FFALIGN(ctx->output_buf_size_each_frame * ctx->out_port_num, 4096);
    av_log(avctx, AV_LOG_DEBUG,
           "buffer_input_frame_size:%zu, buffer_output_pkt_size:%zu\n",
           ctx->buffer_input_frame_size, ctx->buffer_output_pkt_size);

    ctx->frame = av_frame_alloc();
    if (!ctx->frame) {
        av_log(avctx, AV_LOG_ERROR, "Failed to allocate frame\n");
        ret = AVERROR(ENOMEM);
        goto error;
    }
    ctx->sw_frame = av_frame_alloc();
    if (!ctx->sw_frame) {
        av_log(avctx, AV_LOG_ERROR, "Failed to allocate sw_frame\n");
        ret = AVERROR(ENOMEM);
        goto error;
    }
    ctx->pkt = av_packet_alloc();
    if (!ctx->pkt) {
        av_log(avctx, AV_LOG_ERROR, "Failed to allocate pkt\n");
        ret = AVERROR(ENOMEM);
        goto error;
    }
    ctx->ef_buf_frame = (EFBuffer*)av_mallocz(sizeof(EFBuffer));
    if (!ctx->ef_buf_frame) {
        av_log(avctx, AV_LOG_ERROR, "Failed to allocate ef_buf_frame\n");
        ret = AVERROR(ENOMEM);
        goto error;
    }
    ctx->ef_buf_frame->type = EF_BUFFER_TYPE_FRAME;
    ctx->ef_buf_pkt = (EFBuffer*)av_mallocz(sizeof(EFBuffer));
    if (!ctx->ef_buf_pkt) {
        av_log(avctx, AV_LOG_ERROR, "Failed to allocate ef_buf_pkt\n");
        ret = AVERROR(ENOMEM);
        goto error;
    }
    ctx->ef_buf_pkt->type = EF_BUFFER_TYPE_PKT;

    ctx->ef_buf_frame->avctx                 = avctx;
    ctx->ef_buf_frame->ef_enc_context        = ctx;
    ctx->ef_buf_frame->ef_frame_pkt_buf_size = ctx->input_buf_size_each_frame;
    ret = ff_topscodec_alloc_efbuf_internal_data(ctx->ef_buf_frame);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR, "Error, alloc_efbuf_internal_data failed, ret: %d\n", ret);
        ret = AVERROR(EINVAL);
        goto error;
    }

    debug_log_set(avctx);

    ret = topscodec_load_functions(&ctx->topscodec_lib_ctx);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR, "Error, topscodec_lib_load failed, ret: %d\n", ret);
        ret = AVERROR(EINVAL);
        goto error;
    }

    if (!ctx->topscodec_lib_ctx->lib_topscodecEncGetCaps ||
        !ctx->topscodec_lib_ctx->lib_topscodecEncOpenEncodeSession ||
        !ctx->topscodec_lib_ctx->lib_topscodecEncEncodePicture ||
        !ctx->topscodec_lib_ctx->lib_topscodecEncDestroy ||
        !ctx->topscodec_lib_ctx->lib_topscodecEncSetParams ||
        !ctx->topscodec_lib_ctx->lib_topscodecEncUnlockBitstream) {
        av_log(avctx, AV_LOG_ERROR, "Encode API symbol not available in libtopscodec.so\n");
        ret = AVERROR(ENOSYS);
        goto error;
    }

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
    if (ctx->hdr_mode == TOPSCODEC_ENC_HDR_MODE_HDR10 ||
        ctx->hdr_mode == TOPSCODEC_ENC_HDR_MODE_HDR10_PLUS) {
        ret = handle_hdr10_frame_side_data(avctx, ctx);
        if (ret != 0) {
            av_log(avctx, AV_LOG_ERROR, "Failed handling side data! ret: %s\n", av_err2str(ret));
            goto error;
        }
    }
    if (ctx->extra_sei) {
        ret = topsenc_alloc_hdr10plus_t35_buff(ctx);
        if (ret != 0) {
            av_log(avctx, AV_LOG_ERROR, "topsenc_alloc_hdr10plus_t35_buff failed, ret: %d\n", ret);
            goto error;
        }
    }
    ctx->import_frame_fifo = av_fifo_alloc2(MAX_STREAM_NUM, sizeof(topscodecFrame_t), 0);
    if (!ctx->import_frame_fifo) {
        av_log(avctx, AV_LOG_ERROR, "init import_frame_fifo fail (ENOMEM)\n");
        ret = AVERROR(ENOMEM);
        goto error;
    }
    ctx->mid_avpacket_fifo = av_fifo_alloc2(MAX_STREAM_NUM, sizeof(topscodecStream_t), 0);
    if (!ctx->mid_avpacket_fifo) {
        av_log(avctx, AV_LOG_ERROR, "init mid_avpacket_fifo fail (ENOMEM)\n");
        ret = AVERROR(ENOMEM);
        goto error;
    }
    ctx->frame_timestamp_queue = av_fifo_alloc2(TIMESTAMP_QUEUE_SIZE, sizeof(int64_t), 0);
    if (!ctx->frame_timestamp_queue) {
        av_log(avctx, AV_LOG_ERROR, "Failed to init timestamp fifo (ENOMEM)\n");
        ret = AVERROR(ENOMEM);
        goto error;
    }
#else
    ctx->import_frame_fifo = av_fifo_alloc(MAX_STREAM_NUM * sizeof(topscodecFrame_t));
    if (!ctx->import_frame_fifo) {
        av_log(avctx, AV_LOG_ERROR, "init import_frame_fifo fail (ENOMEM)\n");
        ret = AVERROR(ENOMEM);
        goto error;
    }
    ctx->mid_avpacket_fifo = av_fifo_alloc(MAX_STREAM_NUM * sizeof(topscodecStream_t));
    if (!ctx->mid_avpacket_fifo) {
        av_log(avctx, AV_LOG_ERROR, "init mid_avpacket_fifo fail (ENOMEM)\n");
        ret = AVERROR(ENOMEM);
        goto error;
    }
    ctx->frame_timestamp_queue = av_fifo_alloc(TIMESTAMP_QUEUE_SIZE * sizeof(int64_t));
    if (!ctx->frame_timestamp_queue) {
        av_log(avctx, AV_LOG_ERROR, "Failed to init timestamp fifo (ENOMEM)\n");
        ret = AVERROR(ENOMEM);
        goto error;
    }
#endif

    ret = ff_mutex_init(&ctx->import_frame_fifo_mutex, NULL);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR, "init import_frame_fifo_mutex fail, ret(%d)\n", ret);
        ret = AVERROR(EINVAL);
        goto error;
    }
    ctx->import_frame_fifo_mutex_inited = 1;

    ret = sem_init(&ctx->import_frame_fifo_sem, 0, 0);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR, "init import_frame_fifo_sem fail: %s\n", strerror(errno));
        ret = AVERROR(errno);
        goto error;
    }
    ctx->import_frame_fifo_sem_inited = 1;

    ret = ff_mutex_init(&ctx->mid_avpacket_fifo_mutex, NULL);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR, "init mid_avpacket_fifo_mutex fail, ret(%d)\n", ret);
        ret = AVERROR(EINVAL);
        goto error;
    }
    ctx->mid_avpacket_fifo_mutex_inited = 1;

    ret = pthread_cond_init(&ctx->mid_avpacket_fifo_cond, NULL);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR, "init mid_avpacket_fifo_cond fail, ret(%d)\n", ret);
        ret = AVERROR(EINVAL);
        goto error;
    }
    ctx->mid_avpacket_fifo_cond_inited = 1;

    // // get device card id
    // if (ctx->card_id == 0) {
    //     ctx->card_id = get_card_id_from_env();
    // }
    // // get device id
    // if (ctx->device_id == 0) {
    //     ctx->device_id = get_device_id_from_env();
    // }

    av_log(avctx, AV_LOG_DEBUG, "GCU video core balance mode: %d\n", ctx->balance);
    if (ctx->balance == 1) {
        ret = ctx->topscodec_lib_ctx->lib_topscodecSetVideoCoreBalancingPolicy(TOPSCODEC_VIDEO_CORE_BALANCING_LOADING);
        if (TOPSCODEC_SUCCESS != ret) {
            av_log(avctx, AV_LOG_ERROR, "Error, topscodecSetVideoCoreBalancingPolicy failed, ret: %d\n", ret);
            ret = AVERROR(EINVAL);
            goto error;
        }
        av_log(avctx, AV_LOG_DEBUG, "topscodecSetVideoCoreBalancingPolicy success.\n");
    }

    // Write back avctx->profile for muxers/metadata compatibility
    switch (ctx->profile) {
    case EFCODEC_ENC_PROFILE_H264_BASELINE:
        avctx->profile = FF_PROFILE_H264_BASELINE;
        break;
    case EFCODEC_ENC_PROFILE_H264_MAIN:
        avctx->profile = FF_PROFILE_H264_MAIN;
        break;
    case EFCODEC_ENC_PROFILE_H264_HIGH:
        avctx->profile = FF_PROFILE_H264_HIGH;
        break;
    case EFCODEC_ENC_PROFILE_H264_HIGH_10:
        avctx->profile = FF_PROFILE_H264_HIGH_10;
        break;
    case EFCODEC_ENC_PROFILE_HEVC_MAIN:
        avctx->profile = FF_PROFILE_HEVC_MAIN;
        break;
    case EFCODEC_ENC_PROFILE_HEVC_MAIN_10:
        avctx->profile = FF_PROFILE_HEVC_MAIN_10;
        break;
    case EFCODEC_ENC_PROFILE_HEVC_MAIN_STILL:
        avctx->profile = FF_PROFILE_HEVC_MAIN_STILL_PICTURE;
        break;
    default:
        break;
    }
    av_log(avctx, AV_LOG_DEBUG, "profile: %d, avctx->profile: %d, level: %d, avctx->level: %d\n", ctx->profile, avctx->profile, ctx->level, avctx->level);
    if (ctx->profile == EFCODEC_ENC_PROFILE_H264_BASELINE && ctx->enc_b_frame_num > 0) {
        av_log(avctx, AV_LOG_ERROR, "H.264 Baseline profile does not support B-frames. Please set -bf 0 or choose a different profile (main/high).\n");
        ret = AVERROR(EINVAL);
        goto error;
    }

    if (avctx->color_range >= 0) {
        ctx->color_range = avctx->color_range;
        av_log(avctx, AV_LOG_DEBUG, "Set enc_color_range from avctx->color_range: %d\n", ctx->color_range);
    }
    if (avctx->colorspace >= 0) {
        ctx->colorspace = avctx->colorspace;
        av_log(avctx, AV_LOG_DEBUG, "Set enc_color_space from avctx->color_space: %d\n", ctx->colorspace);
    }
    if (avctx->color_trc >= 0) {
        ctx->color_trc = avctx->color_trc;
        av_log(avctx, AV_LOG_DEBUG, "Set enc_color_trc from avctx->color_trc: %d\n", ctx->color_trc);
    }
    if (avctx->color_primaries >= 0) {
        ctx->color_primaries = avctx->color_primaries;
        av_log(avctx, AV_LOG_DEBUG, "Set enc_color_primaries from avctx->color_primaries: %d\n", ctx->color_primaries);
    }

    /*=================== 1. get encode caps ===================*/
    av_log(avctx, AV_LOG_DEBUG, "topscodecEncGetCaps: type[%d], card[%d], dev[%d]\n", ctx->codec_type, ctx->card_id, ctx->device_id);
    memset(&ctx->caps, 0, sizeof(ctx->caps));
    ret = ctx->topscodec_lib_ctx->lib_topscodecEncGetCaps(ctx->codec_type, ctx->card_id, ctx->device_id, &ctx->caps);
    if (TOPSCODEC_SUCCESS != ret) {
        av_log(avctx, AV_LOG_ERROR, "Error, topscodecEncGetCaps failed, ret: %d\n", ret);
        ret = AVERROR(EINVAL);
        goto error;
    }

    print_enc_caps(avctx, &ctx->caps);

    input_descriptor = avcodec_descriptor_get(avctx->codec->id);
    if (!input_descriptor) {
        av_log(avctx, AV_LOG_ERROR, "No codec descriptor found for codec id %d\n", avctx->codec->id);
        ret = AVERROR_BUG;
        goto error;
    }

    if (!ctx->caps.supported) {
        av_log(avctx, AV_LOG_ERROR, "Invalid tops codec %s\n", input_descriptor->long_name);
        ret = AVERROR_BUG;
        goto error;
    }
    av_log(avctx, AV_LOG_DEBUG, "Topscodec enc support: %s\n", input_descriptor->long_name);

    if (avctx->width < ctx->caps.min_width || avctx->width > ctx->caps.max_width ||
        avctx->height < ctx->caps.min_height || avctx->height > ctx->caps.max_height) {
        av_log(avctx, AV_LOG_ERROR, "Resolution %dx%d out of supported range [%d-%d]x[%d-%d]\n",
               avctx->width, avctx->height, ctx->caps.min_width, ctx->caps.max_width, ctx->caps.min_height, ctx->caps.max_height);
        ret = AVERROR(EINVAL);
        goto error;
    }

    /*=================== 2. create enc ===================*/
    memset(&ctx->create_info, 0, sizeof(ctx->create_info));
    /* ap log setting*/
    if (get_ap_log_on_off_from_env()) {
        ctx->create_info.reserved[0] = 1;
        ctx->create_info.reserved[1] = 1;
        /* Log level */
        for (int i = 0; i < 7; i++) {
            ctx->create_info.reserved[i + 2] = 5;
        }
        av_log(avctx, AV_LOG_DEBUG, "run in ap log mode\n");
    }

    ctx->create_info.reserved[9]  = 1; // sync with fw, always true.
    ctx->create_info.reserved[10] = ctx->sf; // switch frame num, set by user.
    av_log(avctx, AV_LOG_DEBUG, "switch frame number:%d\n", ctx->sf);

    ctx->create_info.card_id           = ctx->card_id;
    ctx->create_info.vcu_id            = ctx->device_id;
    ctx->create_info.hw_ctx_id         = 0x0F;  // hardware context id 固定值0x0F
    ctx->create_info.sw_ctx_id         = 0x08;  // software context id 固定值0x08
    ctx->create_info.output_codec_type = ctx->codec_type;
    ctx->create_info.run_mode          = TOPSCODEC_RUN_MODE_ASYNC; // async mode only
    ctx->create_info.callback          = topsenc_event_callback;
    ctx->create_info.user_context      = (u64_t)avctx;
    print_create_info(avctx, &ctx->create_info);
    ret = ctx->topscodec_lib_ctx->lib_topscodecEncOpenEncodeSession(&ctx->handle, &ctx->create_info);
    if (TOPSCODEC_SUCCESS != ret) {
        av_log(avctx, AV_LOG_ERROR, "Error, topscodecEncCreate failed, ret: %d\n", ret);
        ret = AVERROR(EINVAL);
        goto error;
    }
    av_log(avctx, AV_LOG_DEBUG, "[%p]topscodecEncOpenEncodeSession success\n", ctx->handle);

    /*=================== 3. set params ===================*/
    ret = ctx->topsruntime_lib_ctx->lib_topsMalloc((void**)&ctx->buffer_input_frame, ctx->buffer_input_frame_size);
    if (ret != topsSuccess) {
        av_log(avctx, AV_LOG_ERROR, "Error, topsMalloc failed, ret: %d\n", ret);
        ret = AVERROR(EINVAL);
        goto error;
    }
    av_log(avctx, AV_LOG_DEBUG, "buffer_input_frame alloc success:%p, size:%zu\n", ctx->buffer_input_frame, ctx->buffer_input_frame_size);

    ret = ctx->topsruntime_lib_ctx->lib_topsPointerGetAttributes(&att, (void*)(ctx->buffer_input_frame));
    if (ret != topsSuccess) {
        av_log(avctx, AV_LOG_ERROR, "Error, topsPointerGetAttributes failed, ret: %d\n", ret);
        ret = AVERROR(EINVAL);
        goto error;
    }
    ctx->L3_addr_input = (u64_t)att.device_pointer;
    av_log(avctx, AV_LOG_DEBUG, "L3_addr_input:%lx, size:%zu\n", ctx->L3_addr_input, ctx->buffer_input_frame_size);

    ret = ctx->topsruntime_lib_ctx->lib_topsMalloc((void**)&ctx->buffer_output_pkt, ctx->buffer_output_pkt_size);
    if (ret != topsSuccess) {
        av_log(avctx, AV_LOG_ERROR, "Error, topsMalloc failed, ret: %d\n", ret);
        ret = AVERROR(EINVAL);
        goto error;
    }
    av_log(avctx, AV_LOG_DEBUG, "buffer_output_pkt alloc success:%p, size:%zu\n", ctx->buffer_output_pkt, ctx->buffer_output_pkt_size);
    ret = ctx->topsruntime_lib_ctx->lib_topsPointerGetAttributes(&att, (void*)(ctx->buffer_output_pkt));
    if (ret != topsSuccess) {
        av_log(avctx, AV_LOG_ERROR, "Error, topsPointerGetAttributes failed, ret: %d\n", ret);
        ret = AVERROR(EINVAL);
        goto error;
    }
    ctx->L3_addr_output = (u64_t)att.device_pointer;
    av_log(avctx, AV_LOG_DEBUG, "L3_addr_output:%lx, size:%zu\n", ctx->L3_addr_output, ctx->buffer_output_pkt_size);

    memset(&ctx->encode_param, 0, sizeof(ctx->encode_param));
    ret = topsenc_build_params(&ctx->encode_param, ctx);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR, "topsenc_build_params failed, ret: %d\n", ret);
        ret = AVERROR(EINVAL);
        goto error;
    }

    ret = ctx->topscodec_lib_ctx->lib_topscodecEncSetParams(ctx->handle, &ctx->encode_param);
    if (TOPSCODEC_SUCCESS != ret) {
        av_log(avctx, AV_LOG_ERROR, "Error, topscodecEncSetParams failed, ret: %d\n", ret);
        ret = AVERROR(EINVAL);
        goto error;
    }
    av_log(avctx, AV_LOG_DEBUG, "[%p]topscodecEncSetParams success\n", ctx->handle);

    ctx->encoder_init_flag = 1;

error:
    /* FF_CODEC_CAP_INIT_CLEANUP is set, so close() will be called
     * by the framework to release all partially allocated resources. */
    return ret;
}

static int ff_topscodec_encode_init(AVCodecContext* avctx) {
    return ff_topscodec_encode_init_internal(avctx);
}

static int ff_topscodec_encode_close_internal(AVCodecContext* avctx) {
    int ret = 0;
    EFCodecEncContext_t* ctx = (EFCodecEncContext_t*)avctx->priv_data;

    if (ctx->mid_avpacket_fifo_mutex_inited) {
        ff_mutex_lock(&ctx->mid_avpacket_fifo_mutex);
        ctx->close_flag = 1;
        ff_mutex_unlock(&ctx->mid_avpacket_fifo_mutex);
    } else {
        ctx->close_flag = 1;
    }

    if (ctx->mid_avpacket_fifo) {
        while ((int)av_fifo_size(ctx->mid_avpacket_fifo) > 0) {
            topscodecStream_t tmp_stream;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
            av_fifo_read(ctx->mid_avpacket_fifo, &tmp_stream, 1);
#else
            av_fifo_generic_read(ctx->mid_avpacket_fifo, &tmp_stream, sizeof(topscodecStream_t), NULL);
#endif
            av_log(avctx, AV_LOG_DEBUG,
                   "close: unlock pending stream before destroy, remaining size:%d\n",
                   (int)av_fifo_size(ctx->mid_avpacket_fifo));
            if (ctx->topscodec_lib_ctx && ctx->topscodec_lib_ctx->lib_topscodecEncUnlockBitstream) {
                ctx->topscodec_lib_ctx->lib_topscodecEncUnlockBitstream(ctx->handle, &tmp_stream);
            }
        }
    }

    if (ctx->handle && ctx->topscodec_lib_ctx && ctx->topscodec_lib_ctx->lib_topscodecEncDestroy) {
        av_log(avctx, AV_LOG_DEBUG, "[%p]topscodecEncDestroy start\n", ctx->handle);
        ret = ctx->topscodec_lib_ctx->lib_topscodecEncDestroy(ctx->handle);
        if (TOPSCODEC_SUCCESS != ret) {
            av_log(avctx, AV_LOG_ERROR, "Error, topscodecEncDestroy failed, ret(%d)\n", ret);
        } else {
            av_log(avctx, AV_LOG_DEBUG, "topscodecEncDestroy success\n");
        }
        ctx->handle = NULL;
    }

    ctx->first_iframe = 0;
    if (ctx->spspps_pkt) {
        av_packet_free(&ctx->spspps_pkt);
        ctx->spspps_pkt = NULL;
    }

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
    topsenc_free_hdr10plus_t35_buff(ctx);
#endif

    if (ctx->buffer_input_frame && ctx->topsruntime_lib_ctx) {
        ctx->topsruntime_lib_ctx->lib_topsFree((void*)ctx->buffer_input_frame);
        ctx->buffer_input_frame = NULL;
        av_log(avctx, AV_LOG_DEBUG, "topsFree buffer_input_frame success\n");
    }
    if (ctx->buffer_output_pkt && ctx->topsruntime_lib_ctx) {
        ctx->topsruntime_lib_ctx->lib_topsFree((void*)ctx->buffer_output_pkt);
        ctx->buffer_output_pkt = NULL;
        av_log(avctx, AV_LOG_DEBUG, "topsFree buffer_output_pkt success\n");
    }

    if (ctx->ef_buf_frame) {
        ctx->ef_buf_frame->avctx          = avctx;
        ctx->ef_buf_frame->ef_enc_context = ctx;
        ff_topscodec_free_efbuf_internal_data(ctx->ef_buf_frame);
        av_freep(&ctx->ef_buf_frame);
        av_log(avctx, AV_LOG_DEBUG, "ef_buf_frame free\n");
    }
    if (ctx->ef_buf_pkt) {
        ctx->ef_buf_pkt->avctx          = avctx;
        ctx->ef_buf_pkt->ef_enc_context = ctx;
        ff_topscodec_free_efbuf_internal_data(ctx->ef_buf_pkt);
        av_freep(&ctx->ef_buf_pkt);
        av_log(avctx, AV_LOG_DEBUG, "ef_buf_pkt free\n");
    }
    if (ctx->frame) {
        av_frame_free(&ctx->frame);
    }
    if (ctx->sw_frame) {
        av_frame_free(&ctx->sw_frame);
    }
    if (ctx->pkt) {
        av_packet_free(&ctx->pkt);
    }

    ctx->encoder_init_flag = 0;

    if (ctx->topscodec_lib_ctx) {
        topscodec_free_functions(&ctx->topscodec_lib_ctx);
        av_log(avctx, AV_LOG_DEBUG, "topscodec_free_functions success\n");
    }

    if (ctx->hwframe) {
        av_buffer_unref(&ctx->hwframe);
        av_log(avctx, AV_LOG_DEBUG, "hwframe unref\n");
    }

    if (ctx->hwdevice) {
        av_buffer_unref(&ctx->hwdevice);
        av_log(avctx, AV_LOG_DEBUG, "hwdevice unref\n");
    }

    if (ctx->mid_avpacket_fifo) {
        av_fifo_freep(&ctx->mid_avpacket_fifo);
        av_log(avctx, AV_LOG_DEBUG, "mid_avpacket_fifo freep\n");
    }
    if (ctx->mid_avpacket_fifo_cond_inited) {
        pthread_cond_destroy(&ctx->mid_avpacket_fifo_cond);
    }
    if (ctx->mid_avpacket_fifo_mutex_inited)
        ff_mutex_destroy(&ctx->mid_avpacket_fifo_mutex);

    if (ctx->import_frame_fifo) {
        av_fifo_freep(&ctx->import_frame_fifo);
        av_log(avctx, AV_LOG_DEBUG, "import_frame_fifo freep\n");
    }
    if (ctx->import_frame_fifo_sem_inited)
        sem_destroy(&ctx->import_frame_fifo_sem);
    if (ctx->import_frame_fifo_mutex_inited)
        ff_mutex_destroy(&ctx->import_frame_fifo_mutex);

    if (ctx->frame_timestamp_queue) {
        av_fifo_freep(&ctx->frame_timestamp_queue);
    }

    return 0;
}

static int ff_topscodec_encode_close(AVCodecContext* avctx) {
    return ff_topscodec_encode_close_internal(avctx);
}

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 18, 100)  // n3.x
static int ff_encode_get_frame(AVCodecContext* avctx, AVFrame* frame) {
    AVCodecInternal* avci = avctx->internal;

    if (avci->draining) return AVERROR_EOF;

    if (!avci->buffer_frame->buf[0]) return AVERROR(EAGAIN);

    av_frame_move_ref(frame, avci->buffer_frame);

    return 0;
}
#endif

static int topsenc_send_frame(AVCodecContext*      avctx,
                              EFCodecEncContext_t* ctx,
                              AVFrame*             frame) {
    int                    ret           = 0;
    EFBuffer*              ef_buf_frame  = NULL;
    topscodecEncPicAttr_t* frame_attr    = NULL;
    topscodecFrame_t       ef_frame_tmp  = {0};
    topscodecFrame_t*      ef_frame_send = NULL;

    while (sem_wait(&ctx->import_frame_fifo_sem) == -1 && errno == EINTR)
        continue;

    ff_mutex_lock(&ctx->import_frame_fifo_mutex);
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
    av_fifo_read(ctx->import_frame_fifo, &ef_frame_tmp, 1);
#else
    av_fifo_generic_read(ctx->import_frame_fifo, &ef_frame_tmp, sizeof(topscodecFrame_t), NULL);
#endif
    ff_mutex_unlock(&ctx->import_frame_fifo_mutex);

    ef_buf_frame  = ctx->ef_buf_frame;
    ef_frame_send = &ef_buf_frame->ef_frame;
    *ef_frame_send = ef_frame_tmp;

    if (!ctx->draining && frame && frame->buf[0]) {
        ef_buf_frame->avctx          = avctx;
        ef_buf_frame->ef_enc_context = ctx;
        ret = ff_topscodec_avframe_to_efbuf(frame, ef_buf_frame);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "[%p]ff_topscodec_avframe_to_efbuf failed\n", ctx->handle);
            return ret;
        }
    }

    if (ctx->draining) {
        for (int i = 0; i < 4; i++) {
            ef_frame_send->plane[i].alloc_len = 0;
        }
        ef_frame_send->pixel_format = ctx->data_pix_fmt_topscodec;
        ef_frame_send->pts          = ctx->last_frame_pts + 1;
    }

    frame_attr = &ctx->frame_attr;
    memset(frame_attr, 0, sizeof(topscodecEncPicAttr_t));
    ret = topsenc_config_pic_attr(avctx, frame_attr);
    if (ret < 0) {
        return ret;
    }
    // print_tops_codecenc_pic_attr(avctx, frame_attr);

    ctx->last_frame_pts = ef_frame_send->pts;
    // print_frame(avctx, ef_frame_send, "ef_frame_send");

    ret = ctx->topscodec_lib_ctx->lib_topscodecEncEncodePicture(ctx->handle, ef_frame_send, frame_attr);
    if (ret != 0) {
        if (ret == TOPSCODEC_ERROR_TIMEOUT) {
            av_log(avctx, AV_LOG_WARNING, "[%p]topscodecEncEncodePicture timeout[%d], continue.\n", ctx->handle, ret);
        } else {
            av_log(avctx, AV_LOG_ERROR, "[%p]topscodecEncEncodePicture failed[%d]\n", ctx->handle, ret);
            return ret;
        }
    } else {
        if (avctx->max_b_frames > 0)
            topsenc_timestamp_queue_push(ctx->frame_timestamp_queue, ef_frame_send->pts);
        ctx->total_frame_count++;
        av_log(avctx, AV_LOG_DEBUG, "send total_frame_count:%d\n", ctx->total_frame_count);
    }

    return 0;
}

static int topsenc_output_packet(AVCodecContext* avctx, AVPacket* pkt) {
    EFCodecEncContext_t*   ctx        = (EFCodecEncContext_t*)avctx->priv_data;
    TopsRuntimesFunctions* topsruntimes = ctx->topsruntime_lib_ctx;
    topscodecStream_t      stream;
    AVPacket*              avpkt      = NULL;
    AVPacket*              merged_pkt = NULL;
    int                    ret        = 0;

    while (1) {
        ff_mutex_lock(&ctx->mid_avpacket_fifo_mutex);
        if ((int)av_fifo_size(ctx->mid_avpacket_fifo) <= 0) {
            if (atomic_load(&ctx->recv_outport_eos)) {
                ff_mutex_unlock(&ctx->mid_avpacket_fifo_mutex);
                return AVERROR_EOF;
            }
            ff_mutex_unlock(&ctx->mid_avpacket_fifo_mutex);
            return AVERROR(EAGAIN);
        }
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
        av_fifo_read(ctx->mid_avpacket_fifo, &stream, 1);
#else
        av_fifo_generic_read(ctx->mid_avpacket_fifo, &stream, sizeof(topscodecStream_t), NULL);
#endif
        ff_mutex_unlock(&ctx->mid_avpacket_fifo_mutex);

        if (!topsruntimes || !topsruntimes->lib_topsMemcpyDtoH) {
            av_log(avctx, AV_LOG_ERROR, "topsruntimes or lib_topsMemcpyDtoH is NULL\n");
            return AVERROR(EINVAL);
        }

        avpkt = av_packet_alloc();
        if (!avpkt) {
            av_log(avctx, AV_LOG_ERROR, "output_packet malloc avpkt fail\n");
            ctx->topscodec_lib_ctx->lib_topscodecEncUnlockBitstream(ctx->handle, &stream);
            return AVERROR(ENOMEM);
        }

        ret = av_new_packet(avpkt, stream.data_len);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "av_new_packet failed, size %u\n", stream.data_len);
            av_packet_free(&avpkt);
            ctx->topscodec_lib_ctx->lib_topscodecEncUnlockBitstream(ctx->handle, &stream);
            return ret;
        }

        ret = topsruntimes->lib_topsMemcpyDtoH((void*)avpkt->data, (void*)(stream.mem_addr + stream.data_offset), stream.data_len);
        if (ret != topsSuccess) {
            av_log(avctx, AV_LOG_ERROR, "topsMemcpyDtoH failed!\n");
            av_packet_free(&avpkt);
            ctx->topscodec_lib_ctx->lib_topscodecEncUnlockBitstream(ctx->handle, &stream);
            return AVERROR(EPERM);
        }

        ret = ctx->topscodec_lib_ctx->lib_topscodecEncUnlockBitstream(ctx->handle, &stream);
        if (ret != 0) {
            av_log(avctx, AV_LOG_ERROR, "topscodecEncUnlockBitstream failed. handle[%p], ret = %d\n", ctx->handle, ret);
            av_packet_free(&avpkt);
            return AVERROR_BUG;
        }

        avpkt->pts      = stream.pts;
        avpkt->duration = 0;

        if (stream.stream_type == TOPSCODEC_NALU_TYPE_IDR ||
            stream.stream_type == TOPSCODEC_NALU_TYPE_I)
            avpkt->flags |= AV_PKT_FLAG_KEY;

        if (stream.stream_type == TOPSCODEC_H264_NALU_TYPE_SPS_PPS ||
            stream.stream_type == TOPSCODEC_HEVC_NALU_TYPE_VPS_SPS_PPS) {
            if (ctx->spspps_pkt == NULL) {
                ctx->spspps_pkt = av_packet_clone(avpkt);
                if (!ctx->spspps_pkt) {
                    av_log(avctx, AV_LOG_WARNING, "Failed to clone SPS/PPS packet\n");
                } else {
                    av_log(avctx, AV_LOG_DEBUG, "Saved SPS/PPS packet, stream_type: %ld, size: %d\n", stream.stream_type, ctx->spspps_pkt->size);
                    av_packet_free(&avpkt);
                    continue;
                }
            }
        }

        if (ctx->first_iframe &&
            (stream.stream_type == TOPSCODEC_NALU_TYPE_IDR || stream.stream_type == TOPSCODEC_NALU_TYPE_I)) {
            ctx->first_iframe = 0;
            av_log(avctx, AV_LOG_DEBUG, "Got I-frame packet, stream_type: %ld, size: %d, keyframe: %d\n",
                   stream.stream_type, avpkt->size, (avpkt->flags & AV_PKT_FLAG_KEY) ? 1 : 0);
            if (ctx->spspps_pkt != NULL) {
                if (!ctx->spspps_pkt->data || ctx->spspps_pkt->size <= 0) {
                    av_log(avctx, AV_LOG_WARNING, "Invalid SPS/PPS packet data\n");
                    av_packet_free(&ctx->spspps_pkt);
                    ctx->spspps_pkt = NULL;
                } else if (!avpkt->data || avpkt->size <= 0) {
                    av_log(avctx, AV_LOG_WARNING, "Invalid I-frame packet data\n");
                } else {
                    merged_pkt = av_packet_alloc();
                    if (!merged_pkt) {
                        av_log(avctx, AV_LOG_WARNING, "Failed to allocate merged packet, using original I-frame.\n");
                    } else {
                        ret = av_new_packet(merged_pkt, ctx->spspps_pkt->size + avpkt->size);
                        if (ret < 0) {
                            av_log(avctx, AV_LOG_WARNING, "Failed to allocate merged packet data. ret:%d, using original I-frame\n", ret);
                            av_packet_free(&merged_pkt);
                            merged_pkt = NULL;
                        } else {
                            memcpy(merged_pkt->data, ctx->spspps_pkt->data, ctx->spspps_pkt->size);
                            memcpy(merged_pkt->data + ctx->spspps_pkt->size, avpkt->data, avpkt->size);
                            ret = av_packet_copy_props(merged_pkt, avpkt);
                            if (ret < 0) {
                                av_log(avctx, AV_LOG_WARNING, "Failed to copy packet properties. ret:%d, using manual copy\n", ret);
                                merged_pkt->pts          = avpkt->pts;
                                merged_pkt->dts          = avpkt->dts;
                                merged_pkt->duration     = avpkt->duration;
                                merged_pkt->flags        = avpkt->flags;
                                merged_pkt->stream_index = avpkt->stream_index;
                                merged_pkt->pos          = avpkt->pos;
                            }

                            av_log(avctx, AV_LOG_DEBUG, "Merged SPS/PPS(%d bytes) and I-frame(%d bytes) into %d bytes packet\n",
                                   ctx->spspps_pkt->size, avpkt->size, merged_pkt->size);
                            av_packet_free(&ctx->spspps_pkt);
                            ctx->spspps_pkt = NULL;

                            av_packet_free(&avpkt);
                            avpkt = merged_pkt;
                        }
                    }
                }
            } else {
                av_log(avctx, AV_LOG_WARNING, "No SPS/PPS packet to merge with I-frame\n");
            }
        }

        topsenc_set_packet_dts(ctx, avpkt);

        av_packet_ref(pkt, avpkt);
        av_packet_free(&avpkt);
        ctx->total_packet_count++;
        av_log(avctx, AV_LOG_DEBUG, "receive total_packet_count:%d\n", ctx->total_packet_count);
        return 0;
    }
}

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 18, 100)  // n4.x n7.0
static int ff_topscodec_receive_packet(AVCodecContext* avctx, AVPacket* pkt) {
    EFCodecEncContext_t* ctx;
    int                  ret       = 0;
    AVFrame*             frame     = NULL;

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
    uint8_t* t35_data = NULL;
    int      t35_size = 0;
#endif

    if (NULL == avctx || NULL == avctx->priv_data) {
        av_log(avctx, AV_LOG_ERROR, "Early error in topscodec_receive_packet\n");
        return AVERROR_BUG;
    }
    ctx = (EFCodecEncContext_t*)avctx->priv_data;

    if (!ctx->encoder_init_flag) {
        av_log(avctx, AV_LOG_ERROR, "Encoder not initialized or aborted, return AVERROR_BUG\n");
        return AVERROR_BUG;
    }

    if (ctx->draining) {
        goto dequeue;
    }

    frame = ctx->frame;
    if (!frame->buf[0]) {
        ret = ff_encode_get_frame(avctx, frame);
        if (ret < 0 && ret != AVERROR_EOF) return ret;

        if (ret == AVERROR_EOF) {
            ctx->draining = 1;
            av_log(avctx, AV_LOG_VERBOSE, "[%p]receive input frame EOF.\n", ctx->handle);
        } else {
            ret = av_image_fill_linesizes(frame->linesize, ctx->data_pix_fmt_av, frame->width);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "av_image_fill_linesizes failed.\n");
                return AVERROR_BUG;
            }
            for (int i = 0; i < 4; i++) {
                frame->linesize[i] = FFALIGN(frame->linesize[i], ctx->input_frame_stride_align);
            }
        }
    }

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
    ctx->hdr10plus_t35_buff_L3_data_size = 0;
    if (ctx->extra_sei) {
        ret = get_frame_t35_data(avctx, frame, &t35_data, &t35_size);
        if (ret != 0) {
            av_log(avctx, AV_LOG_ERROR, "get_frame_t35_data failed, ret(%d)\n", ret);
            return ret;
        }

        if (t35_data) {
            memset(ctx->hdr10plus_t35_buff_host_addr, 0, ctx->hdr10plus_t35_buff_alloc_size);
            memcpy(ctx->hdr10plus_t35_buff_host_addr, t35_data, t35_size);
            ctx->hdr10plus_t35_buff_L3_data_size = t35_size;
            ctx->sei_attr->sei_payload_size      = t35_size;
            av_log(avctx, AV_LOG_INFO,
                   "copy t35_data to ctx->hdr10plus_t35_buff_host_addr success, size: %d, "
                   "the first 6 bytes are %02x %02x %02x %02x %02x %02x\n",
                   t35_size, ctx->hdr10plus_t35_buff_host_addr[0],
                   ctx->hdr10plus_t35_buff_host_addr[1],
                   ctx->hdr10plus_t35_buff_host_addr[2],
                   ctx->hdr10plus_t35_buff_host_addr[3],
                   ctx->hdr10plus_t35_buff_host_addr[4],
                   ctx->hdr10plus_t35_buff_host_addr[5]);
        }
    }
#endif

    ret = topsenc_send_frame(avctx, ctx, frame);
    if (ret < 0)
        return ret;

dequeue:
    ret = topsenc_output_packet(avctx, pkt);
    if (ret == AVERROR(EAGAIN)) {
        if (ctx->draining) {
            ff_mutex_lock(&ctx->mid_avpacket_fifo_mutex);
            while ((int)av_fifo_size(ctx->mid_avpacket_fifo) == 0 &&
                   !atomic_load(&ctx->recv_outport_eos)) {
                pthread_cond_wait(&ctx->mid_avpacket_fifo_cond, &ctx->mid_avpacket_fifo_mutex);
            }
            ff_mutex_unlock(&ctx->mid_avpacket_fifo_mutex);
            av_log(avctx, AV_LOG_VERBOSE, "draining mode: repeating[%d], ret:%d, handle:%p\n",
                   ctx->draining, ret, ctx->handle);
            goto dequeue;
        } else {
            av_log(avctx, AV_LOG_VERBOSE, "not draining: returning EAGAIN, ret:%d\n", ret);
            return AVERROR(EAGAIN);
        }
    } else if (ret == AVERROR_EOF) {
        av_log(avctx, AV_LOG_VERBOSE, "topsenc_output_packet return AVERROR_EOF\n");
        return AVERROR_EOF;
    } else if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "topsenc_output_packet failed, ret:%d\n", ret);
        return ret;
    } else {
        return 0;
    }
}
#else
static int ff_topscodec_encode2(AVCodecContext* avctx, AVPacket* pkt,
                                AVFrame* frame, int* got_packet_ptr) {
    EFCodecEncContext_t* ctx;
    int                  ret = 0;

    if (NULL == avctx || NULL == avctx->priv_data) {
        av_log(avctx, AV_LOG_ERROR, "Early error in topscodec_encode2\n");
        return AVERROR_BUG;
    }
    ctx = (EFCodecEncContext_t*)avctx->priv_data;
    *got_packet_ptr = 0;

    if (!ctx->encoder_init_flag) {
        av_log(avctx, AV_LOG_ERROR, "Encoder not initialized or aborted, return AVERROR_BUG\n");
        return AVERROR_BUG;
    }

    if (ctx->draining) {
        goto dequeue;
    }

    if (!frame) {
        ctx->draining = 1;
        av_log(avctx, AV_LOG_VERBOSE, "[%p]receive input frame EOF.\n", ctx->handle);
    }

    if (frame) {
        int linesizes[4] = {0};
        ret = av_image_fill_linesizes(linesizes, ctx->data_pix_fmt_av, frame->width);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "av_image_fill_linesizes failed.\n");
            return ret;
        }
        for (int i = 0; i < 4; i++) {
            linesizes[i] = FFALIGN(linesizes[i], ctx->input_frame_stride_align);
            frame->linesize[i] = linesizes[i];
        }
    }

    ret = topsenc_send_frame(avctx, ctx, frame);
    if (ret < 0)
        return ret;

dequeue:
    ret = topsenc_output_packet(avctx, pkt);
    if (ret == AVERROR(EAGAIN)) {
        if (ctx->draining) {
            ff_mutex_lock(&ctx->mid_avpacket_fifo_mutex);
            while ((int)av_fifo_size(ctx->mid_avpacket_fifo) == 0 &&
                   !atomic_load(&ctx->recv_outport_eos)) {
                pthread_cond_wait(&ctx->mid_avpacket_fifo_cond, &ctx->mid_avpacket_fifo_mutex);
            }
            ff_mutex_unlock(&ctx->mid_avpacket_fifo_mutex);
            av_log(avctx, AV_LOG_VERBOSE, "draining mode: repeating[%d], ret:%d, handle:%p\n", ctx->draining, ret, ctx->handle);
            goto dequeue;
        } else {
            av_log(avctx, AV_LOG_VERBOSE, "not draining: returning EAGAIN, ret:%d\n", ret);
            return 0;
        }
    } else if (ret == AVERROR_EOF) {
        av_log(avctx, AV_LOG_VERBOSE, "encode2 eos, no more packets\n");
        return 0;
    } else if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "encode2 output_packet failed, ret:%d\n", ret);
        return ret;
    } else {
        *got_packet_ptr = 1;
        return 0;
    }
}
#endif

#define OFFSET(x) offsetof(EFCodecEncContext_t, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
#define DEFAULT 0

//clang-format off
#define OPTIONS_COMMON                                                      \
        {"card_id",                                                         \
         "use to choose the accelerator card",                              \
         OFFSET(card_id),                                                   \
         AV_OPT_TYPE_INT,                                                   \
         {.i64 = 0},                                                        \
         0,                                                                 \
         FF_TOPSCODEC_MAX_CARD_ID,                                          \
         VE},                                                               \
        {"device_id",                                                       \
         "use to choose the accelerator device",                            \
         OFFSET(device_id),                                                 \
         AV_OPT_TYPE_INT,                                                   \
         {.i64 = 0},                                                        \
         0,                                                                 \
         FF_TOPSCODEC_MAX_DEVICE_ID,                                        \
         VE},                                                               \
        {"balance",                                                         \
         "use to choose the balance mode",                                  \
         OFFSET(balance),                                                   \
         AV_OPT_TYPE_INT,                                                   \
         {.i64 = 0},                                                        \
         0,                                                                 \
         1,                                                                 \
         VE},                                                               \
        {"sf",                                                              \
         "Set the switch frame number",                                     \
         OFFSET(sf),                                                        \
         AV_OPT_TYPE_INT,                                                   \
         {.i64 = 1},                                                        \
         0,                                                                 \
         255,                                                               \
         VE},                                                               \
        {"qp_i",                                                            \
         "set qp i",                                                        \
         OFFSET(qp_i),                                                      \
         AV_OPT_TYPE_INT,                                                   \
         {.i64 = 23},                                                       \
         0,                                                                 \
         51,                                                                \
         VE},                                                               \
        {"qp_p",                                                            \
         "set qp p",                                                        \
         OFFSET(qp_p),                                                      \
         AV_OPT_TYPE_INT,                                                   \
         {.i64 = 25},                                                       \
         0,                                                                 \
         51,                                                                \
         VE},                                                               \
        {"qp_b",                                                            \
         "set qp b",                                                        \
         OFFSET(qp_b),                                                      \
         AV_OPT_TYPE_INT,                                                   \
         {.i64 = 27},                                                       \
         0,                                                                 \
         51,                                                                \
         VE},                                                               \
        {"init_qp_i",                                                       \
         "Set init QP for I frame while rc mode is not fixedQP",            \
         OFFSET(init_qp_i),                                                 \
         AV_OPT_TYPE_INT,                                                   \
         {.i64 = -1},                                                       \
         -1,                                                                \
         51,                                                                \
         VE},                                                               \
        {"init_qp_p",                                                       \
         "Set init QP for P frame while rc mode is not fixedQP",            \
         OFFSET(init_qp_p),                                                 \
         AV_OPT_TYPE_INT,                                                   \
         {.i64 = -1},                                                       \
         -1,                                                                \
         51,                                                                \
         VE},                                                               \
        {"init_qp_b",                                                       \
         "Set init QP for B frame while rc mode is not fixedQP",            \
         OFFSET(init_qp_b),                                                 \
         AV_OPT_TYPE_INT,                                                   \
         {.i64 = -1},                                                       \
         -1,                                                                \
         51,                                                                \
         VE},                                                               \
        {"enc_fps",                                                         \
         "set enc fps",                                                     \
         OFFSET(enc_fps),                                                   \
         AV_OPT_TYPE_INT,                                                   \
         {.i64 = 30},                                                       \
         0,                                                                 \
         UINT_MAX,                                                          \
         VE},                                                               \
        {"enc_gop_type",                                                    \
         "set enc gop type, 0-non, 1=bidirection, 2=low_delay, 3=pyramid, " \
         "4=svct3, 5=gdr",                                                  \
         OFFSET(enc_gop_type),                                              \
         AV_OPT_TYPE_INT,                                                   \
         {.i64 = 1},                                                        \
         0,                                                                 \
         UINT_MAX,                                                          \
         VE},                                                               \
        {"enc_rate_control",                                                \
         "set enc rate control,fixedqp=0, cbr=1,vbr=2,crf=3,cvbr=4",        \
         OFFSET(enc_rate_control),                                          \
         AV_OPT_TYPE_INT,                                                   \
         {.i64 = 1},                                                        \
         0,                                                                 \
         UINT_MAX,                                                          \
         VE},                                                               \
        {"conv_mode",                                                       \
         "set enc conv mode",                                               \
         OFFSET(conv_mode),                                                 \
         AV_OPT_TYPE_INT,                                                   \
         {.i64 = 0},                                                        \
         0,                                                                 \
         UINT_MAX,                                                          \
         VE},                                                               \
        {"out_port_num",                                                    \
         "set out port num",                                                \
         OFFSET(out_port_num),                                              \
         AV_OPT_TYPE_INT,                                                   \
         {.i64 = 8},                                                        \
         0,                                                                 \
         INT_MAX,                                                           \
         VE},                                                               \
        {"in_port_num",                                                     \
         "set in port num",                                                 \
         OFFSET(in_port_num),                                               \
         AV_OPT_TYPE_INT,                                                   \
         {.i64 = 8},                                                        \
         0,                                                                 \
         INT_MAX,                                                           \
         VE},                                                               \
        {"enable_rotation",                                                 \
         "Forces open the rotation, only support orientation,90/180/270",   \
         OFFSET(enable_rotation),                                           \
         AV_OPT_TYPE_BOOL,                                                  \
         {.i64 = 0},                                                        \
         -1,                                                                \
         1,                                                                 \
         VE},                                                               \
        {"rotation",                                                        \
         "setting rotation, only support orientation,90/180/270",           \
         OFFSET(rotation),                                                  \
         AV_OPT_TYPE_INT,                                                   \
         {.i64 = 0},                                                        \
         0,                                                                 \
         INT_MAX,                                                           \
         VE},                                                               \
        {"enable_crop",                                                     \
         "Forces open the crop",                                            \
         OFFSET(enable_crop),                                               \
         AV_OPT_TYPE_BOOL,                                                  \
         {.i64 = 0},                                                        \
         -1,                                                                \
         1,                                                                 \
         VE},                                                               \
        {"crop_top",                                                        \
         "out top (crop)",                                                  \
         OFFSET(crop.top),                                                  \
         AV_OPT_TYPE_INT,                                                   \
         {.i64 = 0},                                                        \
         0,                                                                 \
         INT_MAX,                                                           \
         VE},                                                               \
        {"crop_bottom",                                                     \
         "out bottom(crop)",                                                \
         OFFSET(crop.bottom),                                               \
         AV_OPT_TYPE_INT,                                                   \
         {.i64 = 0},                                                        \
         0,                                                                 \
         INT_MAX,                                                           \
         VE},                                                               \
        {"crop_left",                                                       \
         "out left(crop)",                                                  \
         OFFSET(crop.left),                                                 \
         AV_OPT_TYPE_INT,                                                   \
         {.i64 = 0},                                                        \
         0,                                                                 \
         INT_MAX,                                                           \
         VE},                                                               \
        {"crop_right",                                                      \
         "out right(crop)",                                                 \
         OFFSET(crop.right),                                                \
         AV_OPT_TYPE_INT,                                                   \
         {.i64 = 0},                                                        \
         0,                                                                 \
         INT_MAX,                                                           \
         VE},                                                               \
        {"enable_flip",                                                     \
         "Forces open the flip",                                            \
         OFFSET(enable_flip),                                               \
         AV_OPT_TYPE_BOOL,                                                  \
         {.i64 = 0},                                                        \
         -1,                                                                \
         1,                                                                 \
         VE},                                                               \
        {"flip_model",                                                      \
         "Set the flip model",                                              \
         OFFSET(flip_model),                                                \
         AV_OPT_TYPE_INT,                                                   \
         {.i64 = 0},                                                        \
         0,                                                                 \
         2,                                                                 \
         VE},                                                               \
        {"stride_align",                                                    \
         "stride align",                                                    \
         OFFSET(input_frame_stride_align),                                  \
         AV_OPT_TYPE_INT,                                                   \
         {.i64 = 0},                                                        \
         0,                                                                 \
         INT_MAX,                                                           \
         VE}

#define HDR_OPTIONS                                                           \
        {"hdr_matrix_range",                                                  \
         "0 - disabled, 1 - full range, 2 - limited range",                   \
         OFFSET(hdr_matrix_range),                                            \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = 0},                                                          \
         0,                                                                   \
         UINT_MAX,                                                            \
         VE},                                                                 \
        {"extra_sei",                                                         \
         "0 - disabled, 1 - enabled",                                         \
         OFFSET(extra_sei),                                                   \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = 0},                                                          \
         0,                                                                   \
         1,                                                                   \
         VE},                                                                 \
        {"hdr_flags",                                                         \
         "0-MASTERING_DISPLAY_DATA_VALID, 1-CONTENT_LIGHT_DATA_VALID",        \
         OFFSET(hdr_flags),                                                   \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = 1},                                                          \
         0,                                                                   \
         4,                                                                   \
         VE},                                                                 \
        {"hdr_mode",                                                          \
         "HDR: Set HDR mode, 0-none, 1-hdr10, 2-hdr10+, 3-dolbyvision",       \
         OFFSET(hdr_mode),                                                    \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = 0},                                                          \
         0,                                                                   \
         3,                                                                   \
         VE},                                                                 \
        {"hdr_matrix",                                                        \
         "HDR: Select CSC matrix standard.\n\t\t 0 - unspecified\n\t\t 1 - "  \
         "BT.709\n\t\t 2 - BT470M\n\t\t 3 - BT.601_625\n\t\t 4 - "            \
         "SMPTE240M\n\t\t 5 - BT.2020 non-const luma\n\t\t 6 - BT.2020 "      \
         "constant luma",                                                     \
         OFFSET(hdr_matrix),                                                  \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = 0},                                                          \
         0,                                                                   \
         UINT_MAX,                                                            \
         VE},                                                                 \
        {"hdr_primaries",                                                     \
         "HDR: Set CSC primaries range when HDR enabled.\n\t\t 0 - "          \
         "unspecified "                                                       \
         "\n\t\t 1 - BT709\n\t\t 2 - BT470M\n\t\t 3 - BT601_625\n\t\t 4 - "   \
         "BT601-525\n\t\t 5 - Generic Film\n\t\t 6 - BT2020",                 \
         OFFSET(hdr_primaries),                                               \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = 0},                                                          \
         0,                                                                   \
         UINT_MAX,                                                            \
         VE},                                                                 \
        {"hdr_transfer",                                                      \
         "HDR: Set transfer characteristics when HDR enabled.\n\t\t 0  - "    \
         "unspecified\n\t\t 1  - Linear\n\t\t 2  - sRGB\n\t\t 3  - "          \
         "SMPTE170M\n\t\t 4  - Gamma 2.2\n\t\t 5  - Gamma 2.8\n\t\t 6  - "    \
         "SMPTE ST 2084\n\t\t 7  - ARIB STD-B67 hybrid-log-gamma\n\t\t 8  - " \
         "SMPTE 240M\n\t\t 9  - IEC 61966-2-4\n\t\t 10 - Rec.ITU-R BT.1361 "  \
         "extended gamut.\n\t\t 11 - SMPTE ST 428-1",                         \
         OFFSET(hdr_transfer),                                                \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = 0},                                                          \
         0,                                                                   \
         UINT_MAX,                                                            \
         VE},                                                                 \
        {"hdr_display_r_x",                                                   \
         "HDR: Set coordinate of red primary.",                               \
         OFFSET(hdr_display_r_x),                                             \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = 0},                                                          \
         0,                                                                   \
         UINT_MAX,                                                            \
         VE},                                                                 \
        {"hdr_display_r_y",                                                   \
         "HDR: Set coordinate of red primary.",                               \
         OFFSET(hdr_display_r_y),                                             \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = 0},                                                          \
         0,                                                                   \
         UINT_MAX,                                                            \
         VE},                                                                 \
        {"hdr_display_g_x",                                                   \
         "HDR: Set coordinate of green primary.",                             \
         OFFSET(hdr_display_g_x),                                             \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = 0},                                                          \
         0,                                                                   \
         UINT_MAX,                                                            \
         VE},                                                                 \
        {"hdr_display_g_y",                                                   \
         "HDR: Set coordinate of green primary.",                             \
         OFFSET(hdr_display_g_y),                                             \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = 0},                                                          \
         0,                                                                   \
         UINT_MAX,                                                            \
         VE},                                                                 \
        {"hdr_display_b_x",                                                   \
         "HDR: Set coordinate of blue primary.",                              \
         OFFSET(hdr_display_b_x),                                             \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = 0},                                                          \
         0,                                                                   \
         UINT_MAX,                                                            \
         VE},                                                                 \
        {"hdr_display_b_y",                                                   \
         "HDR: Set coordinate of blue primary.",                              \
         OFFSET(hdr_display_b_y),                                             \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = 0},                                                          \
         0,                                                                   \
         UINT_MAX,                                                            \
         VE},                                                                 \
        {"hdr_display_w_x",                                                   \
         "HDR: Set coordinate of white primary.",                             \
         OFFSET(hdr_display_w_x),                                             \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = 0},                                                          \
         0,                                                                   \
         UINT_MAX,                                                            \
         VE},                                                                 \
        {"hdr_display_w_y",                                                   \
         "HDR: Set coordinate of white primary.",                             \
         OFFSET(hdr_display_w_y),                                             \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = 0},                                                          \
         0,                                                                   \
         UINT_MAX,                                                            \
         VE},                                                                 \
        {"hdr_display_lum_min",                                               \
         "HDR: Set display luminance minimum.",                               \
         OFFSET(hdr_display_lum_min),                                         \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = 0},                                                          \
         0,                                                                   \
         UINT_MAX,                                                            \
         VE},                                                                 \
        {"hdr_display_lum_max",                                               \
         "HDR: Set display luminance maximum.",                               \
         OFFSET(hdr_display_lum_max),                                         \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = 0},                                                          \
         0,                                                                   \
         UINT_MAX,                                                            \
         VE},                                                                 \
        {"hdr_content_lum_max",                                               \
         "HDR: Set content luminance minimum.",                               \
         OFFSET(hdr_content_lum_max),                                         \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = 0},                                                          \
         0,                                                                   \
         UINT_MAX,                                                            \
         VE},                                                                 \
        {"hdr_content_lum_avg",                                               \
         "HDR: Set content luminance average.",                               \
         OFFSET(hdr_content_lum_avg),                                         \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = 0},                                                          \
         0,                                                                   \
         UINT_MAX,                                                            \
         VE},                                                                 \
        {"hdr_aspect_ratio_idc",                                              \
         "HDR: Enable aspect ratio idc.",                                     \
         OFFSET(hdr_aspect_ratio_idc),                                        \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = 0},                                                          \
         0,                                                                   \
         UINT_MAX,                                                            \
         VE},                                                                 \
        {"hdr_sar_width",                                                     \
         "HDR: Set sar width.",                                               \
         OFFSET(hdr_sar_width),                                               \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = 0},                                                          \
         0,                                                                   \
         UINT_MAX,                                                            \
         VE},                                                                 \
        {"hdr_sar_height",                                                    \
         "HDR: Set sar height.",                                              \
         OFFSET(hdr_sar_height),                                              \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = 0},                                                          \
         0,                                                                   \
         UINT_MAX,                                                            \
         VE},                                                                 \
        {"hdr_num_units_in_tick",                                             \
         "HDR: Set units number in per tick.",                                \
         OFFSET(hdr_num_units_in_tick),                                       \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = 0},                                                          \
         0,                                                                   \
         UINT_MAX,                                                            \
         VE},                                                                 \
        {"hdr_time_scale",                                                    \
         "HDR: Set time scale.",                                              \
         OFFSET(hdr_time_scale),                                              \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = 0},                                                          \
         0,                                                                   \
         INT_MAX,                                                             \
         VE}

#define H264_PROFILE_OPTS                                                     \
        {"profile",                                                           \
         "Set the encoding profile",                                          \
         OFFSET(profile),                                                     \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = EFCODEC_ENC_PROFILE_AUTO_SELECT},                       \
         EFCODEC_ENC_PROFILE_AUTO_SELECT,                                \
         EFCODEC_ENC_PROFILE_H264_HIGH_10,                                    \
         VE,                                                                  \
         "profile"},                                                          \
        {"auto",                                                              \
         "Automatic profile selection",                                       \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_PROFILE_AUTO_SELECT},                       \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "profile"},                                                          \
        {"baseline",                                                          \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_PROFILE_H264_BASELINE},                          \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "profile"},                                                          \
        {"main",                                                              \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_PROFILE_H264_MAIN},                              \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "profile"},                                                          \
        {"high",                                                              \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_PROFILE_H264_HIGH},                              \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "profile"},                                                          \
        {"high10",                                                            \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_PROFILE_H264_HIGH_10},                           \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "profile"}

#define H264_LEVEL_OPTS                                                       \
        {"level",                                                             \
         "Set the encoding level restriction",                                \
         OFFSET(level),                                                       \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = EFCODEC_ENC_LEVEL_AUTO_SELECT},                              \
         EFCODEC_ENC_LEVEL_AUTO_SELECT,                                       \
         EFCODEC_ENC_LEVEL_H264_62,                                           \
         VE,                                                                  \
         "level"},                                                            \
        {"auto",                                                              \
         "Automatic level selection",                                         \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_AUTO_SELECT},                              \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"1",                                                                 \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_H264_1},                                   \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"1b",                                                                \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_H264_1B},                                  \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"1.1",                                                               \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_H264_11},                                  \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"1.2",                                                               \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_H264_12},                                  \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"1.3",                                                               \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_H264_13},                                  \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"2",                                                                 \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_H264_2},                                   \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"2.1",                                                               \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_H264_21},                                  \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"2.2",                                                               \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_H264_22},                                  \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"3",                                                                 \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_H264_3},                                   \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"3.1",                                                               \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_H264_31},                                  \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"3.2",                                                               \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_H264_32},                                  \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"4",                                                                 \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_H264_4},                                   \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"4.1",                                                               \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_H264_41},                                  \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"4.2",                                                               \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_H264_42},                                  \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"5",                                                                 \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_H264_5},                                   \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"5.1",                                                               \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_H264_51},                                  \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"5.2",                                                               \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_H264_52},                                  \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"6",                                                                 \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_H264_6},                                   \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"6.1",                                                               \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_H264_61},                                  \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"6.2",                                                               \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_H264_62},                                  \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"}

#define HEVC_PROFILE_OPTS                                                     \
        {"profile",                                                           \
         "Set the encoding profile",                                          \
         OFFSET(profile),                                                     \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = EFCODEC_ENC_PROFILE_AUTO_SELECT},                       \
         EFCODEC_ENC_PROFILE_AUTO_SELECT,                                \
         EFCODEC_ENC_PROFILE_HEVC_MAIN_10,                                    \
         VE,                                                                  \
         "profile"},                                                          \
        {"auto",                                                              \
         "Automatic profile selection",                                       \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_PROFILE_AUTO_SELECT},                       \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "profile"},                                                          \
        {"main",                                                              \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_PROFILE_HEVC_MAIN},                              \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "profile"},                                                          \
        {"main_still",                                                        \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_PROFILE_HEVC_MAIN_STILL},                        \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "profile"},                                                          \
        {"main_intra",                                                        \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_PROFILE_HEVC_MAIN_INTRA},                        \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "profile"},                                                          \
        {"main10",                                                            \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_PROFILE_HEVC_MAIN_10},                           \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "profile"}

#define HEVC_LEVEL_OPTS                                                       \
        {"level",                                                             \
         "Set the encoding level restriction",                                \
         OFFSET(level),                                                       \
         AV_OPT_TYPE_INT,                                                     \
         {.i64 = EFCODEC_ENC_LEVEL_AUTO_SELECT},                              \
         EFCODEC_ENC_LEVEL_AUTO_SELECT,                                       \
         EFCODEC_ENC_LEVEL_HEVC_62,                                           \
         VE,                                                                  \
         "level"},                                                            \
        {"auto",                                                              \
         "Automatic level selection",                                         \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_AUTO_SELECT},                              \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"1",                                                                 \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_HEVC_1},                                   \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"2",                                                                 \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_HEVC_2},                                   \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"2.1",                                                               \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_HEVC_21},                                  \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"3",                                                                 \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_HEVC_3},                                   \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"3.1",                                                               \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_HEVC_31},                                  \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"4",                                                                 \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_HEVC_4},                                   \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"4.1",                                                               \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_HEVC_41},                                  \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"5",                                                                 \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_HEVC_5},                                   \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"5.1",                                                               \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_HEVC_51},                                  \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"5.2",                                                               \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_HEVC_52},                                  \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"6",                                                                 \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_HEVC_6},                                   \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"6.1",                                                               \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_HEVC_61},                                  \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"},                                                            \
        {"6.2",                                                               \
         "",                                                                  \
         0,                                                                   \
         AV_OPT_TYPE_CONST,                                                   \
         {.i64 = EFCODEC_ENC_LEVEL_HEVC_62},                                  \
         0,                                                                   \
         0,                                                                   \
         VE,                                                                  \
         "level"}

// AVOption
#define DEFINE_ENCODER_OPTIONS_AND_CLASS(codec, codec_upper, ...)             \
    static const AVOption codec##_options[] = {                               \
        OPTIONS_COMMON, __VA_ARGS__ HDR_OPTIONS, {NULL}};                     \
                                                                              \
    static const AVClass codec##_topscodec_enc_class = {                      \
        .class_name = #codec "_topscodec_enc",                                \
        .item_name  = av_default_item_name,                                   \
        .option     = codec##_options,                                        \
        .version    = LIBAVUTIL_VERSION_INT,                                  \
    };

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
#define DEFINE_ENCODER_CODEC(codec, codec_upper, codec_id, long_name)     \
    const FFCodec ff_##codec##_topscodec_enc_encoder = {                  \
        .p.name = #codec "_topscodec_enc",                                \
        CODEC_LONG_NAME("TOPSCODEC " long_name " encoder"),               \
        .p.type = AVMEDIA_TYPE_VIDEO,                                     \
        .p.id   = AV_CODEC_ID_##codec_id,                                 \
        .init   = ff_topscodec_encode_init,                               \
        FF_CODEC_RECEIVE_PACKET_CB(ff_topscodec_receive_packet),          \
        .close          = ff_topscodec_encode_close,                      \
        .priv_data_size = sizeof(EFCodecEncContext_t),                    \
        .p.priv_class   = &codec##_topscodec_enc_class,                   \
        .defaults       = defaults,                                       \
        .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE |    \
                          AV_CODEC_CAP_DR1 |                              \
                          AV_CODEC_CAP_ENCODER_REORDERED_OPAQUE,          \
        .caps_internal =                                                  \
            FF_CODEC_CAP_NOT_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP, \
        .p.pix_fmts     = ff_topscodec_pix_fmts,                          \
        .color_ranges   = AVCOL_RANGE_MPEG | AVCOL_RANGE_JPEG,            \
        .p.wrapper_name = "topscodec_enc",                                \
        .hw_configs     = ff_topscodec_hw_configs,                        \
    };

#elif LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 18, 100)  // n4.x
#define DEFINE_ENCODER_CODEC(codec, codec_upper, codec_id, my_long_name) \
    const AVCodec ff_##codec##_topscodec_enc_encoder = {                 \
        .name           = #codec "_topscodec_enc",                       \
        .long_name      = my_long_name,                                  \
        .type           = AVMEDIA_TYPE_VIDEO,                            \
        .id             = AV_CODEC_ID_##codec_id,                        \
        .priv_data_size = sizeof(EFCodecEncContext_t),                   \
        .priv_class     = &codec##_topscodec_enc_class,                  \
        .init           = ff_topscodec_encode_init,                      \
        .receive_packet = ff_topscodec_receive_packet,                   \
        .close          = ff_topscodec_encode_close,                     \
        .defaults       = defaults,                                      \
        .capabilities   = AV_CODEC_CAP_DELAY,                            \
        .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,                     \
        .pix_fmts       = ff_topscodec_pix_fmts,                         \
        .wrapper_name   = "topscodec_enc",                               \
        .hw_configs     = ff_topscodec_hw_configs,                       \
    };
#else  // n3.x
#define DEFINE_ENCODER_CODEC(codec, codec_upper, codec_id, my_long_name) \
    AVCodec ff_##codec##_topscodec_enc_encoder = {                       \
        .name           = #codec "_topscodec_enc",                       \
        .long_name      = my_long_name,                                  \
        .type           = AVMEDIA_TYPE_VIDEO,                            \
        .id             = AV_CODEC_ID_##codec_id,                        \
        .priv_data_size = sizeof(EFCodecEncContext_t),                   \
        .priv_class     = &codec##_topscodec_enc_class,                  \
        .init           = ff_topscodec_encode_init,                      \
        .encode2        = ff_topscodec_encode2,                          \
        .close          = ff_topscodec_encode_close,                     \
        .defaults       = defaults,                                      \
        .capabilities   = AV_CODEC_CAP_DELAY,                            \
        .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,                     \
        .pix_fmts       = ff_topscodec_pix_fmts,                         \
    };
#endif

// h264
#if CONFIG_H264_TOPSCODEC_ENC_ENCODER
DEFINE_ENCODER_OPTIONS_AND_CLASS(h264, H264,
    H264_PROFILE_OPTS, H264_LEVEL_OPTS, )
DEFINE_ENCODER_CODEC(h264, H264, H264, "H.264")
#endif

// hevc
#if CONFIG_HEVC_TOPSCODEC_ENC_ENCODER
DEFINE_ENCODER_OPTIONS_AND_CLASS(hevc, HEVC,
    HEVC_PROFILE_OPTS, HEVC_LEVEL_OPTS, )
DEFINE_ENCODER_CODEC(hevc, HEVC, HEVC, "HEVC")
#endif

// vp8
#if CONFIG_VP8_TOPSCODEC_ENC_ENCODER
DEFINE_ENCODER_OPTIONS_AND_CLASS(vp8, VP8)
DEFINE_ENCODER_CODEC(vp8, VP8, VP8, "VP8")
#endif

// vp9
#if CONFIG_VP9_TOPSCODEC_ENC_ENCODER
DEFINE_ENCODER_OPTIONS_AND_CLASS(vp9, VP9)
DEFINE_ENCODER_CODEC(vp9, VP9, VP9, "VP9")
#endif

// mjpeg
#if CONFIG_MJPEG_TOPSCODEC_ENC_ENCODER
DEFINE_ENCODER_OPTIONS_AND_CLASS(mjpeg, MJPEG)
DEFINE_ENCODER_CODEC(mjpeg, MJPEG, MJPEG, "MJPEG")
#endif
