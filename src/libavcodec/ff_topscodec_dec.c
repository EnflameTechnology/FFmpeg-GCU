/******************************************************************************
 * Enflame Video Process Platform SDK
 * Copyright (C) [2023] by Enflame, Inc. All rights reserved
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
#include <semaphore.h>
#include <stdatomic.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
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
#include "version.h"
#define TOPSCODEC_FREE_FUNCTIONS 1
#define TOPSCODEC_LOAD_FUNCTIONS 1
#include "avcodec.h"
#include "ff_topscodec_buffers.h"
#include "ff_topscodec_dec.h"
#include "internal.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_topscodec.h"

#if LIBAVCODEC_VERSION_INT >=  AV_VERSION_INT(59, 27, 100)  // 5.1
#include "codec_internal.h"
#include "config_components.h"
#endif

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 18, 100)  // 4.0
#include "decode.h"               //3.2 is not support 
#include "hwconfig.h"             //3.2 is not support 
#endif

static pthread_mutex_t g_dec_mutex = PTHREAD_MUTEX_INITIALIZER;
#define FF_EFC_MAJPR_VERSION 1
#define FF_EFC_MINOR_VERSION 0
#define FF_EFC_PATCH_VERSION 1

#define FF_IDR_MAGIC (16384)

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

static inline void topscodec_get_version(AVCodecContext* avctx) {
    EFCodecDecContext_t* ctx = NULL;
    u32_t                major;
    u32_t                minor;
    u32_t                patch;

    ctx = avctx->priv_data;
    ctx->topscodec_lib_ctx->lib_topscodecGetLibVersion(&major, &minor, &patch);
    av_log(NULL, AV_LOG_DEBUG, "REL FLAG:20230321\n");
    av_log(NULL, AV_LOG_DEBUG, "TOPSCODEC: %d.%d.%d \n", major, minor, patch);
    av_log(NULL, AV_LOG_DEBUG, "FFMPEG_TOPSCODEC: %d.%d.%d \n", FF_EFC_MAJPR_VERSION, FF_EFC_MINOR_VERSION,
           FF_EFC_PATCH_VERSION);
}

static void print_caps(AVCodecContext* avctx, topscodecDecCaps_t* DecCaps) {
    av_log(avctx, AV_LOG_DEBUG, "topscodecDecGetCaps success {.\n");
    av_log(avctx, AV_LOG_DEBUG, "Caps supported(%d)           \t\n", DecCaps->supported);
    av_log(avctx, AV_LOG_DEBUG, "max_width(%d)                \t\n", DecCaps->max_width);
    av_log(avctx, AV_LOG_DEBUG, "max_height(%d)               \t\n", DecCaps->max_height);
    av_log(avctx, AV_LOG_DEBUG, "min_width(%d)                \t\n", DecCaps->min_width);
    av_log(avctx, AV_LOG_DEBUG, "min_height(%d)               \t\n", DecCaps->min_height);
    av_log(avctx, AV_LOG_DEBUG, "output_pixel_format_mask(%d) \t\n", DecCaps->output_pixel_format_mask);
    av_log(avctx, AV_LOG_DEBUG, "scale_up_supported(%d)       \t\n", DecCaps->scale_up_supported);
    av_log(avctx, AV_LOG_DEBUG, "rotation_supported(%d)       \t\n", DecCaps->rotation_supported);
    av_log(avctx, AV_LOG_DEBUG, "crop_supported(%d)           \t\n", DecCaps->crop_supported);
    av_log(avctx, AV_LOG_DEBUG, "}                            \t\n");
}

static void print_create_info(AVCodecContext* avctx, topscodecDecCreateInfo_t* create_info) {
    av_log(avctx, AV_LOG_DEBUG, "topscodecDecCreateInfo_t info {.\n");
    av_log(avctx, AV_LOG_DEBUG, "device_id(%d)                \t\n", create_info->device_id);
    av_log(avctx, AV_LOG_DEBUG, "hw_ctx_id(%d)                \t\n", create_info->hw_ctx_id);
    av_log(avctx, AV_LOG_DEBUG, "sw_ctx_id(%d)                \t\n", create_info->sw_ctx_id);
    av_log(avctx, AV_LOG_DEBUG, "codec(%d)                    \t\n", create_info->codec);
    av_log(avctx, AV_LOG_DEBUG, "\t buf_size(%d)              \t\n", create_info->stream_buf_size);
    av_log(avctx, AV_LOG_DEBUG, "\t run_mode(%s)              \t\n",
           create_info->run_mode ? "TOPSCODEC_RUN_MODE_SYNC" : "TOPSCODEC_RUN_MODE_ASYNC");
    av_log(avctx, AV_LOG_DEBUG, "\t             }\t\n");
}

static void print_stream(AVCodecContext* avctx, topscodecStream_t* stream) {
    av_log(avctx, AV_LOG_DEBUG, "stream info {                   \n");
    av_log(avctx, AV_LOG_DEBUG, "\t stream addr(0x%lx)           \t\n", stream->mem_addr);
    av_log(avctx, AV_LOG_DEBUG, "\t data_offset(%d)              \t\n", stream->data_offset);
    av_log(avctx, AV_LOG_DEBUG, "\t alloc_len(%d)                \t\n", stream->alloc_len);
    av_log(avctx, AV_LOG_DEBUG, "\t data_len(%d)                 \t\n", stream->data_len);
    av_log(avctx, AV_LOG_DEBUG, "\t pts(%ld)                     \t\n", stream->pts);
    av_log(avctx, AV_LOG_DEBUG, "\t mem_type(%s)                 \t\n",
           stream->mem_type ? "TOPSCODEC_MEM_TYPE_DEV" : "TOPSCODEC_MEM_TYPE_HOST");
    av_log(avctx, AV_LOG_DEBUG, "\t                             }\t\n");
}

static void print_frame(AVCodecContext* avctx, topscodecFrame_t* frame) {
    av_log(avctx, AV_LOG_DEBUG, "topscodecDecFrameMap successful {  \n");
    av_log(avctx, AV_LOG_DEBUG, "\t frame addr(0x%lx)            \t\n", frame->plane->dev_addr);
    av_log(avctx, AV_LOG_DEBUG, "\t stride(%d)                   \t\n", frame->plane->stride);
    av_log(avctx, AV_LOG_DEBUG, "\t width(%d)                    \t\n", frame->width);
    av_log(avctx, AV_LOG_DEBUG, "\t hight(%d)                    \t\n", frame->height);
    av_log(avctx, AV_LOG_DEBUG, "\t type(%d)                     \t\n", frame->pic_type);
    av_log(avctx, AV_LOG_DEBUG, "\t pixel_fmt(%d)                \t\n", frame->pixel_format);
    av_log(avctx, AV_LOG_DEBUG, "\t pts(%lu)                     \t\n", frame->pts);
    av_log(avctx, AV_LOG_DEBUG, "\t                             }\t\n");
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

static av_cold int topscodec_decode_init(AVCodecContext* avctx) {
    EFCodecDecContext_t*      ctx          = NULL;
    AVHWFramesContext*        hwframe_ctx  = NULL;
    AVHWDeviceContext*        device_ctx   = NULL;
    AVTOPSCodecDeviceContext* device_hwctx = NULL;

    topscodecDecCreateInfo_t codec_info            = {0};
    topscodecDecParams_t     params                = {0};
    topsPointerAttribute_t   att                   = {0};
    topsError_t              tops_ret              = TOPSCODEC_SUCCESS;
    void*                    tmp                   = NULL;
    char                     card_idx[sizeof(int)] = {0};
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

    int switch_frames_mode = 0;
    int switch_frames_num  = 0;
    int rotation_tmp       = 0;
    int debug_level        = 1;

    enum AVPixelFormat pix_fmts[3];

    if (NULL == avctx || NULL == avctx->priv_data) {
        av_log(avctx, AV_LOG_ERROR, "Early error in topscodec_decode_init func.\n");
        return AVERROR_BUG;
    }

    ctx        = avctx->priv_data;
    ctx->avctx = avctx;

    if (ctx->decoder_init_flag == 1) {
        av_log(avctx, AV_LOG_ERROR, "Error, cndecode double init. \n");
        return AVERROR_BUG;
    }

    /*
    DYN_DEBUG_LEVEL_DISABLE = 0
    DYN_DEBUG_LEVEL_ERR     = 1
    DYN_DEBUG_LEVEL_INFO    = 2
    DYN_DEBUG_LEVEL_DEBUG   = 3
    */
    const char* debug_level_str = getenv("DYNLINK_DEBUG_LEVEL");
    if (debug_level_str != NULL) {
        debug_level = atoi(debug_level_str);
        av_log(avctx, AV_LOG_DEBUG, " DYNLINK_DEBUG_LEVEL level: %d\n", debug_level);
    } else {
        av_log(avctx, AV_LOG_DEBUG,
               "DYNLINK_DEBUG_LEVEL environment variable"
               " is not set, default DYN_DEBUG_LEVEL_ERR\n");
    }

    dynlink_set_debug_level(debug_level);

    switch (avctx->codec->id) {
#if CONFIG_H263_TOPSCODEC_DECODER
        case AV_CODEC_ID_H263:
            ctx->codec_type = TOPSCODEC_H263;
            break;
#endif
#if CONFIG_H264_TOPSCODEC_DECODER
        case AV_CODEC_ID_H264:
            ctx->codec_type = TOPSCODEC_H264;
            break;
#endif
#if CONFIG_HEVC_TOPSCODEC_DECODER
        case AV_CODEC_ID_HEVC:
            ctx->codec_type = TOPSCODEC_HEVC;
            break;
#endif
#if CONFIG_MJPEG_TOPSCODEC_DECODER
        case AV_CODEC_ID_MJPEG:
            ctx->codec_type = TOPSCODEC_JPEG;
            break;
#endif
#if CONFIG_MPEG2_TOPSCODEC_DECODER
        case AV_CODEC_ID_MPEG2VIDEO:
            ctx->codec_type = TOPSCODEC_MPEG2;
            break;
#endif
#if CONFIG_MPEG4_TOPSCODEC_DECODER
        case AV_CODEC_ID_MPEG4:
            ctx->codec_type = TOPSCODEC_MPEG4;
            break;
#endif
#if CONFIG_VC1_TOPSCODEC_DECODER
        case AV_CODEC_ID_VC1:
            ctx->codec_type = TOPSCODEC_VC1;
            break;
#endif
#if CONFIG_VP8_TOPSCODEC_DECODER
        case AV_CODEC_ID_VP8:
            ctx->codec_type = TOPSCODEC_VP8;
            break;
#endif
#if CONFIG_VP9_TOPSCODEC_DECODER
        case AV_CODEC_ID_VP9:
            ctx->codec_type = TOPSCODEC_VP9;
            break;
#endif
#if CONFIG_AVS_TOPSCODEC_DECODER
        case AV_CODEC_ID_CAVS:
            ctx->codec_type = TOPSCODEC_AVS;
            break;
#endif
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 18, 100)
#if CONFIG_AVS2_TOPSCODEC_DECODER
        case AV_CODEC_ID_AVS2:
            ctx->codec_type = TOPSCODEC_AVS2;
            break;
