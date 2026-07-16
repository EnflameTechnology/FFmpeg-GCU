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
#include <semaphore.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

// #include "config.h" //0821
#include "libavcodec/version.h"
#include "libavutil/buffer.h"
#include "libavutil/fifo.h"
#include "libavutil/hwcontext.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"
#include "libavutil/thread.h"
#include "libavutil/time.h"
#include "libavutil/version.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/ff_topscodec_buffers.h"
#include "libavcodec/ff_topscodec_dec.h"
#include "libavcodec/ff_topscodec_utils.h"
#include "libavcodec/internal.h"
#include "libavutil/hwcontext_topscodec.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(59, 27, 100)  // 5.1
#include "config_components.h"                             // NOLINT
#include "libavcodec/codec_internal.h"
#endif

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 18, 100)  // 4.0
#include "libavcodec/decode.h"                             //3.2 is not support
#include "libavcodec/hwconfig.h"                           //3.2 is not support
#endif

#define FF_IDR_MAGIC (16384)
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(59, 18, 100)  // n4.x
static const enum AVPixelFormat ff_topscodec_pix_fmts[] = {
    AV_PIX_FMT_TOPSCODEC,   AV_PIX_FMT_YUV420P,  AV_PIX_FMT_NV12,
    AV_PIX_FMT_NV21,        AV_PIX_FMT_RGB24,    AV_PIX_FMT_RGB24P,
    AV_PIX_FMT_BGR24,       AV_PIX_FMT_BGR24P,   AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV444P10LE, AV_PIX_FMT_P010LE,   AV_PIX_FMT_P010LE_LSB,
    AV_PIX_FMT_GRAY8,       AV_PIX_FMT_GRAY10LE, AV_PIX_FMT_NONE};
#else
static const enum AVPixelFormat ff_topscodec_pix_fmts[] = {
    AV_PIX_FMT_TOPSCODEC,   AV_PIX_FMT_YUV420P, AV_PIX_FMT_NV12,
    AV_PIX_FMT_NV21,        AV_PIX_FMT_RGB24,   AV_PIX_FMT_RGB24P,
    AV_PIX_FMT_BGR24,       AV_PIX_FMT_BGR24P,  AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV444P10LE, AV_PIX_FMT_P010LE,  AV_PIX_FMT_P010LE_LSB,
    AV_PIX_FMT_GRAY8,       AV_PIX_FMT_NONE};
#endif

static int check_pix_fmt_support(enum AVPixelFormat pix_fmt) {
    for (int i = 0; i < FF_ARRAY_ELEMS(ff_topscodec_pix_fmts); i++) {
        if (ff_topscodec_pix_fmts[i] == pix_fmt) {
            return 1;
        }
    }
    return 0;
}

static topscodecColorSpace_t str_2_topsolorspace(char* str) {
    topscodecColorSpace_t ret = TOPSCODEC_COLOR_SPACE_BT_601;
    if (!strcmp(str, "bt601")) {
        ret = TOPSCODEC_COLOR_SPACE_BT_601;
    } else if (!strcmp(str, "bt601f")) {
        ret = TOPSCODEC_COLOR_SPACE_BT_601_ER;
    } else if (!strcmp(str, "bt709")) {
        ret = TOPSCODEC_COLOR_SPACE_BT_709;
    } else if (!strcmp(str, "bt709f")) {
        ret = TOPSCODEC_COLOR_SPACE_BT_709_ER;
    } else if (!strcmp(str, "bt2020")) {
        ret = TOPSCODEC_COLOR_SPACE_BT_2020;
    } else if (!strcmp(str, "bt2020f")) {
        ret = TOPSCODEC_COLOR_SPACE_BT_2020_ER;
    }
    return ret;
}

static void print_caps(AVCodecContext* avctx, topscodecDecCaps_t* DecCaps) {
    av_log(avctx, AV_LOG_DEBUG, "topscodecDecGetCaps success {.\n");
    av_log(avctx, AV_LOG_DEBUG, "Caps supported(%d)           \t\n",
           DecCaps->supported);
    av_log(avctx, AV_LOG_DEBUG, "max_width(%d)                \t\n",
           DecCaps->max_width);
    av_log(avctx, AV_LOG_DEBUG, "max_height(%d)               \t\n",
           DecCaps->max_height);
    av_log(avctx, AV_LOG_DEBUG, "min_width(%d)                \t\n",
           DecCaps->min_width);
    av_log(avctx, AV_LOG_DEBUG, "min_height(%d)               \t\n",
           DecCaps->min_height);
    av_log(avctx, AV_LOG_DEBUG, "output_pixel_format_mask(%d) \t\n",
           DecCaps->output_pixel_format_mask);
    av_log(avctx, AV_LOG_DEBUG, "scale_up_supported(%d)       \t\n",
           DecCaps->scale_up_supported);
    av_log(avctx, AV_LOG_DEBUG, "rotation_supported(%d)       \t\n",
           DecCaps->rotation_supported);
    av_log(avctx, AV_LOG_DEBUG, "crop_supported(%d)           \t\n",
           DecCaps->crop_supported);
    av_log(avctx, AV_LOG_DEBUG, "}                            \t\n");
}

static void print_create_info(AVCodecContext*           avctx,
                              topscodecDecCreateInfo_t* create_info) {
    av_log(avctx, AV_LOG_DEBUG, "topscodecDecCreateInfo_t info {.\n");
    av_log(avctx, AV_LOG_DEBUG, "card_id(%d)                  \t\n",
           create_info->card_id);
    av_log(avctx, AV_LOG_DEBUG, "device_id(%d)                \t\n",
           create_info->vcu_id);
    av_log(avctx, AV_LOG_DEBUG, "hw_ctx_id(%d)                \t\n",
           create_info->hw_ctx_id);
    av_log(avctx, AV_LOG_DEBUG, "sw_ctx_id(%d)                \t\n",
           create_info->sw_ctx_id);
    av_log(avctx, AV_LOG_DEBUG, "codec(%d)                    \t\n",
           create_info->codec);
    av_log(avctx, AV_LOG_DEBUG, "callback(%p)                 \t\n",
           create_info->callback);
    av_log(avctx, AV_LOG_DEBUG, "\t buf_size(%d)              \t\n",
           create_info->stream_buf_size);
    av_log(avctx, AV_LOG_DEBUG, "\t run_mode(%s)              \t\n",
           create_info->run_mode ? "TOPSCODEC_RUN_MODE_SYNC"
                                 : "TOPSCODEC_RUN_MODE_ASYNC");
    av_log(avctx, AV_LOG_DEBUG, "\t             }\t\n");
}

static const char* get_output_order_str(
    topscodecDecOutputOrder_t output_order) {
    switch (output_order) {
        case TOPSCODEC_DEC_OUTPUT_ORDER_DISPLAY:
            return "TOPSCODEC_DEC_OUTPUT_ORDER_DISPLAY";
        case TOPSCODEC_DEC_OUTPUT_ORDER_DECODE:
            return "TOPSCODEC_DEC_OUTPUT_ORDER_DECODE";
        default:
            return "UNKNOWN";
    }
}

static void print_param(AVCodecContext* avctx, topscodecDecParams_t* param) {
    av_log(avctx, AV_LOG_DEBUG, "topscodecDecParams_t info {           \n");
    av_log(avctx, AV_LOG_DEBUG, "\t max_width(%d)                      \n",
           param->max_width);
    av_log(avctx, AV_LOG_DEBUG, "\t max_height(%d)                     \n",
           param->max_height);
    av_log(avctx, AV_LOG_DEBUG, "\t stride_align(%d)                   \n",
           param->stride_align);
    for (size_t i = 0; i < 32; i++) {
        av_log(avctx, AV_LOG_DEBUG, "\t reserved[%ld](%d)                \n", i,
               param->reserved[i]);
    }
    av_log(avctx, AV_LOG_DEBUG, "\t input_buf_num,reserved[4](%d)      \n",
           param->reserved[4]);
    av_log(avctx, AV_LOG_DEBUG, "\t output_buf_num(%d)                 \n",
           param->output_buf_num);
    av_log(avctx, AV_LOG_DEBUG, "\t mem_channel(%d)                    \n",
           param->mem_channel);
    av_log(avctx, AV_LOG_DEBUG, "\t pixel_format(%d)                   \n",
           param->pixel_format);
    av_log(avctx, AV_LOG_DEBUG, "\t color_space(%d)                    \n",
           param->color_space);
    av_log(avctx, AV_LOG_DEBUG, "\t dec_mode(%d)                       \n",
           param->dec_mode);
    av_log(avctx, AV_LOG_DEBUG, "\t output_order(%s)                   \n",
           get_output_order_str(param->output_order));
    av_log(avctx, AV_LOG_DEBUG, "\t pp_attr.downscale.enable(%d)       \n",
           param->pp_attr.downscale.enable);
    av_log(avctx, AV_LOG_DEBUG, "\t pp_attr.downscale.width(%d)        \n",
           param->pp_attr.downscale.width);
    av_log(avctx, AV_LOG_DEBUG, "\t pp_attr.downscale.height(%d)       \n",
           param->pp_attr.downscale.height);
    av_log(avctx, AV_LOG_DEBUG, "\t pp_attr.downscale.interDslMode(%d) \n",
           param->pp_attr.downscale.interDslMode);
    av_log(avctx, AV_LOG_DEBUG, "\t pp_attr.crop.enable(%d)            \n",
           param->pp_attr.crop.enable);
    av_log(avctx, AV_LOG_DEBUG, "\t pp_attr.crop.tl_x(%d)              \n",
           param->pp_attr.crop.tl_x);
    av_log(avctx, AV_LOG_DEBUG, "\t pp_attr.crop.tl_y(%d)              \n",
           param->pp_attr.crop.tl_y);
    av_log(avctx, AV_LOG_DEBUG, "\t pp_attr.crop.br_x(%d)              \n",
           param->pp_attr.crop.br_x);
    av_log(avctx, AV_LOG_DEBUG, "\t pp_attr.crop.br_y(%d)              \n",
           param->pp_attr.crop.br_y);
    av_log(avctx, AV_LOG_DEBUG, "\t pp_attr.rotation.enable(%d)        \n",
           param->pp_attr.rotation.enable);
    av_log(avctx, AV_LOG_DEBUG, "\t pp_attr.rotation.rotation(%d)      \n",
           param->pp_attr.rotation.rotation);
    av_log(avctx, AV_LOG_DEBUG, "\t pp_attr.sf.enable(%d)              \n",
           param->pp_attr.sf.enable);
    av_log(avctx, AV_LOG_DEBUG, "\t pp_attr.sf.sfo(%d)                 \n",
           param->pp_attr.sf.sfo);
    av_log(avctx, AV_LOG_DEBUG, "\t pp_attr.sf.sf_idr(%d)              \n",
           param->pp_attr.sf.sf_idr);
    av_log(avctx, AV_LOG_DEBUG, "\t                             }      \n");
}

static void sleep_wait(int* sleep_handle) {
    if (*sleep_handle > 10) {
        av_usleep(10 * 100);
    } else if (*sleep_handle > 7) {
        av_usleep(5 * 100);
    } else if (*sleep_handle > 5) {
        av_usleep(2 * 100);
    } else if (*sleep_handle > 3) {
        av_usleep(100);
    } else {
        av_usleep(10);
    }
    if (*sleep_handle > 15) {
        *sleep_handle = 0;
    } else {
        (*sleep_handle)++;
    }
}

