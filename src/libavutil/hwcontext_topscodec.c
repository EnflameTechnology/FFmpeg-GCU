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
 */

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include "libavutil/buffer.h"
#include "libavutil/common.h"
#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_internal.h"
#include "libavutil/hwcontext_topscodec.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"
#include "libavutil/version.h"

pthread_mutex_t av_tops_lib_mutex = PTHREAD_MUTEX_INITIALIZER;

#if LIBAVUTIL_VERSION_INT < \
    AV_VERSION_INT(56, 14, 100)  // n3.x do not support AV_PIX_FMT_GRAY10BE
static const enum AVPixelFormat supported_formats[] = {
    AV_PIX_FMT_YUV420P,     AV_PIX_FMT_YUV420P10LE, AV_PIX_FMT_NV12,
    AV_PIX_FMT_NV21,        AV_PIX_FMT_RGB24,       AV_PIX_FMT_RGB24P,
    AV_PIX_FMT_BGR24,       AV_PIX_FMT_BGR24P,      AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV444P10LE, AV_PIX_FMT_P010LE_LSB,  AV_PIX_FMT_P010LE,
    AV_PIX_FMT_GRAY8};
#else
static const enum AVPixelFormat supported_formats[] = {
    AV_PIX_FMT_YUV420P,     AV_PIX_FMT_YUV420P10LE, AV_PIX_FMT_NV12,
    AV_PIX_FMT_NV21,        AV_PIX_FMT_RGB24,       AV_PIX_FMT_RGB24P,
    AV_PIX_FMT_BGR24,       AV_PIX_FMT_BGR24P,      AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV444P10LE, AV_PIX_FMT_P010LE_LSB,  AV_PIX_FMT_P010LE,
    AV_PIX_FMT_GRAY8,       AV_PIX_FMT_GRAY10LE};
#endif

static int topscodec_frames_get_constraints(
    AVHWDeviceContext* ctx, const void* hwconfig,
    AVHWFramesConstraints* constraints) {
    int i;

    constraints->valid_sw_formats =
        av_malloc_array(FF_ARRAY_ELEMS(supported_formats) + 1,
                        sizeof(*constraints->valid_sw_formats));
    if (!constraints->valid_sw_formats) return AVERROR(ENOMEM);

    for (i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++)
        constraints->valid_sw_formats[i] = supported_formats[i];

    constraints->valid_sw_formats[FF_ARRAY_ELEMS(supported_formats)] =
        AV_PIX_FMT_NONE;

    constraints->valid_hw_formats =
        av_malloc_array(2, sizeof(*constraints->valid_hw_formats));
    if (!constraints->valid_hw_formats) return AVERROR(ENOMEM);

    constraints->valid_hw_formats[0] = AV_PIX_FMT_TOPSCODEC;
    constraints->valid_hw_formats[1] = AV_PIX_FMT_NONE;

    return 0;
}