#endif
#if CONFIG_AV1_TOPSCODEC_DECODER
        case AV_CODEC_ID_AV1:
            ctx->codec_type = TOPSCODEC_AV1;
            break;
#endif
#endif
        default:
            av_log(avctx, AV_LOG_ERROR, "Invalid tops codec %s\n", avcodec_descriptor_get(avctx->codec->id)->long_name);
            return AVERROR_BUG;
    }
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 18, 100)
    ctx->bsf = NULL;
    if (avctx->codec->id == AV_CODEC_ID_H264 || avctx->codec->id == AV_CODEC_ID_HEVC) {
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

        if (((ret = avcodec_parameters_from_context(ctx->bsf->par_in, avctx)) < 0) ||
            ((ret = av_bsf_init(ctx->bsf)) < 0)) {
            av_bsf_free(&ctx->bsf);
            goto error;
        }
    }
#endif
    ctx->av_pkt = av_packet_alloc();

    pix_fmts[0] = AV_PIX_FMT_TOPSCODEC;
    pix_fmts[1] = AV_PIX_FMT_YUV420P;
    pix_fmts[2] = AV_PIX_FMT_NONE;
    ret         = ff_get_format(avctx, pix_fmts);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "ff_get_format failed: %d\n", ret);
        return ret;
    }
    avctx->pix_fmt = ret;
    av_log(avctx, AV_LOG_DEBUG, "TOPSCODEC AVCTX pix fmt:%s\n", av_get_pix_fmt_name(ret));
    ctx->output_pixfmt = av_get_pix_fmt(ctx->str_output_pixfmt);
    /* sw_pix_fmt is Nominal unaccelerated pixel format.*/
    avctx->sw_pix_fmt = ctx->output_pixfmt;
    av_log(avctx, AV_LOG_DEBUG, "TOPSCODEC AVCTX sw pix fmt:%s\n", av_get_pix_fmt_name(avctx->sw_pix_fmt));
    sprintf(card_idx, "%d", ctx->card_id);
    if (avctx->hw_frames_ctx) {  // if hw_frames_ctx setted by user
        av_buffer_unref(&ctx->hwframe);
        ctx->hwframe = av_buffer_ref(avctx->hw_frames_ctx);
        if (!ctx->hwframe) {
            ret = AVERROR(EINVAL);
            goto error;
        }

        hwframe_ctx             = (AVHWFramesContext*)ctx->hwframe->data;
        hwframe_ctx->device_ctx = (AVHWDeviceContext*)hwframe_ctx->device_ref->data;
        ctx->hwdevice           = av_buffer_ref(hwframe_ctx->device_ref);
        if (!ctx->hwdevice) {
            av_log(avctx, AV_LOG_ERROR,
                   "A hardware frames or device context is"
                   "required for hardware accelerated decoding.\n");
            ret = AVERROR(EINVAL);
            goto error;
        }
    } else {
        ret = av_hwdevice_ctx_create(&ctx->hwdevice, AV_HWDEVICE_TYPE_TOPSCODEC, card_idx, NULL, 0);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Hardware device context create failed,ret(%d).\n", ret);
            goto error;
        }

        ctx->hwframe = av_hwframe_ctx_alloc(ctx->hwdevice);
        if (!ctx->hwframe) {
            av_log(avctx, AV_LOG_ERROR, "Error, av_hwframe_ctx_alloc failed.\n");

            ret = AVERROR(EINVAL);
            goto error;
        }
        hwframe_ctx           = (AVHWFramesContext*)ctx->hwframe->data;
        need_init_hwframe_ctx = 1;
        avctx->hw_frames_ctx  = av_buffer_ref(ctx->hwframe);
    }

    pthread_mutex_lock(&g_dec_mutex);
    ret = topscodec_load_functions(&ctx->topscodec_lib_ctx);
    if (ret != 0) {
        av_log(avctx, AV_LOG_ERROR, "Error, topscodec_lib_load failed, ret(%d)\n", ret);
        ret = AVERROR(EINVAL);
        pthread_mutex_unlock(&g_dec_mutex);
        goto error;
    }
    pthread_mutex_unlock(&g_dec_mutex);

    topscodec_get_version(avctx);
    memset(&ctx->caps, 0, sizeof(ctx->caps));
    /*get device caps*/
    av_log(avctx, AV_LOG_DEBUG, "topscodecDecGetCaps: type[%d],card[%d]dev[%d]\n", ctx->codec_type, ctx->card_id,
           ctx->device_id);
    ret = ctx->topscodec_lib_ctx->lib_topscodecDecGetCaps(ctx->codec_type, ctx->card_id, ctx->device_id, &ctx->caps);
    if (TOPSCODEC_SUCCESS != ret) {
        av_log(avctx, AV_LOG_ERROR, "Error, topscodecDecGetCaps failed, ret(%d)\n", ret);
        ret = AVERROR(EINVAL);
        goto error;
    }

    print_caps(avctx, &ctx->caps);
    if (!ctx->caps.supported) {
        av_log(avctx, AV_LOG_ERROR, "Unsupport tops codec %s\n", avcodec_descriptor_get(avctx->codec->id)->long_name);
        return AVERROR_BUG;
    }

    max_width  = ctx->caps.max_width;
    max_height = ctx->caps.max_height;

    probed_width  = avctx->coded_width ? avctx->coded_width : (avctx->width ? avctx->width : max_width);
    probed_height = avctx->coded_height ? avctx->coded_height : (avctx->height ? avctx->height : max_height);

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
        if (ctx->crop.top < 0 || ctx->crop.bottom < 0 || ctx->crop.left < 0 || ctx->crop.right < 0 ||
            ctx->crop.top > avctx->height || ctx->crop.left > avctx->width || ctx->crop.bottom > avctx->height ||
            ctx->crop.right > avctx->width || ctx->crop.top >= ctx->crop.bottom || ctx->crop.left >= ctx->crop.right ||
            ctx->crop.bottom - ctx->crop.top < 8 || ctx->crop.right - ctx->crop.left < 8) {
            av_log(avctx, AV_LOG_ERROR,
                   "Invalid crop "
                   "dim(lefg:%d,top:%d)(right:%d,bottom:%d)\n",
                   ctx->crop.left, ctx->crop.top, ctx->crop.right, ctx->crop.bottom);
            return AVERROR(EINVAL);
        }
        ctx->out_width  = ctx->crop.right - ctx->crop.left;
        ctx->out_height = ctx->crop.bottom - ctx->crop.top;
    }

    if (ctx->enable_resize) {
        av_log(avctx, AV_LOG_DEBUG, "Open downscale option.\n");
        if (ctx->resize.height < 0 || ctx->resize.width < 0 || ctx->resize.height > avctx->height ||
            ctx->resize.width > avctx->width) {
            av_log(avctx, AV_LOG_ERROR, "Invalid resize dim %dx%d, only support downscale.\n", ctx->resize.width,
                   ctx->resize.height);
            return AVERROR(EINVAL);
        }
        ctx->out_width  = ctx->resize.width;
        ctx->out_height = ctx->resize.height;
    }

    if (ctx->enable_rotation) {
        if (ctx->rotation == 90 || ctx->rotation == 180 || ctx->rotation == 270) {
            av_log(avctx, AV_LOG_DEBUG, "Open rotation option:%d.\n", ctx->rotation);
        } else {
            av_log(avctx, AV_LOG_ERROR, "Invalid rotaion value, only support 90/180/270\n");
            return AVERROR(EINVAL);
        }

        if (ctx->rotation == 90 || ctx->rotation == 270) {
            rotation_tmp    = ctx->out_width;
            ctx->out_width  = ctx->out_height;
            ctx->out_height = rotation_tmp;
        }
    }

    // after getting the final output width and height, init hwframe
    if (need_init_hwframe_ctx && !hwframe_ctx->pool) {
        hwframe_ctx->format            = AV_PIX_FMT_TOPSCODEC;
        hwframe_ctx->sw_format         = avctx->sw_pix_fmt;
        hwframe_ctx->width             = avctx->coded_width;   // outwidth? downscale
        hwframe_ctx->height            = avctx->coded_height;  // outheight? downscale
        hwframe_ctx->initial_pool_size = 3;                    /*TODO*/
        hwframe_ctx->pool              = NULL;                 /*TODO*/
        if ((ret = av_hwframe_ctx_init(ctx->hwframe)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error, av_hwframe_ctx_init failed, ret(%d)\n", ret);
            ret = AVERROR(EINVAL);
            goto error;
        }
    }

    ctx->hwframes_ctx        = hwframe_ctx;
    device_ctx               = hwframe_ctx->device_ctx;
    device_hwctx             = device_ctx->hwctx;
    ctx->topsruntime_lib_ctx = device_hwctx->topsruntime_lib_ctx;

    ctx->total_frame_count  = 0;
    ctx->total_packet_count = 0;
    ctx->recv_first_frame   = 0;
    ctx->draining           = 0;
    ctx->recv_outport_eos   = 0;
    ctx->first_packet       = 1;

    for (int i = 0; i < MAX_FRAME_NUM; i++) {
        ctx->last_received_frame[i] = av_frame_alloc();
    }

    /*
     * At this moment, if the demuxer does not set this value
     * (avctx->field_order == UNKNOWN),
     * the input stream will be assumed as progressive one.
     */
    switch (avctx->field_order) {
        case AV_FIELD_TT:
        case AV_FIELD_BB:
        case AV_FIELD_TB:
        case AV_FIELD_BT:
            ctx->progressive = 0;
            break;
        case AV_FIELD_PROGRESSIVE:  // fall through
        default:
            ctx->progressive = 1;
            break;
    }

    bitstream_size = ceil((probed_width * probed_height) * 1.25);

    ctx->stream_buf_size = FFALIGN(bitstream_size, 4096);
    if (!ctx->stream_addr) {
        tops_ret =
            ctx->topsruntime_lib_ctx->lib_topsExtMallocWithFlags(&tmp, ctx->stream_buf_size, topsMallocHostAccessable);
        if (topsSuccess != tops_ret) {
            av_log(avctx, AV_LOG_ERROR, "Error, topsMalloc failed, ret(%d)\n", tops_ret);
            ret = AVERROR(EPERM);
            goto error;
        }
        ctx->stream_addr = (uint64_t)tmp;
        av_log(avctx, AV_LOG_DEBUG, "malloc stream_addr:0x%lx\n", ctx->stream_addr);
        tops_ret = ctx->topsruntime_lib_ctx->lib_topsPointerGetAttributes(&att, (void*)(ctx->stream_addr));
        if (tops_ret != topsSuccess) {
            av_log(avctx, AV_LOG_ERROR, "topsPointerGetAttributes failed!\n");
            ret = AVERROR(EPERM);
            goto error;
        }
        ctx->mem_addr = (u64_t)att.device_pointer;
    }

    memset(&codec_info, 0, sizeof(topscodecDecCreateInfo_t));
    av_log(avctx, AV_LOG_DEBUG, "zero copy %d\n", ctx->zero_copy);
    codec_info.device_id       = ctx->card_id;
    codec_info.session_id      = ctx->device_id;
    codec_info.hw_ctx_id       = ctx->hw_id;
    codec_info.codec           = ctx->codec_type;
    codec_info.stream_buf_size = ctx->stream_buf_size;
    codec_info.run_mode        = TOPSCODEC_RUN_MODE_ASYNC;
    if (codec_info.codec == TOPSCODEC_VP8 || codec_info.codec == TOPSCODEC_VP9 || codec_info.codec == TOPSCODEC_AV1) {
        codec_info.send_mode = TOPSCODEC_DEC_SEND_MODE_FRAME;
    } else {
        codec_info.send_mode = TOPSCODEC_DEC_SEND_MODE_STREAM;
    }

    /*
     * sf setting
     */
    switch_frames_num       = ctx->sf;
    switch_frames_mode      = 1;
    codec_info.reserved[9]  = switch_frames_mode;
    codec_info.reserved[10] = switch_frames_num;

#ifdef TOPS_LOG
    /* ap log setting*/
    codec_info.reserved[0] = 1;
    codec_info.reserved[1] = 1;
    /* Log level */
    for (int i = 0; i < 7; i++) {
        codec_info.reserved[i + 2] = 4;
    }
#endif

    /*create codec*/
    print_create_info(avctx, &codec_info);
    ret = ctx->topscodec_lib_ctx->lib_topscodecDecCreate(&ctx->handle, &codec_info);
    if (TOPSCODEC_SUCCESS != ret) {
        av_log(avctx, AV_LOG_ERROR, "Error, topscodecDecCreate failed, ret(%d)\n", ret);
        ret = AVERROR(EINVAL);
        goto error;
    }

    memset(&params, 0, sizeof(topscodecDecParams_t));
    params.pixel_format = avpixfmt_2_topspixfmt(ctx->output_pixfmt);
    av_log(avctx, AV_LOG_DEBUG, "Out pixfmt: %s\n", av_pix_fmt_desc_get(ctx->output_pixfmt)->name);

    params.color_space = str_2_topsolorspace(ctx->color_space);
    av_log(avctx, AV_LOG_DEBUG, "Out Colorspace: %s\n", ctx->color_space);

    params.reserved[4] = ctx->input_buf_num;
    av_log(avctx, AV_LOG_DEBUG, "input_buf_num: %d\n", ctx->input_buf_num);

    params.output_buf_num = ctx->output_buf_num;
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
        av_log(avctx, AV_LOG_DEBUG, "Setting resize, %dx%d->%dx%d.\n", avctx->width, avctx->height, ctx->resize.width,
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
               avctx->width, avctx->height, ctx->crop.left, ctx->crop.top, ctx->crop.right, ctx->crop.bottom);
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
        av_log(avctx, AV_LOG_DEBUG, "Setting rotation, rotation:%d\n", ctx->rotation);
    }

    if (ctx->enable_sfo) {
        params.pp_attr.sf.enable = 1;
        if (ctx->sfo != 0)
            params.pp_attr.sf.sfo = ctx->sfo;
        else if (ctx->sf_idr != 0)
            params.pp_attr.sf.sf_idr = FF_IDR_MAGIC;

        av_log(avctx, AV_LOG_DEBUG, "Setting sampling interval value, sfo:%d,sf_idr:%d\n", ctx->sfo, FF_IDR_MAGIC);
    }

    /*set codec params*/
    ret = ctx->topscodec_lib_ctx->lib_topscodecDecSetParams(ctx->handle, &params);
    if (TOPSCODEC_SUCCESS != ret) {
        av_log(avctx, AV_LOG_ERROR, "Error, topscodecDecSetParams failed, ret(%d)\n", ret);
        ret = AVERROR(EINVAL);
        goto error;
    }

    ctx->ef_buf_pkt = av_malloc(sizeof(EFBuffer));
    memset(ctx->ef_buf_pkt, 0, sizeof(EFBuffer));
    ctx->ef_buf_pkt->avctx            = avctx;
    ctx->ef_buf_pkt->ef_context       = ctx;
    ctx->ef_buf_pkt->ef_pkt.mem_addr  = 0;
    ctx->ef_buf_pkt->ef_pkt.alloc_len = 0;
    for (int i = 0; i < MAX_FRAME_NUM; i++) {
        ctx->ef_buf_frame[i] = av_malloc(sizeof(EFBuffer));
        memset(ctx->ef_buf_frame[i], 0, sizeof(EFBuffer));
        ctx->ef_buf_frame[i]->avctx      = avctx;
        ctx->ef_buf_frame[i]->ef_context = ctx;
    }

    if (!avctx->pkt_timebase.num || !avctx->pkt_timebase.den)
        av_log(avctx, AV_LOG_DEBUG, "Invalid pkt_timebase, passing timestamps as-is.\n");

    ctx->decoder_init_flag = 1;
    av_log(avctx, AV_LOG_DEBUG, "Thread: %lu, decoder init done\n", (long unsigned)pthread_self());
    return 0;