static i32_t topsdec_process_event_new_frame(AVCodecContext* avctx, EFCodecDecContext_t* ctx, topscodecFrame_t* frame) {
    ff_mutex_lock(&ctx->frame_fifo_mutex);
    if (atomic_load(&ctx->close_flag) == 1) {
        av_log(avctx, AV_LOG_VERBOSE, "[%p]close flag is 1, callback do not process event:%s\n",
               ctx->handle, get_event_type_string(TOPSCODEC_EVENT_NEW_FRAME));
        ff_mutex_unlock(&ctx->frame_fifo_mutex);
        return TOPSCODEC_SUCCESS;
    }
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
    if (av_fifo_can_write(ctx->frame_fifo) < 1) {
        av_fifo_grow2(ctx->frame_fifo, MAX_FRAME_NUM);
    }
    av_fifo_write(ctx->frame_fifo, frame, 1);
#else
    if (av_fifo_space(ctx->frame_fifo) < (int)sizeof(topscodecFrame_t)) {
        av_fifo_grow(ctx->frame_fifo, MAX_FRAME_NUM * sizeof(topscodecFrame_t));
    }
    av_fifo_generic_write(ctx->frame_fifo, frame, sizeof(topscodecFrame_t), NULL);
#endif
    av_log(avctx, AV_LOG_DEBUG, "[%p] received NEW_FRAME event. frame_fifo size:%d.\n", ctx->handle, (int)av_fifo_size(ctx->frame_fifo));
    pthread_cond_signal(&ctx->frame_fifo_cond);
    ff_mutex_unlock(&ctx->frame_fifo_mutex);

    ctx->total_frame_count++;
    av_log(avctx, AV_LOG_DEBUG, "[%p][async] received total_frame_count:%ld\n", ctx->handle, ctx->total_frame_count);

    return TOPSCODEC_SUCCESS;
}

static i32_t topsdec_process_event_bitstream_processed(AVCodecContext* avctx, EFCodecDecContext_t* ctx) {
    av_log(avctx, AV_LOG_DEBUG, "[%p] received BITSTREAM_PROCESSED event.\n", ctx->handle);
    sem_post(&ctx->send_avpacket_sem);
    return TOPSCODEC_SUCCESS;
}

static i32_t topsdec_process_event_eos(AVCodecContext* avctx, EFCodecDecContext_t* ctx) {
    av_log(avctx, AV_LOG_DEBUG, "[%p] received EOS event.\n", ctx->handle);
    ff_mutex_lock(&ctx->frame_fifo_mutex);
    atomic_store(&ctx->eos_event_flag, 1);
    pthread_cond_signal(&ctx->frame_fifo_cond);
    ff_mutex_unlock(&ctx->frame_fifo_mutex);
    return TOPSCODEC_SUCCESS;
}

static i32_t topsdec_event_callback(topscodecHandle_t handle, topscodecEventType_t event, void* event_data, void* user_data) {
    int                  ret   = TOPSCODEC_SUCCESS;
    AVCodecContext*      avctx = (AVCodecContext*)user_data;
    EFCodecDecContext_t* ctx   = NULL;

    if (avctx == NULL && event_data == NULL) {
        av_log(NULL, AV_LOG_FATAL, "Decoder callback error, event type:%d\n", event);
        return TOPSCODEC_ERROR_INVALID_HANDLE;
    }

    ctx   = (EFCodecDecContext_t*)(avctx->priv_data);
    av_log(avctx, AV_LOG_DEBUG,
           "[%p]Got codec callback event %s, user_data %p, close_flag:%d\n",
           ctx->handle, get_event_type_string(event), user_data, atomic_load(&ctx->close_flag));

    switch (event) {
        case TOPSCODEC_EVENT_NEW_FRAME:
            return topsdec_process_event_new_frame(avctx, ctx, (topscodecFrame_t*)event_data);
        case TOPSCODEC_EVENT_BITSTREAM_PROCESSED:
            return topsdec_process_event_bitstream_processed(avctx, ctx);
        case TOPSCODEC_EVENT_EOS:
            return topsdec_process_event_eos(avctx, ctx);
        case TOPSCODEC_EVENT_SEQUENCE:
        case TOPSCODEC_EVENT_FRAME_PROCESSED:
        case TOPSCODEC_EVENT_OUT_OF_MEMORY:
        case TOPSCODEC_EVENT_STREAM_CORRUPT:
        case TOPSCODEC_EVENT_STREAM_NOT_SUPPORTED:
        case TOPSCODEC_EVENT_BUFFER_OVERFLOW:
        case TOPSCODEC_EVENT_FATAL_ERROR:
            av_log(avctx, AV_LOG_WARNING, "[%p]Got callback event: %s\n", ctx->handle, get_event_type_string(event));
            break;
        default:
            av_log(avctx, AV_LOG_ERROR, "[%p]Got unknown codec callback event %d\n", ctx->handle, event);
            ret = TOPSCODEC_ERROR_UNKNOWN;
            break;
    }
    return ret;
}

/*
   DYN_DEBUG_LEVEL_DISABLE = 0
   DYN_DEBUG_LEVEL_ERR     = 1
   DYN_DEBUG_LEVEL_INFO    = 2
   DYN_DEBUG_LEVEL_DEBUG   = 3
*/
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

static topscodecType_t get_codec_type(AVCodecContext* avctx) {
    topscodecType_t ret = TOPSCODEC_NUM_CODECS;
    switch (avctx->codec->id) {
#if CONFIG_H263_TOPSCODEC_DECODER
        case AV_CODEC_ID_H263:
            ret = TOPSCODEC_H263;
            break;
#endif
#if CONFIG_H264_TOPSCODEC_DECODER
        case AV_CODEC_ID_H264:
            ret = TOPSCODEC_H264;
            break;
#endif
#if CONFIG_HEVC_TOPSCODEC_DECODER
        case AV_CODEC_ID_HEVC:
            ret = TOPSCODEC_HEVC;
            break;
#endif
#if CONFIG_MJPEG_TOPSCODEC_DECODER
        case AV_CODEC_ID_MJPEG:
            ret = TOPSCODEC_JPEG;
            break;
#endif
#if CONFIG_MPEG2_TOPSCODEC_DECODER
        case AV_CODEC_ID_MPEG2VIDEO:
            ret = TOPSCODEC_MPEG2;
            break;
#endif
#if CONFIG_MPEG4_TOPSCODEC_DECODER
        case AV_CODEC_ID_MPEG4:
            ret = TOPSCODEC_MPEG4;
            break;
#endif
#if CONFIG_VC1_TOPSCODEC_DECODER
        case AV_CODEC_ID_VC1:
            ret = TOPSCODEC_VC1;
            break;
#endif
#if CONFIG_VP8_TOPSCODEC_DECODER
        case AV_CODEC_ID_VP8:
            ret = TOPSCODEC_VP8;
            break;
#endif
#if CONFIG_VP9_TOPSCODEC_DECODER
        case AV_CODEC_ID_VP9:
            ret = TOPSCODEC_VP9;
            break;
#endif
#if CONFIG_AVS_TOPSCODEC_DECODER
        case AV_CODEC_ID_CAVS:
            ret = TOPSCODEC_AVS;
            break;
#endif
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 18, 100)
#if CONFIG_AVS2_TOPSCODEC_DECODER
        case AV_CODEC_ID_AVS2:
            ret = TOPSCODEC_AVS2;
            break;
#endif
#if CONFIG_AV1_TOPSCODEC_DECODER
        case AV_CODEC_ID_AV1:
            ret = TOPSCODEC_AV1;
            break;
#endif
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

static int topscodec_decode_init_internal(AVCodecContext* avctx) {
    EFCodecDecContext_t*    ctx          = NULL;
    AVHWFramesContext*      hwframe_ctx  = NULL;
    AVHWDeviceContext*      device_ctx   = NULL;
    TOPSCodecDeviceContext* device_hwctx = NULL;

    topscodecDecCreateInfo_t codec_info = {0};
    topscodecDecParams_t     params     = {0};

    char card_idx[sizeof(int)] = {0};
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 18, 100)
    AVBSFContext* bsf = NULL;
#endif

    int ret                   = 0;
    int need_init_hwframe_ctx = 0;
    int bitstream_size        = 0;
    int probed_width          = 0;
    int probed_height         = 0;
    int max_width             = 0;
    int max_height            = 0;
    int rotation_tmp          = 0;

    enum AVPixelFormat pix_fmts[3];
    avctx->codec_type = AVMEDIA_TYPE_VIDEO;

    if (NULL == avctx || NULL == avctx->priv_data) {
        av_log(avctx, AV_LOG_ERROR, "Early error in topscodec_decode_init func.\n");
        return AVERROR_BUG;
    }

    ctx = avctx->priv_data;

    ff_ffmpeg_gcu_print_version();

    if (ctx->decoder_init_flag == 1) {
        av_log(avctx, AV_LOG_ERROR, "Error, cndecode double init. \n");
        return AVERROR_BUG;
    }

    debug_log_set(avctx);
    ctx->codec_type = get_codec_type(avctx);

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 18, 100)
    ctx->bsf = NULL;
    if (avctx->codec->id == AV_CODEC_ID_H264 ||
        avctx->codec->id == AV_CODEC_ID_HEVC) {
        if (avctx->codec->id == AV_CODEC_ID_H264)
            bsf = av_bsf_get_by_name("h264_mp4toannexb");
        else
            bsf = av_bsf_get_by_name("hevc_mp4toannexb");
        if (!bsf) {
            ret = AVERROR_BSF_NOT_FOUND;
            goto error;
        }
        if (ret = av_bsf_alloc(bsf, &ctx->bsf)) {
            goto error;
        }

        if (((ret = avcodec_parameters_from_context(ctx->bsf->par_in, avctx)) <
             0) ||
            ((ret = av_bsf_init(ctx->bsf)) < 0)) {
            av_bsf_free(&ctx->bsf);
            goto error;
        }
    }