static void topscodec_buffer_free(void* opaque, uint8_t* data) {
    AVHWFramesContext*      ctx        = (AVHWFramesContext*)opaque;
    AVHWDeviceContext*      device_ctx = ctx->device_ctx;
    TOPSCodecDeviceContext* tops_ctx   = device_ctx->hwctx;
    tops_ctx->topsruntime_lib_ctx->lib_topsFree((void*)data);
    av_log(ctx, AV_LOG_DEBUG, "pool buffer topsFree.\n");
}
#if LIBAVUTIL_VERSION_INT < AV_VERSION_INT(59, 8, 100)  // 7.0
static AVBufferRef* topscodec_pool_alloc(void* opaque, int size) {
#else
static AVBufferRef* topscodec_pool_alloc(void* opaque, size_t size) {
#endif
    AVHWFramesContext*      ctx        = (AVHWFramesContext*)opaque;
    AVHWDeviceContext*      device_ctx = ctx->device_ctx;
    TOPSCodecDeviceContext* tops_ctx   = device_ctx->hwctx;

    AVBufferRef* ref  = NULL;
    void*        data = NULL;
    int          ret  = 0;

    ret = tops_ctx->topsruntime_lib_ctx->lib_topsMalloc(&data, size);
    if (ret != topsSuccess) {
#if LIBAVUTIL_VERSION_INT < AV_VERSION_INT(59, 8, 100)  // 7.0
        av_log(ctx, AV_LOG_ERROR,
               "topscodec_malloc failed: dev addr %p, size %d \n", data, size);
#else
        av_log(ctx, AV_LOG_ERROR,
               "topscodec_malloc failed: dev addr %p, size %ld \n", data, size);
#endif
        return NULL;
    }

#if LIBAVUTIL_VERSION_INT < AV_VERSION_INT(59, 8, 100)  // 7.0
    av_log(ctx, AV_LOG_DEBUG, "pool topsMalloc size:%d, addr:%p\n", size, data);
#else
    av_log(ctx, AV_LOG_DEBUG, "pool topsMalloc size:%ld, addr:%p\n", size,
           data);
#endif
    ref = av_buffer_create((uint8_t*)data, size, topscodec_buffer_free, ctx, 0);
    if (!ref) {
        tops_ctx->topsruntime_lib_ctx->lib_topsFree(data);
    }
    return ref;
}

static int topscodec_frames_init(AVHWFramesContext* ctx) {
    int                     i;
    AVHWDeviceContext*      device_ctx = ctx->device_ctx;
    TOPSCodecDeviceContext* tops_ctx   = device_ctx->hwctx;

    for (i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++) {
        if (ctx->sw_format == supported_formats[i]) break;
    }
    if (i == FF_ARRAY_ELEMS(supported_formats)) {
        av_log(ctx, AV_LOG_ERROR, "Pixel format '%s' is not supported\n",
               av_get_pix_fmt_name(ctx->sw_format));
        return AVERROR(ENOSYS);
    }

    if (!ctx->pool) {
        int size = av_image_get_buffer_size(
            ctx->sw_format, ctx->width, ctx->height, tops_ctx->stride_align);
        if (size < 0) return size;
#if LIBAVUTIL_VERSION_INT < AV_VERSION_INT(59, 8, 100)  // 7.0
        ctx->internal->pool_internal =
            av_buffer_pool_init2(size, ctx, topscodec_pool_alloc, NULL);
        if (!ctx->internal->pool_internal) return AVERROR(ENOMEM);
#else
        ffhwframesctx(ctx)->pool_internal =
            av_buffer_pool_init2(size, ctx, topscodec_pool_alloc, NULL);
        if (!ffhwframesctx(ctx)->pool_internal) return AVERROR(ENOMEM);
#endif
    }

    return 0;
}

static int topscodec_get_buffer(AVHWFramesContext* ctx, AVFrame* frame) {
    int                     res;
    AVHWDeviceContext*      device_ctx = ctx->device_ctx;
    TOPSCodecDeviceContext* tops_ctx   = device_ctx->hwctx;
    /*when unref frame buf, buf[0] can unref pool buf*/
    frame->buf[0] = av_buffer_pool_get(ctx->pool);
    if (!frame->buf[0]) return AVERROR(ENOMEM);

    res = av_image_fill_arrays(frame->data, frame->linesize,
                               frame->buf[0]->data, ctx->sw_format, ctx->width,
                               ctx->height, tops_ctx->stride_align);
    if (res < 0) return res;

    // frame->format = ctx->sw_format;  // AV_PIX_FMT_TOPSCODEC;/*hw pixel
    // format*/
    frame->format = AV_PIX_FMT_TOPSCODEC;
    frame->width  = ctx->width;
    frame->height = ctx->height;

    return 0;
}

static int topscodec_transfer_get_formats(AVHWFramesContext*              ctx,
                                          enum AVHWFrameTransferDirection dir,
                                          enum AVPixelFormat** formats) {
    enum AVPixelFormat* fmts;

    fmts = av_malloc_array(2, sizeof(*fmts));
    if (!fmts) return AVERROR(ENOMEM);

    fmts[0] = ctx->sw_format;
    fmts[1] = AV_PIX_FMT_NONE;

    *formats = fmts;

    return 0;
}

static int topscodec_transfer_data(AVHWFramesContext* ctx, AVFrame* dst,
                                   const AVFrame* src) {
    int                     ret;
    AVHWDeviceContext*      device_ctx = ctx->device_ctx;
    TOPSCodecDeviceContext* tops_ctx   = device_ctx->hwctx;
    int                     src_format;
    topsError_t             tops_ret;
    size_t                  size;
    int                     plans;
    // int                     linesizes[4];
    ptrdiff_t linesizes1[4];
    size_t    planesizes[4];

    if (!dst || !src) {
        av_log(ctx, AV_LOG_ERROR,
               "topscodec_transfer_data_from failed,dst/src is NULL.\n");
        return AVERROR(ENOSYS);
    }

    if ((src->hw_frames_ctx &&
         ((AVHWFramesContext*)src->hw_frames_ctx->data)->format !=
             AV_PIX_FMT_TOPSCODEC) &&
        (dst->hw_frames_ctx &&
         ((AVHWFramesContext*)dst->hw_frames_ctx->data)->format !=
             AV_PIX_FMT_TOPSCODEC)) {
        av_log(ctx, AV_LOG_ERROR,
               "topscodec_transfer_data_from failed,src/dst format err[%s].\n",
               av_get_pix_fmt_name(
                   ((AVHWFramesContext*)src->hw_frames_ctx->data)->format));
        return AVERROR(ENOSYS);
    }

    if (src->hw_frames_ctx && !src->data[0]) {
        av_log(ctx, AV_LOG_ERROR,
               "topscodec_transfer_data_from failed,src data size is zero.\n");
        return AVERROR(ENOSYS);
    }

    if (dst->hw_frames_ctx && !dst->data[0]) {
        av_log(ctx, AV_LOG_ERROR,
               "topscodec_transfer_data_from failed,dst data size is zero.\n");
        return AVERROR(ENOSYS);
    }

    if (src->hw_frames_ctx) {
        src_format = ((AVHWFramesContext*)src->hw_frames_ctx->data)->sw_format;

        av_log(ctx, AV_LOG_DEBUG, "1-src format:%d, w:%d, h:%d\n", src_format,
               src->width, src->height);

        // ret = av_image_fill_linesizes(linesizes, src_format, src->width);
        // if (ret < 0) {
        //     av_log(ctx, AV_LOG_ERROR,
        //            "tran data av_image_fill_linesizes failed.\n");
        //     return AVERROR(ENOSYS);
        // }
        for (int i = 0; i < 4; i++) {
            //     linesizes[i]     = FFALIGN(linesizes[i],
            //     tops_ctx->stride_align); linesizes1[i]    =
            //     FFALIGN(linesizes[i], tops_ctx->stride_align);
            dst->linesize[i] = 0;  // init to zero
            planesizes[i]    = 0;
            // av_log(ctx, AV_LOG_DEBUG, "ptrlinesizes[%d]:%d\n", i,
            // src->linesize[i]);
            linesizes1[i] = src->linesize[i];
        }
        ret = av_image_fill_plane_sizes(planesizes, src_format, src->height,
                                        linesizes1);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "tran data av_image_fill_plane_sizes failed.\n");
            return AVERROR(ENOSYS);
        }

        size  = 0;
        plans = av_pix_fmt_count_planes(src_format);
        for (int i = 0; i < plans; i++) {
            size += planesizes[i];
            av_log(ctx, AV_LOG_DEBUG,
                   "src linesizes[%d]:%d,planesizes[%d]:%ld\n", i,
                   src->linesize[i], i, planesizes[i]);
        }

        for (int i = 0; i < plans && src->data[i]; i++) {
            if (dst->hw_frames_ctx) {
                av_log(ctx, AV_LOG_DEBUG,
                       "tops DtoD [%d],dst:%p,src:%p,cpy size:%ld .\n", i,
                       (void*)dst->data[i], src->data[i], planesizes[i]);
                tops_ret = tops_ctx->topsruntime_lib_ctx->lib_topsMemcpy(
                    dst->data[i], src->data[i], planesizes[i],
                    topsMemcpyDeviceToDevice);
                dst->format = src->format;
            } else {
                av_log(ctx, AV_LOG_DEBUG,
                       "tops DtoH [%d],dst:%p,src:%p,cpy size:%ld.\n", i,
                       (void*)dst->data[i], src->data[i], planesizes[i]);
                tops_ret = tops_ctx->topsruntime_lib_ctx->lib_topsMemcpy(
                    dst->data[i], src->data[i], planesizes[i],
                    topsMemcpyDeviceToHost);
                dst->format = src_format;
            }

            if (tops_ret != topsSuccess) {
                av_log(ctx, AV_LOG_ERROR, "d2x: host %p -> dev %p, size %lu \n",
                       src->data[i], dst->data[i], planesizes[i]);
                return -1;
            }
            dst->linesize[i] = src->linesize[i];
        }

        av_frame_copy_props(dst, src);
        dst->width  = src->width;
        dst->height = src->height;

        // dst->channels       = src->channels; //audio
        // dst->channel_layout = src->channel_layout; //audio
        dst->nb_samples = src->nb_samples;
        av_log(ctx, AV_LOG_DEBUG, "topscodec_memcpyDtoX size:%ld\n", size);
    } else {  // src on host, dst on device
        src_format = src->format;
        av_log(ctx, AV_LOG_DEBUG, "2-src format:%d, w:%d, h:%d\n", src_format,
               src->width, src->height);

        // ret = av_image_fill_linesizes(linesizes, src_format, src->width);
        // if (ret < 0) {
        //     av_log(ctx, AV_LOG_ERROR,
        //            "tran data av_image_fill_linesizes failed.\n");
        //     return AVERROR(ENOSYS);
        // }
        for (int i = 0; i < 4; i++) {
            //     linesizes[i]     = FFALIGN(linesizes[i],
            //     tops_ctx->stride_align); linesizes1[i]    =
            //     FFALIGN(linesizes[i], tops_ctx->stride_align);
            dst->linesize[i] = 0;  // init to zero
            planesizes[i]    = 0;
            // av_log(ctx, AV_LOG_DEBUG, "ptrlinesizes[%d]:%d\n", i,
            // src->linesize[i]);
            linesizes1[i] = src->linesize[i];
        }
        ret = av_image_fill_plane_sizes(planesizes, src_format, src->height,
                                        linesizes1);
        if (ret < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "tran data av_image_fill_plane_sizes failed.\n");
            return AVERROR(ENOSYS);
        }

        size  = 0;
        plans = av_pix_fmt_count_planes(src_format);
        for (int i = 0; i < plans; i++) {
            size += planesizes[i];
            av_log(ctx, AV_LOG_DEBUG,
                   "src linesizes[%d]:%d,planesizes[%d]:%ld\n", i,
                   src->linesize[i], i, planesizes[i]);
        }

        for (int i = 0; i < plans && src->data[i]; i++) {
            if (dst->hw_frames_ctx) {
                av_log(ctx, AV_LOG_DEBUG,
                       "tops HtoD [%d],dst:%p,src:%p,cpy size:%ld .\n", i,
                       (void*)dst->data[i], src->data[i], planesizes[i]);
                tops_ret = tops_ctx->topsruntime_lib_ctx->lib_topsMemcpy(
                    dst->data[i], src->data[i], planesizes[i],
                    topsMemcpyHostToDevice);
                dst->format = AV_PIX_FMT_TOPSCODEC;
            } else {
                av_log(ctx, AV_LOG_DEBUG,
                       "tops DtoH [%d],dst:%p,src:%p,cpy size:%ld.\n", i,
                       (void*)dst->data[i], src->data[i], planesizes[i]);
                tops_ret = tops_ctx->topsruntime_lib_ctx->lib_topsMemcpy(
                    dst->data[i], src->data[i], planesizes[i],
                    topsMemcpyHostToHost);
                dst->format = src_format;
            }

            if (tops_ret != topsSuccess) {
                av_log(ctx, AV_LOG_ERROR, "h2x: host %p -> dev %p, size %lu \n",
                       src->data[i], dst->data[i], planesizes[i]);
                return -1;
            }
            dst->linesize[i] = src->linesize[i];
        }

        av_frame_copy_props(dst, src);
        dst->width  = src->width;
        dst->height = src->height;
        av_log(ctx, AV_LOG_DEBUG, "topscodec_memcpyHtoX size:%ld\n", size);
    }
    return 0;
}