error:
    return ret;
}

static av_cold int topscodec_decode_close(AVCodecContext* avctx) {
    EFCodecDecContext_t* ctx;
    if (NULL == avctx || NULL == avctx->priv_data) {
        return AVERROR_BUG;
    }
    ctx = (EFCodecDecContext_t*)avctx->priv_data;
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 18, 100)
    if (ctx->bsf) av_bsf_free(&ctx->bsf);
#endif
    if (ctx->av_pkt) av_packet_free(&ctx->av_pkt);

    if (ctx->handle) {
        /*destory codec dec*/
        ctx->topscodec_lib_ctx->lib_topscodecDecDestroy(ctx->handle);
        ctx->handle = 0;
        av_log(avctx, AV_LOG_DEBUG, "topscodecDecDestroy success\n");
    }

    if (ctx->stream_addr) {
        ctx->topsruntime_lib_ctx->lib_topsFree((void*)ctx->stream_addr);
        ctx->stream_addr = 0;
        av_log(avctx, AV_LOG_DEBUG, "topsFree stream_addr success\n");
    }

    if (ctx->ef_buf_pkt) {
        av_free(ctx->ef_buf_pkt);
        av_log(avctx, AV_LOG_DEBUG, "ef_buf_pkt free\n");
    }
    for (int i = 0; i < MAX_FRAME_NUM; i++) {
        if (ctx->ef_buf_frame[i]) {
            av_free(ctx->ef_buf_frame[i]);
            av_log(avctx, AV_LOG_DEBUG, "ef_buf_frame[%d] free\n", i);
        }
    }

    if (ctx->topscodec_lib_ctx) {
        pthread_mutex_lock(&g_dec_mutex);
        topscodec_free_functions(&ctx->topscodec_lib_ctx);
        av_log(avctx, AV_LOG_DEBUG, "topscodec_free_functions success\n");
        pthread_mutex_unlock(&g_dec_mutex);
    }

    if (ctx->hwdevice) {
        av_buffer_unref(&ctx->hwdevice);
        av_log(avctx, AV_LOG_DEBUG, "hwdevice unref\n");
    }

    if (ctx->hwframe) {
        av_buffer_unref(&ctx->hwframe);
        av_log(avctx, AV_LOG_DEBUG, "hwframe unref\n");
    }

    for (int i = 0; i < MAX_FRAME_NUM; i++) {
        if (ctx->last_received_frame[i]) {
            av_frame_free(&ctx->last_received_frame[i]);
        }
    }
    ctx->decoder_init_flag = 0;
    av_log(avctx, AV_LOG_DEBUG, "Thread, %lu, decode close \n", (long unsigned)pthread_self());
    return 0;
}