#endif
    ctx->av_pkt = av_packet_alloc();

    avctx->pix_fmt = AV_PIX_FMT_TOPSCODEC;
    ctx->output_pixfmt = av_get_pix_fmt(ctx->str_output_pixfmt);
    if (!check_pix_fmt_support(ctx->output_pixfmt)) {
        av_log(avctx, AV_LOG_ERROR, "Invalid output pixel format: %s\n",
               av_get_pix_fmt_name(ctx->output_pixfmt));
        return AVERROR(EINVAL);
    }
    av_log(avctx, AV_LOG_DEBUG,
           "topscodec_decode_init output_pixfmt set by user is:%s\n",
           av_get_pix_fmt_name(ctx->output_pixfmt));

    pix_fmts[0] = AV_PIX_FMT_TOPSCODEC;
    pix_fmts[1] = ctx->output_pixfmt;
    pix_fmts[2] = AV_PIX_FMT_NONE;
    ret         = ff_get_format(avctx, pix_fmts);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "ff_get_format failed: %d\n", ret);
        return ret;
    }
    avctx->pix_fmt = ret;
    av_log(avctx, AV_LOG_DEBUG,
           "topscodec_decode_init avctx->pix_fmt:%s\n",
           av_get_pix_fmt_name(avctx->pix_fmt));
    // Never meet this if condition.
    if (avctx->sw_pix_fmt != ctx->output_pixfmt) {
        av_log(avctx, AV_LOG_WARNING,
               "topscodec_decode_init "
               "avctx->sw_pix_fmt from ff_get_format is:%s. "
               "User get_format() callback has wrong sw_pix_fmt.\n",
               av_get_pix_fmt_name(avctx->sw_pix_fmt));
        avctx->sw_pix_fmt = ctx->output_pixfmt;
    }
    av_log(avctx, AV_LOG_DEBUG,
           "topscodec_decode_init avctx->sw_pix_fmt:%s\n",
           av_get_pix_fmt_name(avctx->sw_pix_fmt));

    snprintf(card_idx, sizeof(int), "%d", ctx->card_id);
    if (avctx->hw_frames_ctx) {  // if hw_frames_ctx set by user
        av_buffer_unref(&ctx->hwframe);
        ctx->hwframe = av_buffer_ref(avctx->hw_frames_ctx);
        if (!ctx->hwframe) {
            ret = AVERROR(EINVAL);
            goto error;
        }

        hwframe_ctx = (AVHWFramesContext*)ctx->hwframe->data;
        hwframe_ctx->device_ctx =
            (AVHWDeviceContext*)hwframe_ctx->device_ref->data;
        ctx->hwdevice = av_buffer_ref(hwframe_ctx->device_ref);
        if (!ctx->hwdevice) {
            av_log(avctx, AV_LOG_ERROR,
                   "A hardware frames or device context is"
                   "required for hardware accelerated decoding.\n");
            ret = AVERROR(EINVAL);
            goto error;
        }
    } else {
        ret = av_hwdevice_ctx_create(&ctx->hwdevice, AV_HWDEVICE_TYPE_TOPSCODEC,
                                     card_idx, NULL, 0);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "Hardware device context create failed,ret(%d).\n", ret);
            goto error;
        }

        ctx->hwframe = av_hwframe_ctx_alloc(ctx->hwdevice);
        if (!ctx->hwframe) {
            av_log(avctx, AV_LOG_ERROR,
                   "Error, av_hwframe_ctx_alloc failed.\n");

            ret = AVERROR(EINVAL);
            goto error;
        }
        hwframe_ctx           = (AVHWFramesContext*)ctx->hwframe->data;
        need_init_hwframe_ctx = 1;
        avctx->hw_frames_ctx  = av_buffer_ref(ctx->hwframe);
    }

    ret = topscodec_load_functions(&ctx->topscodec_lib_ctx);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Error, topscodec_lib_load failed, ret(%d)\n", ret);
        ret = AVERROR(EINVAL);
        goto error;
    }

    memset(&ctx->caps, 0, sizeof(ctx->caps));
    if (ctx->card_id == 0) {
        ctx->card_id = get_card_id_from_env();
    }

    if (ctx->device_id == 0) {
        ctx->device_id = get_device_id_from_env();
    }
    av_log(avctx, AV_LOG_DEBUG, "balance: %d\n", ctx->balance);

    if (ctx->balance == 1) {
        av_log(avctx, AV_LOG_DEBUG, "set balance mode\n");
        ret = ctx->topscodec_lib_ctx->lib_topscodecSetVideoCoreBalancingPolicy(TOPSCODEC_VIDEO_CORE_BALANCING_LOADING);
        if (TOPSCODEC_SUCCESS != ret) {
            av_log(avctx, AV_LOG_ERROR,
                   "Error, topscodecSetVideoCoreBalancingPolicy failed,"
                   "ret(%d)\n",
                   ret);
            ret = AVERROR(EINVAL);
            goto error;
        }
        av_log(avctx, AV_LOG_DEBUG, "topscodecSetVideoCoreBalancingPolicy success.\n");
    }

    /*get device caps*/
    av_log(avctx, AV_LOG_DEBUG,
           "topscodecDecGetCaps: type[%d],card[%d]dev[%d]\n", ctx->codec_type,
           ctx->card_id, ctx->device_id);
    ret = ctx->topscodec_lib_ctx->lib_topscodecDecGetCaps(ctx->codec_type, ctx->card_id, ctx->device_id, &ctx->caps);
    if (TOPSCODEC_SUCCESS != ret) {
        av_log(avctx, AV_LOG_ERROR, "Error, topscodecDecGetCaps failed, ret(%d)\n", ret);
        ret = AVERROR(EINVAL);
        goto error;
    }

    print_caps(avctx, &ctx->caps);

    if (!ctx->caps.supported) {
        av_log(avctx, AV_LOG_ERROR, "Invalid topscodec supported.\n");
        return AVERROR_BUG;
    }

    max_width  = ctx->caps.max_width;
    max_height = ctx->caps.max_height;

    probed_width = avctx->coded_width ? avctx->coded_width
                                      : (avctx->width ? avctx->width : 0);
    probed_height = avctx->coded_height ? avctx->coded_height
                                        : (avctx->height ? avctx->height : 0);

    if (ctx->out_width <= 0 || ctx->out_height <= 0) {
        ctx->out_width  = probed_width;
        ctx->out_height = probed_height;
    }
    // if the user set the input width and height, use it
    if (ctx->in_width <= 0 || ctx->in_height <= 0) {
        ctx->in_width  = probed_width;
        ctx->in_height = probed_height;
    } else {
        probed_height = ctx->in_height;
        probed_width  = ctx->in_width;
    }
    av_log(avctx, AV_LOG_DEBUG, "Input dim:(%dx%d)\n", ctx->in_width, ctx->in_height);

    avctx->coded_width  = probed_width;
    avctx->coded_height = probed_height;
    avctx->width        = probed_width;
    avctx->height       = probed_height;
    if (probed_width > max_width || probed_height > max_height) {
        av_log(avctx, AV_LOG_ERROR,
               "Invalid dim,[AVCodecContext.width:%d ,"
               "AVCodecContext.height:%d],supported %d X %d \n",
               avctx->coded_width, avctx->coded_height, max_width, max_height);
        return AVERROR(EINVAL);
    }

    if (ctx->enable_crop) {
        av_log(avctx, AV_LOG_DEBUG, "Open crop options.\n");
        if (ctx->crop.top < 0 || ctx->crop.bottom < 0 || ctx->crop.left < 0 ||
            ctx->crop.right < 0 || ctx->crop.top > avctx->height ||
            ctx->crop.left > avctx->width || ctx->crop.bottom > avctx->height ||
            ctx->crop.right > avctx->width ||
            ctx->crop.top >= ctx->crop.bottom ||
            ctx->crop.left >= ctx->crop.right ||
            ctx->crop.bottom - ctx->crop.top < 8 ||
            ctx->crop.right - ctx->crop.left < 8) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid crop "
                   "dim(lefg:%d,top:%d)(right:%d,bottom:%d)\n",
                   ctx->crop.left, ctx->crop.top, ctx->crop.right,
                   ctx->crop.bottom);
            return AVERROR(EINVAL);
        }
        ctx->out_width  = ctx->crop.right - ctx->crop.left;
        ctx->out_height = ctx->crop.bottom - ctx->crop.top;
    }

    if (ctx->enable_resize) {
        av_log(avctx, AV_LOG_DEBUG, "Open downscale option.\n");
        if (ctx->resize.height < 0 || ctx->resize.width < 0 ||
            ctx->resize.height > avctx->height ||
            ctx->resize.width > avctx->width) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid resize dim %dx%d, only support downscale.\n",
                   ctx->resize.width, ctx->resize.height);
            return AVERROR(EINVAL);
        }
        ctx->out_width  = ctx->resize.width;
        ctx->out_height = ctx->resize.height;
    }

    if (ctx->enable_rotation) {
        if (ctx->rotation == 90 || ctx->rotation == 180 ||
            ctx->rotation == 270) {
            av_log(avctx, AV_LOG_DEBUG, "Open rotation option:%d.\n",
                   ctx->rotation);
        } else {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid rotation value, only support 90/180/270\n");
            return AVERROR(EINVAL);
        }

        if (ctx->rotation == 90 || ctx->rotation == 270) {
            rotation_tmp    = ctx->out_width;
            ctx->out_width  = ctx->out_height;
            ctx->out_height = rotation_tmp;
        }
    }

    // stride align
    if (ctx->stride_align) {
        // The value is power of 2 in the range [1, 2048]
        if (ctx->stride_align > 2048 || ctx->stride_align < 1) {
            av_log(avctx, AV_LOG_ERROR, "stride alignment must be even\n");
            return AVERROR(EINVAL);
        }
        if ((ctx->stride_align & (ctx->stride_align - 1)) != 0) {
            av_log(avctx, AV_LOG_ERROR, "stride alignment must be power of 2\n");
            return AVERROR(EINVAL);
        }
    }

    device_ctx                 = hwframe_ctx->device_ctx;
    device_hwctx               = device_ctx->hwctx;
    device_hwctx->stride_align = ctx->stride_align;
    ctx->topsruntime_lib_ctx   = device_hwctx->topsruntime_lib_ctx;

    // after getting the final output width and height, init hwframe
    if (need_init_hwframe_ctx && !hwframe_ctx->pool) {
        hwframe_ctx->format    = AV_PIX_FMT_TOPSCODEC;
        hwframe_ctx->sw_format = avctx->sw_pix_fmt;
        hwframe_ctx->width     = ctx->out_width;
        hwframe_ctx->height    = ctx->out_height;
        if (hwframe_ctx->width > 0 && hwframe_ctx->width > 0) {
            hwframe_ctx->initial_pool_size = 3;    /*TODO*/
            hwframe_ctx->pool              = NULL; /*TODO*/
            if ((ret = av_hwframe_ctx_init(ctx->hwframe)) < 0) {
                av_log(avctx, AV_LOG_ERROR, "Error, av_hwframe_ctx_init failed, ret(%d)\n", ret);
                ret = AVERROR(EINVAL);
                goto error;
            }
            av_log(avctx, AV_LOG_DEBUG, "hw frame init 1 success.\n");
        }
    }

    ctx->total_frame_count  = 0;
    ctx->total_packet_count = 0;
    ctx->draining           = 0;
    atomic_store(&ctx->eos_event_flag, 0);
    atomic_store(&ctx->close_flag, 0);
    ctx->first_packet       = 1;

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
    ctx->mid_avframe_fifo       = av_fifo_alloc2(MAX_FRAME_NUM, sizeof(AVFrame*), 0);
    ctx->frame_fifo             = av_fifo_alloc2(MAX_FRAME_NUM, sizeof(topscodecFrame_t), 0);
#else
    ctx->mid_avframe_fifo       = av_fifo_alloc(MAX_FRAME_NUM * sizeof(AVFrame*));
    ctx->frame_fifo             = av_fifo_alloc(MAX_FRAME_NUM * sizeof(topscodecFrame_t));