static int topscodec_device_init(AVHWDeviceContext* device_ctx) {
    int                     ret = 0;
    TOPSCodecDeviceContext* ctx = device_ctx->hwctx;
    (void)ctx;
    (void)ret;
    av_log(NULL, AV_LOG_DEBUG, "topscodec_device_init success\n");
    return 0;
}

static void topscodec_device_uninit(AVHWDeviceContext* device_ctx) {
    TOPSCodecDeviceContext* ctx = device_ctx->hwctx;
    if (ctx->topsruntime_lib_ctx) {
        topsruntimes_free_functions(&ctx->topsruntime_lib_ctx);
        av_log(NULL, AV_LOG_DEBUG, "topsruntimes_free_functions success\n");
    }
    av_log(NULL, AV_LOG_DEBUG, "unload topsruntime_lib_ctx\n");
}

/*TODO*/
static int topscodec_device_create(AVHWDeviceContext* device_ctx,
                                   const char*        device, /*device id*/
                                   AVDictionary* opts, int flags) {
    TOPSCodecDeviceContext* ctx        = device_ctx->hwctx;
    int                     device_idx = 0;
    int                     ret        = 0;

    ret = topsruntimes_load_functions(&ctx->topsruntime_lib_ctx);
    if (ret != 0) {
        av_log(NULL, AV_LOG_ERROR,
               "Error, topsruntime_lib_ctx failed, ret(%d)\n", ret);
        return ret;
    }
    av_log(NULL, AV_LOG_DEBUG, "topsruntimes_load_functions success\n");

    if (device) device_idx = strtol(device, NULL, 0);

    av_log(NULL, AV_LOG_DEBUG, "topscodec_set_device: device_idx[%d]\n",
           device_idx);
    pthread_mutex_lock(&av_tops_lib_mutex);
    ret = ctx->topsruntime_lib_ctx->lib_topsSetDevice(device_idx);
    pthread_mutex_unlock(&av_tops_lib_mutex);
    if (ret != 0) {
        av_log(NULL, AV_LOG_ERROR,
               "Error, topscodec_set_device[%d] failed, ret(%d)\n", device_idx,
               ret);
        ret = AVERROR(EINVAL);
        return ret;
    }
    av_log(NULL, AV_LOG_DEBUG, "topscodec_set_device[%d] success\n",
           device_idx);
    return 0;
}

const HWContextType ff_hwcontext_type_topscodec = {
    .type              = AV_HWDEVICE_TYPE_TOPSCODEC,
    .name              = "topscodec",
    .device_hwctx_size = sizeof(TOPSCodecDeviceContext),
    // .device_priv_size       = 0,                       /*TODO*/
    .frames_hwctx_size = 0, /*TODO*/
    // .frames_priv_size       = 0,                       /*TODO*/
    .device_create          = topscodec_device_create, /*MUST NOT BE NULL*/
    .device_init            = topscodec_device_init,
    .device_uninit          = topscodec_device_uninit,
    .frames_get_constraints = topscodec_frames_get_constraints,
    .frames_init            = topscodec_frames_init,
    .frames_get_buffer      = topscodec_get_buffer,
    .transfer_get_formats   = topscodec_transfer_get_formats,
    .transfer_data_to       = topscodec_transfer_data,
    .transfer_data_from     = topscodec_transfer_data,
    .pix_fmts =
        (const enum AVPixelFormat[]){AV_PIX_FMT_TOPSCODEC, AV_PIX_FMT_NONE},
};