static int topscodec_recived_helper(AVCodecContext* avctx, AVFrame* avframe, int is_internel) {
    int ret = 0;
    int idx = 0;

    EFCodecDecContext_t* ctx = (EFCodecDecContext_t*)avctx->priv_data;
    av_frame_unref(avframe);  // fix me

    av_log(avctx, AV_LOG_DEBUG, "is_internel:%d, get:%d, put:%d\n", is_internel, ctx->idx_get, ctx->idx_put);
    if (is_internel != 1 && ctx->idx_put != ctx->idx_get) {
        av_frame_ref(avframe, ctx->last_received_frame[ctx->idx_get]);
        av_frame_unref(ctx->last_received_frame[ctx->idx_get]);
        ctx->idx_get = (ctx->idx_get + 1) % MAX_FRAME_NUM;
        av_log(avctx, AV_LOG_DEBUG, "Get frame ,get:%d, put:%d\n", ctx->idx_get, ctx->idx_put);
        return 0;
    }
    idx = ctx->idx_put;
    ret = ctx->topscodec_lib_ctx->lib_topscodecDecFrameMap(ctx->handle, &ctx->ef_buf_frame[idx]->ef_frame);
    /*End of file*/
    if (TOPSCODEC_SUCCESS == ret) {
        if (ctx->draining &&
            (0 == ctx->ef_buf_frame[idx]->ef_frame.width || 0 == ctx->ef_buf_frame[idx]->ef_frame.height)) {
            av_log(avctx, AV_LOG_DEBUG, "----EOS -----\n");
            ctx->recv_outport_eos = 1;
            av_usleep(10);
            return AVERROR_EOF;
        }
        print_frame(avctx, &ctx->ef_buf_frame[idx]->ef_frame);
        av_log(avctx, AV_LOG_DEBUG, "topscodecDecFrameMap success\n");
    } else if (TOPSCODEC_ERROR_BUFFER_EMPTY == ret) {
        av_log(avctx, AV_LOG_DEBUG, "TOPSCODEC_ERROR_BUFFER_EMPTY\n");
        return AVERROR(EAGAIN);
    } else {
        av_log(avctx, AV_LOG_ERROR, "topscodecDecFrameMap failed, ret(%d)\n", ret);
        return AVERROR(EPERM);
    }

    if (!ctx->recv_first_frame) ctx->recv_first_frame = 1;

    ctx->total_frame_count++;
    ret = ff_decode_frame_props(avctx, avframe);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "ff_decode_frame_props failed\n");
        return AVERROR_BUG;
    }

    if (avctx->pix_fmt == AV_PIX_FMT_TOPSCODEC) {
        ctx->ef_buf_frame[idx]->avctx      = avctx;
        ctx->ef_buf_frame[idx]->ef_context = ctx;
        ret                                = ff_topscodec_efbuf_to_avframe(ctx->ef_buf_frame[idx], avframe);
        if (ret < 0) return AVERROR_BUG;
    } else {
        ctx->ef_buf_frame[idx]->avctx      = avctx;
        ctx->ef_buf_frame[idx]->ef_context = ctx;
        ret                                = ff_topscodec_efbuf_to_avframe(ctx->ef_buf_frame[idx], &ctx->mid_frame);
        if (ret < 0) return AVERROR_BUG;
        avframe->format = topspixfmt_2_avpixfmt(ctx->ef_buf_frame[idx]->ef_frame.pixel_format);
        ret             = av_hwframe_transfer_data(avframe, &ctx->mid_frame, 0);
        if (ret) {
            av_log(avctx, AV_LOG_ERROR, "av_hwframe_transfer_data failed\n");
            av_frame_unref(&ctx->mid_frame);
            return AVERROR_BUG;
        }
        av_frame_unref(&ctx->mid_frame);
    }
    return ret;
}

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 18, 100)
static int topscodec_decode(AVCodecContext* avctx, void* data, int* got_frame, AVPacket* avpkt) {
    EFCodecDecContext_t* ctx             = NULL;
    AVFrame*             frame           = data;
    AVPacket             filter_packet   = {0};
    AVPacket             filtered_packet = {0};
    int                  ret             = 0;
    int                  ret2            = 0;
    int                  sleep_handle    = 0;
    int                  input_eos       = 0;

    if (NULL == avctx || NULL == avctx->priv_data) {
        av_log(avctx, AV_LOG_ERROR, "Early error in topscodec_receive_frame\n");
        return AVERROR_BUG;
    }
    ctx = (EFCodecDecContext_t*)avctx->priv_data;

    if (!ctx->decoder_init_flag) {
        av_log(avctx, AV_LOG_ERROR, "Decode got abort or not init, return AVERROR_EXTERNAL \n");
        return AVERROR_BUG;
    }

    if (ctx->recv_outport_eos) {
        return AVERROR_EOF;
    }

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

    ctx->ef_buf_pkt->avctx      = avctx;
    ctx->ef_buf_pkt->ef_context = ctx;
    /*when avpkt.size==0, means eof*/
    av_log(avctx, AV_LOG_DEBUG, "topscodecDecodeStream,pkt_size=%d\n", avpkt->size);
    if (ctx->first_packet) {
        if (avctx->extradata_size) {
            AVPacket p;
            p.data = avctx->extradata;
            p.size = avctx->extradata_size;
            p.pts  = 0;
            ff_topscodec_avpkt_to_efbuf(&p, ctx->ef_buf_pkt);
            print_stream(avctx, &ctx->ef_buf_pkt->ef_pkt);
            do {
                ret = ctx->topscodec_lib_ctx->lib_topscodecDecodeStream(ctx->handle, &ctx->ef_buf_pkt->ef_pkt,
                                                                        0); /*timeout is 0*/
                if (ret != TOPSCODEC_SUCCESS) {
                    if (ret == TOPSCODEC_ERROR_TIMEOUT) {
                        av_log(avctx, AV_LOG_DEBUG, "topscodecDecodeStream timeout,retry again!\n");
                        sleep_wait(&sleep_handle);
                    } else {
                        av_log(avctx, AV_LOG_ERROR, "topscodecDecSendStream failed. ret = %d\n", ret);
                        goto fail;
                    }
                }
            } while (ret == TOPSCODEC_ERROR_TIMEOUT);
        }
        ctx->first_packet = 0;
    }
    ff_topscodec_avpkt_to_efbuf(avpkt, ctx->ef_buf_pkt);
    ctx->total_packet_count++;
    print_stream(avctx, &ctx->ef_buf_pkt->ef_pkt);
    do {
        ret = ctx->topscodec_lib_ctx->lib_topscodecDecodeStream(ctx->handle, &ctx->ef_buf_pkt->ef_pkt,
                                                                0); /*timeout is 0*/
        if (ret != TOPSCODEC_SUCCESS) {
            if (ret == TOPSCODEC_ERROR_TIMEOUT) {
                // last_received_frame array is not full
                AVFrame* tmp = ctx->last_received_frame[ctx->idx_put];
                if (ctx->idx_get - ctx->idx_put != 1 && ctx->idx_get - ctx->idx_put != -(MAX_FRAME_NUM - 2)) {
                    ret2 = topscodec_recived_helper(avctx, tmp, 1);
                    if (0 == ret2) {
                        ctx->idx_put = (ctx->idx_put + 1) % MAX_FRAME_NUM;
                        av_log(avctx, AV_LOG_DEBUG, "add frame to queue,put:%d,get:%d!\n", ctx->idx_put, ctx->idx_get);
                    } else if (AVERROR(EAGAIN) == ret2) {
                        // do nothing
                        av_usleep(2);
                        av_log(avctx, AV_LOG_DEBUG, "TOPSCODEC_ERROR_BUFFER_EMPTY22\n");
                    } else {
                        av_log(avctx, AV_LOG_ERROR, "topscodec_recived_helper failed. ret = %d\n", ret2);
                        goto fail;
                    }
                }
                av_log(avctx, AV_LOG_DEBUG, "topscodecDecodeStream timeout,retry again!\n");
                sleep_wait(&sleep_handle);
            } else {
                av_log(avctx, AV_LOG_ERROR, "topscodecDecSendStream failed. ret = %d\n", ret);
                goto fail;
            }
        } else {
            av_log(avctx, AV_LOG_DEBUG, "topscodecDecodeStream success\n");
        }
    } while (ret == TOPSCODEC_ERROR_TIMEOUT);

    if (!avpkt->size) {
        ctx->draining = 1;
        input_eos     = 1;
    }
    av_packet_unref(avpkt);
recv:
    ret = topscodec_recived_helper(avctx, frame, 0);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        if (input_eos) {
            goto recv;
        } else {
            *got_frame = 0;
        }
    } else if (ret < 0) {
        return ret;
    } else {
        *got_frame = 1;
        av_log(avctx, AV_LOG_DEBUG, "topscodec got_frame, got_frame:%d\n", *got_frame);
    }
    return ret;