#endif

    if (!ctx->callback) {
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
        ctx->pkt_prop_fifo = av_fifo_alloc2(MAX_FRAME_NUM, sizeof(AVFrame*), 0);
#else
        ctx->pkt_prop_fifo = av_fifo_alloc(MAX_FRAME_NUM * sizeof(AVFrame*));
#endif
        ret = ff_mutex_init(&ctx->pkt_prop_mutex, NULL);
        if (ret != 0) {
            av_log(avctx, AV_LOG_ERROR, "init pkt_prop_mutex fail, ret(%d)\n", ret);
            ret = AVERROR(EINVAL);
            goto error;
        }
    }

    ret = ff_mutex_init(&ctx->frame_fifo_mutex, NULL);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR, "init frame_fifo_mutex fail, ret(%d)\n", ret);
        ret = AVERROR(EINVAL);
        goto error;
    }

    if (ctx->callback) {
        ret = sem_init(&ctx->send_avpacket_sem, 0, 0);
        if (ret != 0) {
            av_log(avctx, AV_LOG_ERROR, "init send_avpacket_sem fail, ret(%d)\n", ret);
            ret = AVERROR(EINVAL);
            goto error;
        }

        ret = pthread_cond_init(&ctx->frame_fifo_cond, NULL);
        if (ret != 0) {
            av_log(avctx, AV_LOG_ERROR, "init frame_fifo_cond fail, ret(%d)\n", ret);
            ret = AVERROR(EINVAL);
            goto error;
        }
    }

    bitstream_size = ceil((probed_width * probed_height) * 1.25);
    if (bitstream_size == 0) bitstream_size = 10 * 1024 * 1024;

    ctx->ef_buf_pkt = av_mallocz(sizeof(EFBuffer));
    memset(ctx->ef_buf_pkt, 0, sizeof(EFBuffer));
    ctx->ef_buf_pkt->type                  = EF_BUFFER_TYPE_PKT;
    ctx->ef_buf_pkt->avctx                 = avctx;
    ctx->ef_buf_pkt->ef_dec_context        = ctx;
    ctx->ef_buf_pkt->ef_pkt.mem_addr       = 0;
    ctx->ef_buf_pkt->ef_pkt.alloc_len      = 0;
    ctx->ef_buf_pkt->ef_frame_pkt_buf_size = bitstream_size;
    ret = ff_topscodec_alloc_efbuf_internal_data(ctx->ef_buf_pkt);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR, "Error, alloc_efbuf_internal_data failed, ret(%d)\n", ret);
        ret = AVERROR(EINVAL);
        goto error;
    }

    memset(&codec_info, 0, sizeof(topscodecDecCreateInfo_t));
    codec_info.card_id    = ctx->card_id;
    codec_info.vcu_id     = ctx->device_id;
    codec_info.hw_ctx_id  = ctx->hw_id;
    codec_info.codec      = ctx->codec_type;
    codec_info.stream_buf_size = ctx->ef_buf_pkt->ef_frame_pkt_buf_size_aligned_4k;
    if (ctx->callback) {
        codec_info.hw_ctx_id    = 0x0F;
        codec_info.sw_ctx_id    = 0x08;
        codec_info.run_mode     = TOPSCODEC_RUN_MODE_ASYNC;
        codec_info.callback     = topsdec_event_callback;
        codec_info.user_context = (u64_t)avctx;
        av_log(avctx, AV_LOG_DEBUG, "run in async mode\n");
    } else {
        codec_info.run_mode     = TOPSCODEC_RUN_MODE_SYNC;
        codec_info.callback     = NULL;
        codec_info.user_context = 0;
        av_log(avctx, AV_LOG_DEBUG, "run in sync mode\n");
    }
    av_log(avctx, AV_LOG_DEBUG, "zero copy %d\n", ctx->zero_copy);

    if (codec_info.codec == TOPSCODEC_VP8 ||
        codec_info.codec == TOPSCODEC_VP9 ||
        codec_info.codec == TOPSCODEC_AV1) {
        codec_info.send_mode = TOPSCODEC_DEC_SEND_MODE_FRAME;
    } else {
        codec_info.send_mode = TOPSCODEC_DEC_SEND_MODE_STREAM;
    }

    codec_info.reserved[9]  = 1;
    codec_info.reserved[10] = ctx->sf; // switch frame num, set by user.
    av_log(avctx, AV_LOG_DEBUG, "switch frame number:%d\n", ctx->sf);

    /* ap log setting*/
    if (get_ap_log_on_off_from_env()) {
        codec_info.reserved[0] = 1;
        codec_info.reserved[1] = 1;
        /* Log level */
        for (int i = 0; i < 7; i++) {
            codec_info.reserved[i + 2] = 5;
        }
        av_log(avctx, AV_LOG_DEBUG, "run in ap log mode\n");
    }

    /*create codec*/
    print_create_info(avctx, &codec_info);
    ret = ctx->topscodec_lib_ctx->lib_topscodecDecCreate(&ctx->handle, &codec_info);
    if (TOPSCODEC_SUCCESS != ret) {
        av_log(avctx, AV_LOG_ERROR, "Error, topscodecDecCreate failed, ret(%d)\n", ret);
        ret = AVERROR(EINVAL);
        goto error;
    }
    av_log(avctx, AV_LOG_DEBUG, "topscodecDecCreate successful, handle:0x%p\n", ctx->handle);

    memset(&params, 0, sizeof(topscodecDecParams_t));
    params.pixel_format = avpixfmt_2_topspixfmt(ctx->output_pixfmt);
    av_log(avctx, AV_LOG_DEBUG, "Out pixfmt: (%d)%s\n", params.pixel_format, av_pix_fmt_desc_get(ctx->output_pixfmt)->name);

    params.color_space = str_2_topsolorspace(ctx->color_space);
    av_log(avctx, AV_LOG_DEBUG, "Out Colorspace: %s\n", ctx->color_space);

    params.reserved[4] = ctx->input_buf_num;
    if (params.reserved[4] < 8) params.reserved[4] = 8;
    av_log(avctx, AV_LOG_DEBUG, "input_buf_num: %d\n", ctx->input_buf_num);

    params.output_buf_num = ctx->output_buf_num;
    if (params.output_buf_num < 8) params.output_buf_num = 8;
    av_log(avctx, AV_LOG_DEBUG, "output_buf_num: %d\n", ctx->output_buf_num);

    if (ctx->enable_crop && ctx->enable_rotation) {
        av_log(avctx, AV_LOG_ERROR,
               "Set Parameter error, Rotation and Crop "
               "can not be set at the same time. \n");
        ret = AVERROR(EINVAL);
        goto error;
    }

    if (ctx->enable_crop && ctx->enable_resize) {
        av_log(avctx, AV_LOG_ERROR,
               "Set Parameter error, Downscale resize "
               "and Crop can not be set at the same time. \n");
        ret = AVERROR(EINVAL);
        goto error;
    }

    if (ctx->enable_sfo && (ctx->sfo * ctx->sf_idr != 0)) {
        av_log(avctx, AV_LOG_ERROR,
               "Set Parameter error, Frame sampling"
               " interval and IDR Frame sampling can not be set at the same "
               "time.\n");
        ret = AVERROR(EINVAL);
        goto error;
    }

    if (ctx->enable_resize) {
        params.pp_attr.downscale.enable = 1;
        params.pp_attr.downscale.width  = ctx->resize.width;
        params.pp_attr.downscale.height = ctx->resize.height;
        /*!< Downscale mode: 0-Bilinear, 1-Nearest*/
        params.pp_attr.downscale.interDslMode = ctx->resize.mode;
        av_log(avctx, AV_LOG_DEBUG, "Setting resize, %dx%d->%dx%d.\n",
               avctx->width, avctx->height, ctx->resize.width,
               ctx->resize.height);
    }

    /* Set Crop Parameter */
    if (ctx->enable_crop) {
        params.pp_attr.crop.enable = 1;
        params.pp_attr.crop.tl_x   = ctx->crop.left;
        params.pp_attr.crop.tl_y   = ctx->crop.top;
        params.pp_attr.crop.br_x   = ctx->crop.right;
        params.pp_attr.crop.br_y   = ctx->crop.bottom;
        av_log(avctx, AV_LOG_DEBUG,
               "Setting crop,src dim:(%dx%d),crop dim:"
               "((left,top)(right,bottom)):"
               "((%dx%d),(%dx%d))\n",
               avctx->width, avctx->height, ctx->crop.left, ctx->crop.top,
               ctx->crop.right, ctx->crop.bottom);
    }

    /* Set Rotation Parameter */
    if (ctx->enable_rotation) {
        params.pp_attr.rotation.enable = 1;
        switch (ctx->rotation) {
            case 90:
                params.pp_attr.rotation.rotation = TOPSCODEC_ROTATION_90;
                break;
            case 180:
                params.pp_attr.rotation.rotation = TOPSCODEC_ROTATION_180;
                break;
            case 270:
                params.pp_attr.rotation.rotation = TOPSCODEC_ROTATION_270;
                break;
            default:
                break;
        }
        av_log(avctx, AV_LOG_DEBUG, "Setting rotation, rotation:%d\n",
               ctx->rotation);
    }

    params.max_width  = ctx->out_width;
    params.max_height = ctx->out_height;

    if (ctx->enable_sfo) {
        params.pp_attr.sf.enable = 1;
        if (ctx->sfo != 0)
            params.pp_attr.sf.sfo = ctx->sfo;
        else if (ctx->sf_idr != 0)
            params.pp_attr.sf.sf_idr = FF_IDR_MAGIC;

        av_log(avctx, AV_LOG_DEBUG,
               "Setting sampling interval value, sfo:%d,sf_idr:%d\n", ctx->sfo,
               FF_IDR_MAGIC);
    }

    // set stride align
    if (ctx->stride_align != 0) {
        params.stride_align = ctx->stride_align;
        if (params.stride_align > 1) {
            /* for mjpeg, enable stride and disable
            plane compat; for other format, fw will ignore it. */
            params.reserved[1] = 2;
        }
        av_log(avctx, AV_LOG_DEBUG, "stride_align :%d, reserved[1]:%d\n", ctx->stride_align, params.reserved[1]);
    }

    /*set codec params*/
    print_param(avctx, &params);
    ret = ctx->topscodec_lib_ctx->lib_topscodecDecSetParams(ctx->handle, &params);
    if (TOPSCODEC_SUCCESS != ret) {
        av_log(avctx, AV_LOG_ERROR, "Error, topscodecDecSetParams failed, ret(%d)\n", ret);
        ret = AVERROR(EINVAL);
        goto error;
    }
    av_log(avctx, AV_LOG_DEBUG, "topscodecDecSetParams success\n");

    if (!avctx->pkt_timebase.num || !avctx->pkt_timebase.den)
        av_log(avctx, AV_LOG_DEBUG, "Invalid pkt_timebase, passing timestamps as-is.\n");

    ctx->decoder_init_flag = 1;
    av_log(avctx, AV_LOG_DEBUG, "Thread: %lu, decoder init done\n", (uint64_t)pthread_self());
    return 0;

error:
    return ret;
}

static av_cold int topscodec_decode_init(AVCodecContext* avctx) {
    return topscodec_decode_init_internal(avctx);
}

static int topscodec_decode_close_internal(AVCodecContext* avctx) {
    EFCodecDecContext_t* ctx;
    if (NULL == avctx || NULL == avctx->priv_data) {
        return AVERROR_BUG;
    }
    ctx = (EFCodecDecContext_t*)avctx->priv_data;
    atomic_store(&ctx->close_flag, 1);

    if (ctx->callback) {
        sem_post(&ctx->send_avpacket_sem);

        ff_mutex_lock(&ctx->frame_fifo_mutex);
        pthread_cond_signal(&ctx->frame_fifo_cond);
        ff_mutex_unlock(&ctx->frame_fifo_mutex);
    }

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 18, 100)
    if (ctx->bsf) av_bsf_free(&ctx->bsf);
#endif
    if (ctx->pkt_prop_fifo) {
        while ((int)av_fifo_size(ctx->pkt_prop_fifo) > 0) {
            AVFrame* avframe_tmp;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
            av_fifo_read(ctx->pkt_prop_fifo, &avframe_tmp, 1);
#else
            av_fifo_generic_read(ctx->pkt_prop_fifo, &avframe_tmp, sizeof(AVFrame*), NULL);
#endif
            av_log(avctx, AV_LOG_DEBUG, "close pkt_prop_fifo [%p] Get frame ,size:%d\n", avframe_tmp, (int)av_fifo_size(ctx->pkt_prop_fifo));
            av_frame_unref(avframe_tmp);
            av_frame_free(&avframe_tmp);
        }
        av_fifo_freep(&ctx->pkt_prop_fifo);
    }

    if (ctx->pkt_prop_frame) {
        av_frame_unref(ctx->pkt_prop_frame);
        av_frame_free(&ctx->pkt_prop_frame);
    }

    if (ctx->mid_avframe_fifo) {
        while ((int)av_fifo_size(ctx->mid_avframe_fifo) > 0) {
            AVFrame* avframe_tmp;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
            av_fifo_read(ctx->mid_avframe_fifo, &avframe_tmp, 1);
#else
            av_fifo_generic_read(ctx->mid_avframe_fifo, &avframe_tmp, sizeof(AVFrame*), NULL);
#endif
            av_log(avctx, AV_LOG_DEBUG, "close mid_avframe_fifo [%p] Get frame ,size:%d\n", avframe_tmp, (int)av_fifo_size(ctx->mid_avframe_fifo));
            av_frame_unref(avframe_tmp);
            av_frame_free(&avframe_tmp);
        }
        av_fifo_freep(&ctx->mid_avframe_fifo);
    }

    if (ctx->frame_fifo) {
        while ((int)av_fifo_size(ctx->frame_fifo) > 0) {
            topscodecFrame_t frame_tmp;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
            av_fifo_read(ctx->frame_fifo, &frame_tmp, 1);
#else
            av_fifo_generic_read(ctx->frame_fifo, &frame_tmp, sizeof(topscodecFrame_t), NULL);
#endif
            av_log(avctx, AV_LOG_DEBUG, "close frame_fifo, unmap frame, size:%d\n",
                   (int)av_fifo_size(ctx->frame_fifo));
            if (ctx->topscodec_lib_ctx) {
                ctx->topscodec_lib_ctx->lib_topscodecDecFrameUnmap(ctx->handle, &frame_tmp);
            }
        }
        av_fifo_freep(&ctx->frame_fifo);
    }

    if (ctx->handle) {
        ctx->topscodec_lib_ctx->lib_topscodecDecDestroy(ctx->handle);
        ctx->handle = 0;
        av_log(avctx, AV_LOG_DEBUG, "topscodecDecDestroy success\n");
    }

    if (ctx->av_pkt) av_packet_free(&ctx->av_pkt);

    if (!ctx->callback) {
        ff_mutex_destroy(&ctx->pkt_prop_mutex);
    }
    ff_mutex_destroy(&ctx->frame_fifo_mutex);

    if (ctx->callback) {
        sem_destroy(&ctx->send_avpacket_sem);
        pthread_cond_destroy(&ctx->frame_fifo_cond);
    }

    if (ctx->ef_buf_pkt) {
        ff_topscodec_free_efbuf_internal_data(ctx->ef_buf_pkt);
        av_freep(&ctx->ef_buf_pkt);
        av_log(avctx, AV_LOG_DEBUG, "ef_buf_pkt free\n");
    }

    if (ctx->topscodec_lib_ctx) {
        topscodec_free_functions(&ctx->topscodec_lib_ctx);
        av_log(avctx, AV_LOG_DEBUG, "topscodec_free_functions success\n");
    }

    if (ctx->hwdevice) {
        av_buffer_unref(&ctx->hwdevice);
        av_log(avctx, AV_LOG_DEBUG, "hwdevice unref\n");
    }

    if (ctx->hwframe) {
        av_buffer_unref(&ctx->hwframe);
        av_log(avctx, AV_LOG_DEBUG, "hwframe unref\n");
    }

    ctx->decoder_init_flag = 0;

    av_log(avctx, AV_LOG_DEBUG, "Thread, %lu, decode close \n", (uint64_t)pthread_self());
    return 0;
}

static av_cold int topscodec_decode_close(AVCodecContext* avctx) {
    return topscodec_decode_close_internal(avctx);
}

static void topsdec_set_frame_props(AVCodecContext* avctx,
                                    AVFrame* avframe,
                                    const topscodecFrame_t* ef_frame) {
    EFCodecDecContext_t* ctx = avctx->priv_data;

    if (ctx->pkt_prop_frame)
        av_frame_copy_props(avframe, ctx->pkt_prop_frame);

    avframe->pict_type = tops_2_av_pic_type(ef_frame->pic_type);
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)
    if (tops_is_key_frame(ef_frame->pic_type))
        avframe->flags = AV_FRAME_FLAG_KEY;
    else
        avframe->flags = 0;
#else
    avframe->key_frame = tops_is_key_frame(ef_frame->pic_type);
#endif
    avframe->pts = ef_frame->pts;
    av_log(avctx, AV_LOG_DEBUG,
           "[%p]frame info: pic_type=%d, key_frame=%d, pts=%lu\n",
           ctx->handle, ef_frame->pic_type,
           tops_is_key_frame(ef_frame->pic_type), avframe->pts);

    if (!ctx->enable_crop && !ctx->enable_resize) {
        avctx->coded_height = ef_frame->height;
        avctx->coded_width  = ef_frame->width;
    }
}

static int topsdec_efbuf_to_avframe_d2d_async(EFBuffer* efbuf, AVFrame* avframe) {
    int                    ret          = 0;
    AVCodecContext*        avctx        = NULL;
    EFCodecDecContext_t*   ctx          = NULL;
    AVHWFramesContext*     hw_frame_ctx = NULL;
    TopsRuntimesFunctions* topsruntime  = NULL;
    TopsCodecFunctions*    topscodec    = NULL;
    enum AVPixelFormat     avframe_format;

    ptrdiff_t linesizes1[4] = {0};
    size_t    planesizes[4] = {0};
    uint8_t*  data[4]       = {NULL};

    avctx        = efbuf->avctx;
    ctx          = avctx->priv_data;
    topscodec    = ctx->topscodec_lib_ctx;
    topsruntime  = ctx->topsruntime_lib_ctx;
    hw_frame_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;

    avframe->height = efbuf->ef_frame.height;
    avframe->width  = efbuf->ef_frame.width;
    avframe_format  = topspixfmt_2_avpixfmt(efbuf->ef_frame.pixel_format);

    if (hw_frame_ctx->height <= 0 || hw_frame_ctx->width <= 0) {
        hw_frame_ctx->height = efbuf->ef_frame.height;
        hw_frame_ctx->width  = efbuf->ef_frame.width;

        hw_frame_ctx->initial_pool_size = 3;
        hw_frame_ctx->pool              = NULL;
        if ((ret = av_hwframe_ctx_init(ctx->hwframe)) < 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "[%p]av_hwframe_ctx_init failed, ret(%d)\n",
                   ctx->handle, ret);
            goto fail_unmap;
        }
        av_log(avctx, AV_LOG_DEBUG, "[%p]hw frame init 2 success\n", ctx->handle);
    }

    for (int i = 0; i < 4; i++) {
        linesizes1[i] = efbuf->ef_frame.plane[i].stride;
        data[i]       = (uint8_t*)efbuf->ef_frame.plane[i].dev_addr;
    }
    av_log(avctx, AV_LOG_TRACE,
           "[%p]linesizes: [%ld, %ld, %ld, %ld]\n",
           ctx->handle, linesizes1[0], linesizes1[1], linesizes1[2], linesizes1[3]);

    ret = av_image_fill_plane_sizes(planesizes, avframe_format, avframe->height, linesizes1);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "[%p]av_image_fill_plane_sizes failed\n", ctx->handle);
        goto fail_unmap;
    }

    if (av_pix_fmt_count_planes(avframe_format) != efbuf->ef_frame.plane_num) {
        av_log(avctx, AV_LOG_ERROR,
               "[%p]plane count mismatch, pix:%s, efbuf plane[%d], ffmpeg[%d]\n",
               ctx->handle, av_get_pix_fmt_name(avframe_format),
               efbuf->ef_frame.plane_num,
               av_pix_fmt_count_planes(avframe_format));
        goto fail_unmap;
    }

    if (ctx->zero_copy) {
        /* zero-copy: wrap decoder output buffers directly into AVFrame */
        for (int i = 0; i < efbuf->ef_frame.plane_num; i++) {
            ret = ff_topscodec_buf_to_bufref(efbuf, i, &avframe->buf[i], planesizes[i]);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "[%p]ff_topscodec_buf_to_bufref failed, plane[%d]\n", ctx->handle, i);
                av_frame_unref(avframe);
                return ret;
            }
            avframe->linesize[i] = efbuf->ef_frame.plane[i].stride;
            avframe->data[i]     = avframe->buf[i]->data;
        }
        avframe->hw_frames_ctx = av_buffer_ref(avctx->hw_frames_ctx);
    } else {
        /* D2D copy: allocate new HW buffer and copy decoded data into it */
        ret = av_hwframe_get_buffer(avctx->hw_frames_ctx, avframe, 0);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "[%p]av_hwframe_get_buffer failed, ret(%d)\n", ctx->handle, ret);
            goto fail_unmap;
        }

        for (int i = 0; i < efbuf->ef_frame.plane_num; i++) {
            avframe->linesize[i] = efbuf->ef_frame.plane[i].stride;

            if (planesizes[i] == 0) {
                av_log(avctx, AV_LOG_ERROR, "[%p]planesizes[%d] is zero\n", ctx->handle, i);
                continue;
            }
            ret = topsruntime->lib_topsMemcpyDtoD(avframe->data[i], data[i], planesizes[i]);
            if (ret != topsSuccess) {
                av_log(avctx, AV_LOG_ERROR,
                    "[%p]topsMemcpyDtoD failed, plane[%d]: dev %p -> dev %p, size %lu\n",
                    ctx->handle, i, data[i], (void*)avframe->data[i], planesizes[i]);
                goto fail_unmap;
            }
            av_log(avctx, AV_LOG_TRACE,
                "[%p]d2d[%d]: dev %p -> dev %p, size %lu\n",
                ctx->handle, i, data[i], avframe->data[i], planesizes[i]);
        }

        ret = topscodec->lib_topscodecDecFrameUnmap(ctx->handle, &efbuf->ef_frame);
        if (ret != 0) {
            av_log(avctx, AV_LOG_ERROR, "[%p]topscodecDecFrameUnmap failed\n", ctx->handle);
            av_frame_unref(avframe);
            return AVERROR_BUG;
        }
    }

    topsdec_set_frame_props(avctx, avframe, &efbuf->ef_frame);
    avframe->format = AV_PIX_FMT_TOPSCODEC;

    return 0;

fail_unmap:
    topscodec->lib_topscodecDecFrameUnmap(ctx->handle, &efbuf->ef_frame);
    av_frame_unref(avframe);
    return AVERROR_BUG;
}

static int topsdec_efbuf_to_avframe_d2h_async(EFBuffer* efbuf, AVFrame* avframe) {
    AVCodecContext*        avctx       = efbuf->avctx;
    EFCodecDecContext_t*   ctx         = avctx->priv_data;
    TopsRuntimesFunctions* topsruntime = ctx->topsruntime_lib_ctx;
    TopsCodecFunctions*    topscodec   = ctx->topscodec_lib_ctx;
    enum AVPixelFormat avframe_format  = topspixfmt_2_avpixfmt(efbuf->ef_frame.pixel_format);
    int plane_num = efbuf->ef_frame.plane_num;
    int ret;

    ptrdiff_t linesizes1[4] = {0};
    size_t    planesizes[4] = {0};

    avframe->format = avframe_format;
    avframe->width  = efbuf->ef_frame.width;
    avframe->height = efbuf->ef_frame.height;
    for (int i = 0; i < 4; i++) {
        linesizes1[i]        = efbuf->ef_frame.plane[i].stride;
        avframe->linesize[i] = linesizes1[i];
    }

    ret = av_image_fill_plane_sizes(planesizes, avframe_format, avframe->height, linesizes1);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "[%p]av_image_fill_plane_sizes failed\n", ctx->handle);
        goto fail_unmap;
    }

    ret = av_frame_get_buffer(avframe, ctx->stride_align);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "[%p]av_frame_get_buffer failed, ret(%d)\n", ctx->handle, ret);
        goto fail_unmap;
    }

    for (int i = 0; i < plane_num; i++) {
        if (planesizes[i] == 0) continue;
        ret = topsruntime->lib_topsMemcpyDtoH(avframe->data[i], (void*)efbuf->ef_frame.plane[i].dev_addr, planesizes[i]);
        if (ret != topsSuccess) {
            av_log(avctx, AV_LOG_ERROR,
                   "[%p]D2H failed, plane[%d]: dev %p -> host %p, size %lu\n",
                   ctx->handle, i,
                   (void*)efbuf->ef_frame.plane[i].dev_addr,
                   avframe->data[i], planesizes[i]);
            av_frame_unref(avframe);
            goto fail_unmap;
        }
        av_log(avctx, AV_LOG_TRACE,
               "[%p]d2h[%d]: dev %p -> host %p, size %lu\n",
               ctx->handle, i,
               (void*)efbuf->ef_frame.plane[i].dev_addr,
               avframe->data[i], planesizes[i]);
    }

    ret = topscodec->lib_topscodecDecFrameUnmap(ctx->handle, &efbuf->ef_frame);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR, "[%p]topscodecDecFrameUnmap failed\n", ctx->handle);
        av_frame_unref(avframe);
        return AVERROR_BUG;
    }

    topsdec_set_frame_props(avctx, avframe, &efbuf->ef_frame);

    return 0;

fail_unmap:
    topscodec->lib_topscodecDecFrameUnmap(ctx->handle, &efbuf->ef_frame);
    return AVERROR_BUG;
}