fail:
    av_log(avctx, AV_LOG_DEBUG, "topscodec_receive_frame,fail.\n");
    return AVERROR_BUG;
}
#endif

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 100, 100) //n4.0
static int topscodec_receive_frame(AVCodecContext* avctx, AVFrame* frame) {
    EFCodecDecContext_t* ctx;
    int                  ret, ret2;
    int                  sleep_handle = 0;

    if (NULL == avctx || NULL == avctx->priv_data) {
        av_log(avctx, AV_LOG_ERROR, "Early error in topscodec_receive_frame\n");
        return AVERROR_BUG;
    }
    ctx = (EFCodecDecContext_t*)avctx->priv_data;

    if (!ctx->decoder_init_flag) {
        av_log(avctx, AV_LOG_ERROR, "Decode got abort or not init, return AVERROR_EXTERNAL \n");
        return AVERROR_BUG;
    }

    if (ctx->recv_outport_eos) {
        return AVERROR_EOF;
    }

    if (!ctx->av_pkt->size) {
        ret = ff_decode_get_packet(avctx, ctx->av_pkt);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN)) {
                return topscodec_recived_helper(avctx, frame, 0);
            } else if (ret != AVERROR_EOF) {
                return ret;
            }
        }
    }

    if (ctx->draining) goto dequeue;

    // if (!ctx->av_pkt->size && !ctx->recv_first_frame)
    //     goto dequeue;

    ctx->ef_buf_pkt->avctx      = avctx;
    ctx->ef_buf_pkt->ef_context = ctx;
    /*when avpkt.size==0, means eof*/
    av_log(avctx, AV_LOG_DEBUG, "topscodecDecodeStream,pkt_size=%d\n", ctx->av_pkt->size);
    if (ctx->first_packet) {
        if (avctx->extradata_size) {
            AVPacket p;
            p.data = avctx->extradata;
            p.size = avctx->extradata_size;
            p.pts  = 0;
            ff_topscodec_avpkt_to_efbuf(&p, ctx->ef_buf_pkt);
            print_stream(avctx, &ctx->ef_buf_pkt->ef_pkt);
            do {
                ret = ctx->topscodec_lib_ctx->lib_topscodecDecodeStream(ctx->handle, &ctx->ef_buf_pkt->ef_pkt,
                                                                        0); /*timeout is 0*/
                if (ret != TOPSCODEC_SUCCESS) {
                    if (ret == TOPSCODEC_ERROR_TIMEOUT) {
                        av_log(avctx, AV_LOG_DEBUG, "topscodecDecodeStream timeout,retry again!\n");
                        sleep_wait(&sleep_handle);
                    } else {
                        av_log(avctx, AV_LOG_ERROR, "topscodecDecSendStream failed. ret = %d\n", ret);
                        goto fail;
                    }
                }
            } while (ret == TOPSCODEC_ERROR_TIMEOUT);
        }
        ctx->first_packet = 0;
    }
    ff_topscodec_avpkt_to_efbuf(ctx->av_pkt, ctx->ef_buf_pkt);
    ctx->total_packet_count++;
    print_stream(avctx, &ctx->ef_buf_pkt->ef_pkt);
    do {
        ret = ctx->topscodec_lib_ctx->lib_topscodecDecodeStream(ctx->handle, &ctx->ef_buf_pkt->ef_pkt,
                                                                0); /*timeout is 0*/
        if (ret != TOPSCODEC_SUCCESS) {
            if (ret == TOPSCODEC_ERROR_TIMEOUT) {
                // last_received_frame array is not full
                AVFrame* tmp = ctx->last_received_frame[ctx->idx_put];
                if (ctx->idx_get - ctx->idx_put != 1 && ctx->idx_get - ctx->idx_put != -(MAX_FRAME_NUM - 2)) {
                    ret2 = topscodec_recived_helper(avctx, tmp, 1);
                    if (0 == ret2) {
                        ctx->idx_put = (ctx->idx_put + 1) % MAX_FRAME_NUM;
                        av_log(avctx, AV_LOG_DEBUG, "add frame to queue,put:%d,get:%d!\n", ctx->idx_put, ctx->idx_get);
                    } else if (AVERROR(EAGAIN) == ret2) {
                        // do nothing
                        av_usleep(2);
                        av_log(avctx, AV_LOG_DEBUG, "TOPSCODEC_ERROR_BUFFER_EMPTY22\n");
                    } else {
                        av_log(avctx, AV_LOG_ERROR, "topscodec_recived_helper failed. ret = %d\n", ret2);
                        goto fail;
                    }
                }
                av_log(avctx, AV_LOG_DEBUG, "topscodecDecodeStream timeout,retry again!\n");
                sleep_wait(&sleep_handle);
            } else {
                av_log(avctx, AV_LOG_ERROR, "topscodecDecSendStream failed. ret = %d\n", ret);
                goto fail;
            }
        } else {
            av_log(avctx, AV_LOG_DEBUG, "topscodecDecodeStream success\n");
        }
    } while (ret == TOPSCODEC_ERROR_TIMEOUT);

    if (!ctx->av_pkt->size) {
        ctx->draining = 1;
    }
    av_packet_unref(ctx->av_pkt);