static int topsdec_output_frame_async(AVCodecContext* avctx, AVFrame* avframe) {
    int ret = 0;
    topscodecFrame_t frame_tmp;
    EFCodecDecContext_t* ctx = (EFCodecDecContext_t*)avctx->priv_data;

    ff_mutex_lock(&ctx->frame_fifo_mutex);
    // non-draining: do not block send pkt to hw.
    if ((int)av_fifo_size(ctx->frame_fifo) == 0
        && !atomic_load(&ctx->eos_event_flag)
        && !atomic_load(&ctx->close_flag)
        && !ctx->draining) {
        ff_mutex_unlock(&ctx->frame_fifo_mutex);
        return AVERROR(EAGAIN);
    }
    // draining: block until got buffered frames.
    while ((int)av_fifo_size(ctx->frame_fifo) == 0
           && !atomic_load(&ctx->eos_event_flag)
           && !atomic_load(&ctx->close_flag)) {
        pthread_cond_wait(&ctx->frame_fifo_cond, &ctx->frame_fifo_mutex);
    }
    if (atomic_load(&ctx->close_flag)) {
        ff_mutex_unlock(&ctx->frame_fifo_mutex);
        return AVERROR_EOF;
    }
    if (atomic_load(&ctx->eos_event_flag)
        && (int)av_fifo_size(ctx->frame_fifo) == 0) {
        ff_mutex_unlock(&ctx->frame_fifo_mutex);
        return AVERROR_EOF;
    }
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)
    av_fifo_read(ctx->frame_fifo, &frame_tmp, 1);
#else
    av_fifo_generic_read(ctx->frame_fifo, &frame_tmp, sizeof(topscodecFrame_t), NULL);
#endif
    ff_mutex_unlock(&ctx->frame_fifo_mutex);

    EFBuffer* ef_buf = (EFBuffer*)av_mallocz(sizeof(EFBuffer));
    memcpy(&ef_buf->ef_frame, &frame_tmp, sizeof(topscodecFrame_t));
    ef_buf->avctx          = avctx;
    ef_buf->ef_dec_context = ctx;

    if (avctx->pix_fmt == AV_PIX_FMT_TOPSCODEC) {
        ret = topsdec_efbuf_to_avframe_d2d_async(ef_buf, avframe);
        /* zero-copy: ef_buf ownership transferred to AVFrame buf refs, freed by topscodec_free_buffer callback on last unref */
        if (!ctx->zero_copy) {
            av_freep(&ef_buf);
        }
    } else {
        ret = topsdec_efbuf_to_avframe_d2h_async(ef_buf, avframe);
        av_freep(&ef_buf);
    }
    if (ret < 0) return ret;
    avctx->height = avframe->height;
    avctx->width  = avframe->width;
    return 0;
}

static int topsdec_output_frame_sync(AVCodecContext* avctx, AVFrame* avframe, int is_internal) {
    int                ret          = 0;
    EFBuffer*          ef_buf_frame = NULL;
    AVHWFramesContext* hwframe_ctx  = NULL;

    EFCodecDecContext_t* ctx = (EFCodecDecContext_t*)avctx->priv_data;

    if (is_internal != 1 && (int)av_fifo_size(ctx->mid_avframe_fifo) > 0) {
        AVFrame* avframe_tmp;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
        av_fifo_read(ctx->mid_avframe_fifo, &avframe_tmp, 1);
#else
        av_fifo_generic_read(ctx->mid_avframe_fifo, &avframe_tmp, sizeof(AVFrame*), NULL);
#endif
        av_log(avctx, AV_LOG_DEBUG,
               "mid fifo [%p] Get frame ,size:%d, handle:%p\n",
               avframe_tmp, (int)av_fifo_size(ctx->mid_avframe_fifo), ctx->handle);
        av_frame_ref(avframe, avframe_tmp);
        av_frame_free(&avframe_tmp);
        avctx->height = avframe->height;
        avctx->width  = avframe->width;
        return 0;
    }

    ef_buf_frame = av_mallocz(sizeof(EFBuffer));
    memset(ef_buf_frame, 0, sizeof(EFBuffer));
    ef_buf_frame->type = EF_BUFFER_TYPE_FRAME;
    ret = ctx->topscodec_lib_ctx->lib_topscodecDecFrameMap(ctx->handle, &ef_buf_frame->ef_frame);
    if (TOPSCODEC_SUCCESS == ret) {
        if (ctx->draining && (0 == ef_buf_frame->ef_frame.width || 0 == ef_buf_frame->ef_frame.height)) {
            av_log(avctx, AV_LOG_DEBUG, "----EOS -----\n");
            atomic_store(&ctx->eos_event_flag, 1);
            av_usleep(10);
            av_freep(&ef_buf_frame);
            return AVERROR_EOF;
        }
        print_frame(avctx, &ef_buf_frame->ef_frame, "recv frame");
        ctx->total_frame_count++;
        av_log(avctx, AV_LOG_DEBUG, "[sync][%p] received total_frame_count:%ld\n", ctx->handle, ctx->total_frame_count);
        av_log(avctx, AV_LOG_DEBUG, "topscodecDecFrameMap success\n");
    } else if (TOPSCODEC_ERROR_BUFFER_EMPTY == ret) {
        av_freep(&ef_buf_frame);
        av_log(avctx, AV_LOG_DEBUG, "TOPSCODEC_ERROR_BUFFER_EMPTY1\n");
        return AVERROR(EAGAIN);
    } else {
        av_freep(&ef_buf_frame);
        av_log(avctx, AV_LOG_ERROR, "topscodecDecFrameMap failed, ret(%d)\n", ret);
        return AVERROR(EPERM);
    }

    ret = ff_decode_frame_props(avctx, avframe);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "ff_decode_frame_props failed\n");
        av_freep(&ef_buf_frame);
        return AVERROR_BUG;
    }
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
    av_buffer_unref(&avframe->opaque_ref);
    avframe->opaque = NULL;
#endif

    if (avctx->pix_fmt == AV_PIX_FMT_TOPSCODEC) {
        ef_buf_frame->avctx          = avctx;
        ef_buf_frame->ef_dec_context = ctx;
        ret = ff_topscodec_efbuf_to_avframe(ef_buf_frame, avframe);
        if (ret < 0) return AVERROR_BUG;
    } else {
        ef_buf_frame->avctx          = avctx;
        ef_buf_frame->ef_dec_context = ctx;
        ret = ff_topscodec_efbuf_to_avframe(ef_buf_frame, &ctx->mid_frame);
        if (ret < 0) return AVERROR_BUG;
        // 这里位置不要移动，av_hwframe_transfer_data会用到
        // avframe->format = ctx->mid_frame.format;
        hwframe_ctx = (AVHWFramesContext*)ctx->mid_frame.hw_frames_ctx->data;
        avframe->format      = hwframe_ctx->sw_format;
        avframe->width       = ctx->mid_frame.width;
        avframe->height      = ctx->mid_frame.height;
        avframe->linesize[0] = ctx->mid_frame.linesize[0];
        avframe->linesize[1] = ctx->mid_frame.linesize[1];
        avframe->linesize[2] = ctx->mid_frame.linesize[2];
        avframe->linesize[3] = ctx->mid_frame.linesize[3];

        av_frame_get_buffer(avframe, ctx->stride_align);
        ret = av_hwframe_transfer_data(avframe, &ctx->mid_frame, 0);
        if (ret) {
            av_log(avctx, AV_LOG_ERROR, "av_frame_copy failed\n");
            av_frame_unref(&ctx->mid_frame);
            return AVERROR_BUG;
        }
        //  print_avframe(avctx, &ctx->mid_frame);
        av_frame_copy_props(avframe, &ctx->mid_frame);
        av_frame_unref(&ctx->mid_frame);
    }
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(61, 3, 100)  // n7.0
    avframe->coded_picture_number = ctx->total_frame_count;
#endif
    print_avframe(avctx, avframe);
    return ret;
}

static int topsdec_output_frame(AVCodecContext* avctx, AVFrame* avframe, int is_internal) {
    EFCodecDecContext_t* ctx = (EFCodecDecContext_t*)avctx->priv_data;
    av_frame_unref(avframe);

    if (ctx->callback)
        return topsdec_output_frame_async(avctx, avframe);
    else
        return topsdec_output_frame_sync(avctx, avframe, is_internal);
}

static int topsdec_send_packet_async(AVCodecContext* avctx, EFCodecDecContext_t* ctx, AVPacket* avpkt) {
    AVFrame* prop_frame = NULL;
    int      ret        = 0;

    int sem_val;
    sem_getvalue(&ctx->send_avpacket_sem, &sem_val);
    av_log(avctx, AV_LOG_DEBUG, "[%p] topsdec_send_packet_async sem_val: %d\n", ctx->handle, sem_val);
    while (sem_wait(&ctx->send_avpacket_sem) != 0) {
        if (errno == EINTR) continue;
        return AVERROR_BUG;
    }

    if (atomic_load(&ctx->close_flag)) return AVERROR_EOF;

    ff_topscodec_avpkt_to_efbuf(avpkt, ctx->ef_buf_pkt);
    print_stream(avctx, &ctx->ef_buf_pkt->ef_pkt);

    ret = ctx->topscodec_lib_ctx->lib_topscodecDecodeStream(ctx->handle, &ctx->ef_buf_pkt->ef_pkt, 0);
    if (ret != TOPSCODEC_SUCCESS) {
        if (ret == TOPSCODEC_ERROR_TIMEOUT) {
            av_log(avctx, AV_LOG_WARNING, "[%p][async]topscodecDecodeStream timeout, pipeline full. Should not happen!!!\n", ctx->handle);
            return AVERROR(EAGAIN);
        }
        av_log(avctx, AV_LOG_ERROR, "[%p]topscodecDecodeStream failed, ret=%d\n", ctx->handle, ret);
        sem_post(&ctx->send_avpacket_sem);
        return AVERROR_BUG;
    }
    av_log(avctx, AV_LOG_DEBUG, "[%p]topscodecDecodeStream success\n", ctx->handle);

    if (!ctx->pkt_prop_frame) {
        prop_frame = av_frame_alloc();
        ret        = ff_decode_frame_props(avctx, prop_frame);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "[%p]ff_decode_frame_props failed receive frame\n", ctx->handle);
            av_frame_free(&prop_frame);
            return AVERROR_BUG;
        }
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
        av_buffer_unref(&prop_frame->opaque_ref);
        prop_frame->opaque = NULL;
#endif
        ctx->pkt_prop_frame = prop_frame;
        av_log(avctx, AV_LOG_DEBUG, "[%p]pkt_prop_frame set success.\n", ctx->handle);
    }

    return 0;
}

static int topsdec_send_packet_sync(AVCodecContext* avctx, EFCodecDecContext_t* ctx) {
    AVFrame* prop_frame   = NULL;
    int      ret          = 0;
    int      ret2         = 0;
    int      sleep_handle = 0;

    do {
        ret = ctx->topscodec_lib_ctx->lib_topscodecDecodeStream(ctx->handle, &ctx->ef_buf_pkt->ef_pkt, 0);
        if (ret != TOPSCODEC_SUCCESS) {
            if (ret == TOPSCODEC_ERROR_TIMEOUT) {
                AVFrame* tmp = av_frame_alloc();
                ret2         = topsdec_output_frame(avctx, tmp, 1);
                if (0 == ret2) {
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
                    if (av_fifo_can_write(ctx->mid_avframe_fifo) < 1) {
                        av_fifo_grow2(ctx->mid_avframe_fifo, MAX_FRAME_NUM);
                    }
                    av_fifo_write(ctx->mid_avframe_fifo, &tmp, 1);
#else
                    if (av_fifo_space(ctx->mid_avframe_fifo) < sizeof(AVFrame*)) {
                        av_fifo_grow(ctx->mid_avframe_fifo, MAX_FRAME_NUM * sizeof(AVFrame*));
                    }
                    av_fifo_generic_write(ctx->mid_avframe_fifo, &tmp, sizeof(AVFrame*), NULL);
#endif
                    av_log(avctx, AV_LOG_DEBUG, "[%p]mid_frame fifo write success, size:%d.\n", ctx->handle, (int)av_fifo_size(ctx->mid_avframe_fifo));
                } else if (AVERROR(EAGAIN) == ret2) {
                    av_usleep(2);
                    av_frame_free(&tmp);
                    av_log(avctx, AV_LOG_DEBUG, "TOPSCODEC_ERROR_BUFFER_EMPTY22\n");
                } else {
                    av_log(avctx, AV_LOG_ERROR, "[%p]topsdec_output_frame failed. ret = %d\n", ctx->handle, ret2);
                    av_frame_free(&tmp);
                    return AVERROR_BUG;
                }
                av_log(avctx, AV_LOG_DEBUG, "[%p]topscodecDecodeStream timeout, retry again!\n", ctx->handle);
                sleep_wait(&sleep_handle);
            } else {
                av_log(avctx, AV_LOG_ERROR, "[%p]topscodecDecSendStream failed. ret = %d\n", ctx->handle, ret);
                return AVERROR_BUG;
            }
        } else {
            av_log(avctx, AV_LOG_DEBUG, "[%p]topscodecDecodeStream success\n", ctx->handle);
            ff_mutex_lock(&ctx->pkt_prop_mutex);
            if ((int)av_fifo_size(ctx->pkt_prop_fifo) > 0) {
                ff_mutex_unlock(&ctx->pkt_prop_mutex);
                break;
            }
            prop_frame = av_frame_alloc();
            ret        = ff_decode_frame_props(avctx, prop_frame);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "[%p]ff_decode_frame_props failed receive frame\n", ctx->handle);
                av_frame_free(&prop_frame);
                ff_mutex_unlock(&ctx->pkt_prop_mutex);
                return AVERROR_BUG;
            }
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
            av_buffer_unref(&prop_frame->opaque_ref);
            prop_frame->opaque = NULL;
#endif
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
            if (av_fifo_can_write(ctx->pkt_prop_fifo) < 1) {
                av_fifo_grow2(ctx->pkt_prop_fifo, MAX_FRAME_NUM);
            }
            av_fifo_write(ctx->pkt_prop_fifo, &prop_frame, 1);
#else
            if (av_fifo_space(ctx->pkt_prop_fifo) < sizeof(AVFrame*)) {
                av_fifo_grow(ctx->pkt_prop_fifo, MAX_FRAME_NUM * sizeof(AVFrame*));
            }
            av_fifo_generic_write(ctx->pkt_prop_fifo, &prop_frame, sizeof(AVFrame*), NULL);
#endif
            av_log(avctx, AV_LOG_DEBUG, "[%p]prop fifo write success, size:%d.\n", ctx->handle, (int)av_fifo_size(ctx->pkt_prop_fifo));
            ff_mutex_unlock(&ctx->pkt_prop_mutex);
        }
    } while (ret == TOPSCODEC_ERROR_TIMEOUT);

    return 0;
}

static int topsdec_send_packet(AVCodecContext* avctx, EFCodecDecContext_t* ctx, AVPacket* avpkt) {
    int ret          = 0;
    int sleep_handle = 0;

    ctx->ef_buf_pkt->avctx          = avctx;
    ctx->ef_buf_pkt->ef_dec_context = ctx;
    if (avpkt->size <= 0) {
        ctx->draining = 1;
    }
    av_log(avctx, AV_LOG_DEBUG, "[%p]topsdec_send_packet: pkt_size=%d, ctx->draining:%d\n", ctx->handle, avpkt->size, ctx->draining);

    if (ctx->first_packet) {
        if (avctx->extradata_size) {
            AVPacket p;
            p.data = avctx->extradata;
            p.size = avctx->extradata_size;
            p.pts  = 0;
            if (ctx->callback) {
                // sem -1 for the first packet.
                while (sem_wait(&ctx->send_avpacket_sem) != 0) {
                    if (errno == EINTR) continue;
                    return AVERROR_BUG;
                }
            }
            ff_topscodec_avpkt_to_efbuf(&p, ctx->ef_buf_pkt);
            print_stream(avctx, &ctx->ef_buf_pkt->ef_pkt);
            do {
                ret = ctx->topscodec_lib_ctx->lib_topscodecDecodeStream(ctx->handle, &ctx->ef_buf_pkt->ef_pkt, 0);
                if (ret != TOPSCODEC_SUCCESS) {
                    if (ret == TOPSCODEC_ERROR_TIMEOUT) {
                        av_log(avctx, AV_LOG_WARNING, "[%p]topscodecDecodeStream timeout, retry again!\n", ctx->handle);
                        sleep_wait(&sleep_handle);
                    } else {
                        av_log(avctx, AV_LOG_ERROR, "[%p]topscodecDecSendStream failed. ret = %d\n", ctx->handle, ret);
                        return AVERROR_BUG;
                    }
                }
            } while (ret == TOPSCODEC_ERROR_TIMEOUT);
        }
        ctx->first_packet = 0;
    }

    ctx->total_packet_count++;
    av_log(avctx, AV_LOG_DEBUG, "[%p][async] send total_packet_count:%ld\n", ctx->handle, ctx->total_packet_count);

    if (ctx->callback) {
        return topsdec_send_packet_async(avctx, ctx, avpkt);
    } else {
        ff_topscodec_avpkt_to_efbuf(avpkt, ctx->ef_buf_pkt);
        print_stream(avctx, &ctx->ef_buf_pkt->ef_pkt);
        return topsdec_send_packet_sync(avctx, ctx);
    }
}

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 18, 100)  // n3.2
static int topscodec_decode(AVCodecContext* avctx, void* data, int* got_frame, AVPacket* avpkt) {
    EFCodecDecContext_t* ctx   = NULL;
    AVFrame*             frame = data;

    AVPacket filter_packet   = {0};
    AVPacket filtered_packet = {0};
    int      ret             = 0;

    if (NULL == avctx || NULL == avctx->priv_data) {
        av_log(avctx, AV_LOG_ERROR, "Early error in topscodec_decode func.\n");
        return AVERROR_BUG;
    }

    ctx = (EFCodecDecContext_t*)avctx->priv_data;
    if (!ctx->decoder_init_flag) {
        av_log(avctx, AV_LOG_ERROR, "Decode got abort or not init.\n");
        return AVERROR_BUG;
    }

    ff_mutex_lock(&ctx->frame_fifo_mutex);
    if (atomic_load(&ctx->eos_event_flag)) {
        int fifo_empty = ctx->callback ? (int)av_fifo_size(ctx->frame_fifo) == 0 : (int)av_fifo_size(ctx->mid_avframe_fifo) == 0;
        if (fifo_empty) {
            ff_mutex_unlock(&ctx->frame_fifo_mutex);
            return AVERROR_EOF;
        }
    }
    ff_mutex_unlock(&ctx->frame_fifo_mutex);

    if (ctx->draining) {
        goto recv;
    }

    if (ctx->bsf && avpkt && avpkt->size) {
        if ((ret = av_packet_ref(&filter_packet, avpkt)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "av_packet_ref failed\n");
            return ret;
        }

        if ((ret = av_bsf_send_packet(ctx->bsf, &filter_packet)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "av_bsf_send_packet failed\n");
            av_packet_unref(&filter_packet);
            return ret;
        }

        if ((ret = av_bsf_receive_packet(ctx->bsf, &filtered_packet)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "av_bsf_receive_packet failed\n");
            return ret;
        }
        avpkt = &filtered_packet;
    }

    ret = topsdec_send_packet(avctx, ctx, avpkt);
    if (ret == AVERROR(EAGAIN)) {
        av_log(avctx, AV_LOG_WARNING, "[%p]topsdec_send_packet: EAGAIN should not happen!!!\n", ctx->handle);
        goto recv;
    }
    if (ret < 0) return ret;

recv:
    ret = topsdec_output_frame(avctx, frame, 0);
    if (ret == AVERROR(EAGAIN)) {
        if (ctx->draining) {
            av_log(avctx, AV_LOG_DEBUG, "[%p]EAGAIN(draining), ret:%d\n", ctx->handle, ret);
            goto recv;
        } else {
            *got_frame = 0;
            av_log(avctx, AV_LOG_DEBUG, "[%p]EAGAIN(non-draining), ret:%d\n", ctx->handle, ret);
        }
    } else if (ret < 0) {
        return ret;
    } else {
        *got_frame = 1;
        av_log(avctx, AV_LOG_DEBUG, "topscodec got_frame, got_frame:%d, ret:%d.\n", *got_frame, ret);
    }
    return ret;
}
#endif  // n3.2

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 100, 100)  // n4.x
static int topscodec_receive_frame(AVCodecContext* avctx, AVFrame* frame) {
    EFCodecDecContext_t* ctx;
    int                  ret;

    if (NULL == avctx || NULL == avctx->priv_data) {
        av_log(avctx, AV_LOG_ERROR, "Early error in topscodec_receive_frame\n");
        return AVERROR_BUG;
    }

    ctx = (EFCodecDecContext_t*)avctx->priv_data;
    if (!ctx->decoder_init_flag) {
        av_log(avctx, AV_LOG_ERROR, "[%p]Decode got abort or not init.\n", ctx->handle);
        return AVERROR_BUG;
    }

    ff_mutex_lock(&ctx->frame_fifo_mutex);
    if (atomic_load(&ctx->eos_event_flag)) {
        int fifo_empty = ctx->callback ? (int)av_fifo_size(ctx->frame_fifo) == 0 : (int)av_fifo_size(ctx->mid_avframe_fifo) == 0;
        if (fifo_empty) {
            ff_mutex_unlock(&ctx->frame_fifo_mutex);
            return AVERROR_EOF;
        }
    }
    ff_mutex_unlock(&ctx->frame_fifo_mutex);

    if (!ctx->av_pkt->size) {
        ret = ff_decode_get_packet(avctx, ctx->av_pkt);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN)) {
                return topsdec_output_frame(avctx, frame, 0);
            } else if (ret != AVERROR_EOF) {
                return ret;
            }
        }
    }

    if (ctx->draining) goto dequeue;

    ret = topsdec_send_packet(avctx, ctx, ctx->av_pkt);
    if (ret == AVERROR(EAGAIN)) {
        av_log(avctx, AV_LOG_WARNING, "[%p]topsdec_send_packet: EAGAIN should not happen!!!\n", ctx->handle);
        goto dequeue;
    }
    if (ret < 0) return ret;

    av_packet_unref(ctx->av_pkt);

dequeue:
    ret = topsdec_output_frame(avctx, frame, 0);
    if (ret == AVERROR(EAGAIN)) {
        if (ctx->draining) {
            av_log(avctx, AV_LOG_DEBUG, "[%p]EAGAIN(draining), ret:%d\n", ctx->handle, ret);
            goto dequeue;
        } else {
            av_log(avctx, AV_LOG_DEBUG, "[%p]EAGAIN(non-draining), ret:%d\n", ctx->handle, ret);
        }
    } else if (ret < 0) {
        return ret;
    } else {
        av_log(avctx, AV_LOG_DEBUG, "[%p]topscodec got_frame, ret:%d\n", ctx->handle, ret);
    }
    return ret;
}
#endif  // n4.x

static void topscodec_flush(struct AVCodecContext* avctx) {
    int ret = 0;

    ret = topscodec_decode_close(avctx);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to close decoder during flush: ret=%d\n", ret);
        return;
    }

    ret = topscodec_decode_init(avctx);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to reinit decoder during flush: ret=%d\n", ret);
        return;
    }

    av_log(avctx, AV_LOG_VERBOSE, "topscodec_flush completed.\n");
}

#define OFFSET(x) offsetof(EFCodecDecContext_t, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
#define DEFAULT 0