dequeue:
    return topscodec_recived_helper(avctx, frame, 0);

fail:
    av_log(avctx, AV_LOG_DEBUG, "topscodec_receive_frame,fail.\n");
    return AVERROR_BUG;
}
#else
static int topscodec_receive_frame(AVCodecContext* avctx, AVFrame* frame) {
    av_log(avctx, AV_LOG_DEBUG, "topscodec_recived_helper===>\n");
    return topscodec_recived_helper(avctx, frame, 0);
}
#endif

#define OFFSET(x) offsetof(EFCodecDecContext_t, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
#define DEFAULT 0
#define MAX_DEVICE_ID (32)

static const AVOption options[] = {
    {"card_id",
     "use to choose the accelerator card",
     OFFSET(card_id),
     AV_OPT_TYPE_INT,
     {.i64 = 0},
     0,
     MAX_DEVICE_ID,
     VD},
    {"device_id",
     "use to choose the accelerator device",
     OFFSET(device_id),
     AV_OPT_TYPE_INT,
     {.i64 = 0},
     0,
     MAX_DEVICE_ID,
     VD},
    {"hw_id", "use to choose the accelerator id", OFFSET(hw_id), AV_OPT_TYPE_INT, {.i64 = 15}, 0, 100, VD},
    {"in_w", "video width", OFFSET(in_width), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, VD},
    {"in_h", "video height", OFFSET(in_height), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, VD},
    {"sf", "use to choose the switch ratio", OFFSET(sf), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 500, VD},
    {"out_port_num", "decode outport buf num", OFFSET(output_buf_num), AV_OPT_TYPE_INT, {.i64 = 8}, 0, 100, VD},
    {"in_port_num", "decode inport buf num", OFFSET(input_buf_num), AV_OPT_TYPE_INT, {.i64 = 8}, 0, 100, VD},
    {"zero_copy",
     "copy the decoded image to the hw frame buffer(D2D)",
     OFFSET(zero_copy),
     AV_OPT_TYPE_BOOL,
     {.i64 = 1},
     -1,
     1,
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
    {"enable_crop", "Forces open the crop", OFFSET(enable_crop), AV_OPT_TYPE_BOOL, {.i64 = 0}, -1, 1, VD},
    {"enable_resize",
     "Forces open the resize,only support downscale",
     OFFSET(enable_resize),
     AV_OPT_TYPE_BOOL,
     {.i64 = 0},
     -1,
     1,
     VD},
    {"enable_sfo", "enable frame sampling interval", OFFSET(enable_sfo), AV_OPT_TYPE_BOOL, {.i64 = 0}, -1, 1, VD},
    {"crop_top", "out top (crop)", OFFSET(crop.top), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, VD},
    {"crop_bottom", "out bottom(crop)", OFFSET(crop.bottom), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, VD},
    {"crop_left", "out left(crop)", OFFSET(crop.left), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, VD},
    {"crop_right", "out right(crop)", OFFSET(crop.right), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, VD},
    {"resize_w", "out width(resize)", OFFSET(resize.width), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, VD},
    {"resize_h", "out height(resize)", OFFSET(resize.height), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, VD},
    {"resize_m", "0-Bilinear, 1-Nearest", OFFSET(resize.mode), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, VD},
    {"sfo", "frame sampling interval value", OFFSET(sfo), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT_MAX, VD},
    {"idr", "frame sampling interval value", OFFSET(sf_idr), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, VD},
    {NULL},
};


#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 18, 100)
static const AVCodecHWConfigInternal* topscodec_hw_configs[] = {
    &(const AVCodecHWConfigInternal){
        .public  = {.pix_fmt     = AV_PIX_FMT_TOPSCODEC,
                   .methods     = AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX | AV_CODEC_HW_CONFIG_METHOD_INTERNAL,
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

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 18, 100) //n3.x
#define TOPSCODECDEC(NAME, LONGNAME, CODEC, BSF_NAME)                                                                \
    TOPSCODEC_CLASS(NAME)                                                                                            \
    AVHWAccel ff_##NAME##_topscodec_hwaccel = {                                                                      \
        .name    = #NAME "_topscodec",                                                                               \
        .type    = AVMEDIA_TYPE_VIDEO,                                                                               \
        .id      = CODEC,                                                                                            \
        .pix_fmt = AV_PIX_FMT_TOPSCODEC,                                                                             \
    };                                                                                                               \
    AVCodec ff_##NAME##_topscodec_decoder = {                                                                        \
        .name           = #NAME "_topscodec",                                                                        \
        .long_name      = NULL_IF_CONFIG_SMALL(#NAME "TOPSCODEC"),                                                   \
        .type           = AVMEDIA_TYPE_VIDEO,                                                                        \
        .id             = CODEC,                                                                                     \
        .priv_data_size = sizeof(EFCodecDecContext_t),                                                               \
        .priv_class     = &topscodec_##NAME##_dec_class,                                                             \
        .init           = topscodec_decode_init,                                                                     \
        .decode         = topscodec_decode,                                                                          \
        .close          = topscodec_decode_close,                                                                    \
        .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AVOID_PROBING,                                           \
        .caps_internal  = FF_CODEC_CAP_SETS_PKT_DTS | FF_CODEC_CAP_INIT_CLEANUP,                                     \
        .pix_fmts =                                                                                                  \
            (const enum AVPixelFormat[]){AV_PIX_FMT_TOPSCODEC, AV_PIX_FMT_YUV420P, AV_PIX_FMT_NV12, AV_PIX_FMT_NV21, \
                                         AV_PIX_FMT_RGB24, AV_PIX_FMT_RGB24P, AV_PIX_FMT_BGR24, AV_PIX_FMT_BGR24P,   \
                                         AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV444P10LE, AV_PIX_FMT_P010LE_EF,           \
                                         AV_PIX_FMT_P010LE, AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE},                      \
    }

#elif LIBAVCODEC_VERSION_INT <= AV_VERSION_INT(59, 18, 100) // n4.x
#define TOPSCODECDEC(NAME, LONGNAME, CODEC, BSF_NAME)                                                                \
    TOPSCODEC_CLASS(NAME)                                                                                            \
    const AVCodec ff_##NAME##_topscodec_decoder = {                                                                  \
        .name           = #NAME "_topscodec",                                                                        \
        .long_name      = NULL_IF_CONFIG_SMALL(#NAME "TOPSCODEC"),                                                   \
        .type           = AVMEDIA_TYPE_VIDEO,                                                                        \
        .id             = CODEC,                                                                                     \
        .priv_data_size = sizeof(EFCodecDecContext_t),                                                               \
        .priv_class     = &topscodec_##NAME##_dec_class,                                                             \
        .init           = topscodec_decode_init,                                                                     \
        .receive_frame  = topscodec_receive_frame,                                                                   \
        .close          = topscodec_decode_close,                                                                    \
        .bsfs           = BSF_NAME,                                                                                  \
        .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE | AV_CODEC_CAP_AVOID_PROBING,                   \
        .caps_internal  = FF_CODEC_CAP_SETS_PKT_DTS | FF_CODEC_CAP_INIT_CLEANUP,                                     \
        .pix_fmts =                                                                                                  \
            (const enum AVPixelFormat[]){AV_PIX_FMT_TOPSCODEC, AV_PIX_FMT_YUV420P, AV_PIX_FMT_NV12, AV_PIX_FMT_NV21, \
                                         AV_PIX_FMT_RGB24, AV_PIX_FMT_RGB24P, AV_PIX_FMT_BGR24, AV_PIX_FMT_BGR24P,   \
                                         AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV444P10LE, AV_PIX_FMT_P010LE_EF,           \
                                         AV_PIX_FMT_P010LE, AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY10, AV_PIX_FMT_NONE},   \
        .hw_configs   = topscodec_hw_configs,                                                                        \
        .wrapper_name = "topscodec",                                                                                 \
    }
#else
#define TOPSCODECDEC(NAME, LONGNAME, CODEC, BSF_NAME)                                                                \
    TOPSCODEC_CLASS(NAME)                                                                                            \
    const FFCodec ff_##NAME##_topscodec_decoder = {                                                                  \
        .p.name           = #NAME "_topscodec",                                                                      \
        .p.long_name      = NULL_IF_CONFIG_SMALL(#NAME "TOPSCODEC"),                                                 \
        .p.type           = AVMEDIA_TYPE_VIDEO,                                                                      \
        .p.id             = CODEC,                                                                                   \
        .priv_data_size   = sizeof(EFCodecDecContext_t),                                                             \
        .p.priv_class     = &topscodec_##NAME##_dec_class,                                                           \
        .init             = topscodec_decode_init,                                                                   \
        .cb_type          = FF_CODEC_CB_TYPE_RECEIVE_FRAME,                                                          \
        .cb.receive_frame = topscodec_receive_frame,                                                                 \
        .close            = topscodec_decode_close,                                                                  \
        .bsfs             = BSF_NAME,                                                                                \
        .p.capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_HARDWARE | AV_CODEC_CAP_AVOID_PROBING,                 \
        .caps_internal    = FF_CODEC_CAP_SETS_PKT_DTS | FF_CODEC_CAP_INIT_CLEANUP,                                   \
        .p.pix_fmts =                                                                                                \
            (const enum AVPixelFormat[]){AV_PIX_FMT_TOPSCODEC, AV_PIX_FMT_YUV420P, AV_PIX_FMT_NV12, AV_PIX_FMT_NV21, \
                                         AV_PIX_FMT_RGB24, AV_PIX_FMT_RGB24P, AV_PIX_FMT_BGR24, AV_PIX_FMT_BGR24P,   \
                                         AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV444P10LE, AV_PIX_FMT_P010LE_EF,           \
                                         AV_PIX_FMT_P010LE, AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY10, AV_PIX_FMT_NONE},   \
        .hw_configs     = topscodec_hw_configs,                                                                      \
        .p.wrapper_name = "topscodec",                                                                               \
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