static const AVOption options[] = {
    {"card_id",
     "use to choose the accelerator card",
     OFFSET(card_id),
     AV_OPT_TYPE_INT,
     {.i64 = 0},
     0,
     FF_TOPSCODEC_MAX_CARD_ID,
     VD},
    {"device_id",
     "use to choose the accelerator device",
     OFFSET(device_id),
     AV_OPT_TYPE_INT,
     {.i64 = 0},
     0,
     FF_TOPSCODEC_MAX_DEVICE_ID,
     VD},
    {"callback",
     "use to choose the callback model",
     OFFSET(callback),
     AV_OPT_TYPE_INT,
     {.i64 = 1},
     0,
     1,
     VD},
    {"balance",
     "use to choose the balance mode",
     OFFSET(balance),
     AV_OPT_TYPE_INT,
     {.i64 = 1},
     0,
     1,
     VD},
    {"hw_id",
     "use to choose the accelerator id",
     OFFSET(hw_id),
     AV_OPT_TYPE_INT,
     {.i64 = 15},
     0,
     100,
     VD},
    {"in_w",
     "video width",
     OFFSET(in_width),
     AV_OPT_TYPE_INT,
     {.i64 = 0},
     0,
     INT_MAX,
     VD},
    {"in_h",
     "video height",
     OFFSET(in_height),
     AV_OPT_TYPE_INT,
     {.i64 = 0},
     0,
     INT_MAX,
     VD},
    {"sf",
     "Set the switch frame number",
     OFFSET(sf),
     AV_OPT_TYPE_INT,
     {.i64 = 1},
     0,
     255,
     VD},
    {"out_port_num",
     "decode outport buf num",
     OFFSET(output_buf_num),
     AV_OPT_TYPE_INT,
     {.i64 = 8},
     0,
     100,
     VD},
    {"in_port_num",
     "decode import buf num",
     OFFSET(input_buf_num),
     AV_OPT_TYPE_INT,
     {.i64 = 8},
     0,
     100,
     VD},
    {"zero_copy",
     "Reference decoder hardware buffers without device plane copy",
     OFFSET(zero_copy),
     AV_OPT_TYPE_BOOL,
     {.i64 = 1},
     0,
     INT_MAX,
     VD},
    {"output_pixfmt",
     "decoder output pixfmt",
     OFFSET(str_output_pixfmt),
     AV_OPT_TYPE_STRING,
     {.str = "yuv420p"},
     0,
     INT_MAX,
     VD},
    {"output_colorspace",
     "decoder output colorspace(bt601)",
     OFFSET(color_space),
     AV_OPT_TYPE_STRING,
     {.str = "bt601"},
     0,
     0,
     VD},
    {"enable_rotation",
     "Forces open the rotation, only support orientation,90/180/270",
     OFFSET(enable_rotation),
     AV_OPT_TYPE_BOOL,
     {.i64 = 0},
     -1,
     1,
     VD},
    {"rotation",
     "setting rotation, only support orientation,90/180/270",
     OFFSET(rotation),
     AV_OPT_TYPE_INT,
     {.i64 = 0},
     0,
     INT_MAX,
     VD},
    {"enable_crop",
     "Forces open the crop",
     OFFSET(enable_crop),
     AV_OPT_TYPE_BOOL,
     {.i64 = 0},
     -1,
     1,
     VD},
    {"enable_resize",
     "Forces open the resize,only support downscale",
     OFFSET(enable_resize),
     AV_OPT_TYPE_BOOL,
     {.i64 = 0},
     -1,
     1,
     VD},
    {"enable_sfo",
     "enable frame sampling interval",
     OFFSET(enable_sfo),
     AV_OPT_TYPE_BOOL,
     {.i64 = 0},
     -1,
     1,
     VD},
    {"crop_top",
     "out top (crop)",
     OFFSET(crop.top),
     AV_OPT_TYPE_INT,
     {.i64 = 0},
     0,
     INT_MAX,
     VD},
    {"crop_bottom",
     "out bottom(crop)",
     OFFSET(crop.bottom),
     AV_OPT_TYPE_INT,
     {.i64 = 0},
     0,
     INT_MAX,
     VD},
    {"crop_left",
     "out left(crop)",
     OFFSET(crop.left),
     AV_OPT_TYPE_INT,
     {.i64 = 0},
     0,
     INT_MAX,
     VD},
    {"crop_right",
     "out right(crop)",
     OFFSET(crop.right),
     AV_OPT_TYPE_INT,
     {.i64 = 0},
     0,
     INT_MAX,
     VD},
    {"resize_w",
     "out width(resize)",
     OFFSET(resize.width),
     AV_OPT_TYPE_INT,
     {.i64 = 0},
     0,
     INT_MAX,
     VD},
    {"resize_h",
     "out height(resize)",
     OFFSET(resize.height),
     AV_OPT_TYPE_INT,
     {.i64 = 0},
     0,
     INT_MAX,
     VD},
    {"resize_m",
     "0-Bilinear, 1-Nearest",
     OFFSET(resize.mode),
     AV_OPT_TYPE_INT,
     {.i64 = 0},
     0,
     INT_MAX,
     VD},
    {"sfo",
     "frame sampling interval value",
     OFFSET(sfo),
     AV_OPT_TYPE_INT,
     {.i64 = 0},
     0,
     INT_MAX,
     VD},
    {"idr",
     "frame sampling interval value",
     OFFSET(sf_idr),
     AV_OPT_TYPE_INT,
     {.i64 = 0},
     0,
     1,
     VD},
    {"stride_align",
     "stride align",
     OFFSET(stride_align),
     AV_OPT_TYPE_INT,
     {.i64 = 1},
     1,
     INT_MAX,
     VD},
    {NULL},
};

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 18, 100)
static const AVCodecHWConfigInternal* topscodec_hw_configs[] = {
    &(const AVCodecHWConfigInternal){
        .public  = {.pix_fmt = AV_PIX_FMT_TOPSCODEC,
                   .methods = AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX |
                              AV_CODEC_HW_CONFIG_METHOD_INTERNAL,
                   .device_type = AV_HWDEVICE_TYPE_TOPSCODEC},
        .hwaccel = NULL,
    },
    NULL};
#endif

#define TOPSCODEC_CLASS(NAME)                             \
    static const AVClass topscodec_##NAME##_dec_class = { \
        .class_name = #NAME "_topscodec_decoder",         \
        .item_name  = av_default_item_name,               \
        .option     = options,                            \
        .version    = LIBAVUTIL_VERSION_INT,              \
    };

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 18, 100)  // n3.x
#define TOPSCODECDEC(NAME, LONGNAME, CODEC, BSF_NAME)                      \
    TOPSCODEC_CLASS(NAME)                                                  \
    AVHWAccel ff_##NAME##_topscodec_hwaccel = {                            \
        .name    = #NAME "_topscodec",                                     \
        .type    = AVMEDIA_TYPE_VIDEO,                                     \
        .id      = CODEC,                                                  \
        .pix_fmt = AV_PIX_FMT_TOPSCODEC,                                   \
    };                                                                     \
    AVCodec ff_##NAME##_topscodec_decoder = {                              \
        .name           = #NAME "_topscodec",                              \
        .long_name      = NULL_IF_CONFIG_SMALL(#NAME "TOPSCODEC"),         \
        .type           = AVMEDIA_TYPE_VIDEO,                              \
        .id             = CODEC,                                           \
        .priv_data_size = sizeof(EFCodecDecContext_t),                     \
        .priv_class     = &topscodec_##NAME##_dec_class,                   \
        .init           = topscodec_decode_init,                           \
        .decode         = topscodec_decode,                                \
        .flush          = topscodec_flush,                                 \
        .close          = topscodec_decode_close,                          \
        .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING, \
        .caps_internal =                                                   \
            FF_CODEC_CAP_SETS_PKT_DTS | FF_CODEC_CAP_INIT_CLEANUP,         \
        .pix_fmts = ff_topscodec_pix_fmts,                                 \
    }

#elif LIBAVCODEC_VERSION_INT <= AV_VERSION_INT(59, 18, 100)  // n4.x
#define TOPSCODECDEC(NAME, LONGNAME, CODEC, BSF_NAME)                  \
    TOPSCODEC_CLASS(NAME)                                              \
    const AVCodec ff_##NAME##_topscodec_decoder = {                    \
        .name           = #NAME "_topscodec",                          \
        .long_name      = NULL_IF_CONFIG_SMALL(#NAME "TOPSCODEC"),     \
        .type           = AVMEDIA_TYPE_VIDEO,                          \
        .id             = CODEC,                                       \
        .priv_data_size = sizeof(EFCodecDecContext_t),                 \
        .priv_class     = &topscodec_##NAME##_dec_class,               \
        .init           = topscodec_decode_init,                       \
        .receive_frame  = topscodec_receive_frame,                     \
        .close          = topscodec_decode_close,                      \
        .flush          = topscodec_flush,                             \
        .bsfs           = BSF_NAME,                                    \
        .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE | \
                        AV_CODEC_CAP_AVOID_PROBING,                    \
        .caps_internal =                                               \
            FF_CODEC_CAP_SETS_PKT_DTS | FF_CODEC_CAP_INIT_CLEANUP,     \
        .pix_fmts     = ff_topscodec_pix_fmts,                         \
        .hw_configs   = topscodec_hw_configs,                          \
        .wrapper_name = "topscodec",                                   \
    }
#else
#define TOPSCODECDEC(NAME, LONGNAME, CODEC, BSF_NAME)                    \
    TOPSCODEC_CLASS(NAME)                                                \
    const FFCodec ff_##NAME##_topscodec_decoder = {                      \
        .p.name           = #NAME "_topscodec",                          \
        .p.long_name      = NULL_IF_CONFIG_SMALL(#NAME "TOPSCODEC"),     \
        .p.type           = AVMEDIA_TYPE_VIDEO,                          \
        .p.id             = CODEC,                                       \
        .priv_data_size   = sizeof(EFCodecDecContext_t),                 \
        .p.priv_class     = &topscodec_##NAME##_dec_class,               \
        .init             = topscodec_decode_init,                       \
        .cb_type          = FF_CODEC_CB_TYPE_RECEIVE_FRAME,              \
        .cb.receive_frame = topscodec_receive_frame,                     \
        .close            = topscodec_decode_close,                      \
        .flush            = topscodec_flush,                             \
        .bsfs             = BSF_NAME,                                    \
        .p.capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE | \
                          AV_CODEC_CAP_AVOID_PROBING,                    \
        .caps_internal =                                                 \
            FF_CODEC_CAP_SETS_PKT_DTS | FF_CODEC_CAP_INIT_CLEANUP,       \
        .p.pix_fmts     = ff_topscodec_pix_fmts,                         \
        .hw_configs     = topscodec_hw_configs,                          \
        .p.wrapper_name = "topscodec",                                   \
    }
#endif

#if CONFIG_H263_TOPSCODEC_DECODER
TOPSCODECDEC(h263, "H.263", AV_CODEC_ID_H263, NULL);
#endif
#if CONFIG_H264_TOPSCODEC_DECODER
TOPSCODECDEC(h264, "H.264", AV_CODEC_ID_H264, "h264_mp4toannexb");
#endif
#if CONFIG_HEVC_TOPSCODEC_DECODER
TOPSCODECDEC(hevc, "HEVC", AV_CODEC_ID_HEVC, "hevc_mp4toannexb");
#endif
#if CONFIG_MJPEG_TOPSCODEC_DECODER
TOPSCODECDEC(mjpeg, "MJPEG", AV_CODEC_ID_MJPEG, NULL);
#endif
#if CONFIG_MPEG2_TOPSCODEC_DECODER
TOPSCODECDEC(mpeg2, "MPEG2", AV_CODEC_ID_MPEG2VIDEO, NULL);
#endif
#if CONFIG_MPEG4_TOPSCODEC_DECODER
TOPSCODECDEC(mpeg4, "MPEG4", AV_CODEC_ID_MPEG4, NULL);
#endif
#if CONFIG_VC1_TOPSCODEC_DECODER
TOPSCODECDEC(vc1, "VC1", AV_CODEC_ID_VC1, NULL);
#endif
#if CONFIG_VP8_TOPSCODEC_DECODER
TOPSCODECDEC(vp8, "VP8", AV_CODEC_ID_VP8, NULL);
#endif
#if CONFIG_VP9_TOPSCODEC_DECODER
TOPSCODECDEC(vp9, "VP9", AV_CODEC_ID_VP9, NULL);
#endif
#if CONFIG_AVS_TOPSCODEC_DECODER
TOPSCODECDEC(avs, "AVS", AV_CODEC_ID_CAVS, NULL);
#endif

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 18, 100)
#if CONFIG_AVS2_TOPSCODEC_DECODER
TOPSCODECDEC(avs2, "AVS2", AV_CODEC_ID_AVS2, NULL);
#endif
#if CONFIG_AV1_TOPSCODEC_DECODER
TOPSCODECDEC(av1, "AV1", AV_CODEC_ID_AV1, NULL);
#endif
#endif
