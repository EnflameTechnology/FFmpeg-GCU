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

#include "libavcodec/ff_topscodec_buffers.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "libavcodec/avcodec.h"
#include "libavcodec/ff_topscodec_dec.h"
#include "libavcodec/ff_topscodec_utils.h"
#include "libavcodec/internal.h"
#include "libavutil/avassert.h"
#include "libavutil/hwcontext_topscodec.h"
#include "libavutil/imgutils.h"
#include "libavutil/macros.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"
#include "libavutil/thread.h"
#include "libavutil/time.h"

// static pthread_mutex_t g_buf_mutex = PTHREAD_MUTEX_INITIALIZER;

__attribute__((unused)) static enum AVColorPrimaries
topscodec_get_color_primaries(const EFBuffer* buf) {
    return AVCOL_PRI_UNSPECIFIED;
}

__attribute__((unused)) static enum AVColorRange topscodec_get_color_range(
    const EFBuffer* buf) {
    return AVCOL_RANGE_UNSPECIFIED;
}

// FixMe
__attribute__((unused)) static enum AVColorSpace
tops_colorspace_2_av_colorspace(topscodecColorSpace_t cs) {
    enum AVColorSpace ret = AVCOL_SPC_UNSPECIFIED;
    if (cs == TOPSCODEC_COLOR_SPACE_BT_601) {
        ret = AVCOL_SPC_SMPTE170M;
    } else if (cs == TOPSCODEC_COLOR_SPACE_BT_709) {
        ret = AVCOL_SPC_BT709;
    } else if (cs == TOPSCODEC_COLOR_SPACE_BT_2020) {
        ret = AVCOL_SPC_BT2020_CL;
    } else if (cs == TOPSCODEC_COLOR_SPACE_BT_601_ER) {
        ret = AVCOL_SPC_BT470BG;
    } else if (cs == TOPSCODEC_COLOR_SPACE_BT_709_ER) {
        ret = AVCOL_SPC_RGB;
    } else if (cs == TOPSCODEC_COLOR_SPACE_BT_2020_ER) {
        ret = AVCOL_SPC_BT2020_NCL;
    }
    return ret;
}
// FixMe
static topscodecColorSpace_t av_colorspace_2_tops_colorspace(
    enum AVColorSpace cs) {
    topscodecColorSpace_t ret = TOPSCODEC_COLOR_SPACE_BT_601;
    if (cs == AVCOL_SPC_SMPTE170M) {
        ret = TOPSCODEC_COLOR_SPACE_BT_601;
    } else if (cs == AVCOL_SPC_BT709) {
        ret = TOPSCODEC_COLOR_SPACE_BT_709;
    } else if (cs == AVCOL_SPC_BT2020_CL) {
        ret = TOPSCODEC_COLOR_SPACE_BT_2020;
    } else if (cs == AVCOL_SPC_BT470BG) {
        ret = TOPSCODEC_COLOR_SPACE_BT_601_ER;
    } else if (cs == AVCOL_SPC_RGB) {
        ret = TOPSCODEC_COLOR_SPACE_BT_709_ER;
    } else if (cs == AVCOL_SPC_BT2020_NCL) {
        ret = TOPSCODEC_COLOR_SPACE_BT_2020_ER;
    }
    return ret;
}

__attribute__((unused)) static enum AVColorTransferCharacteristic
topscodec_get_color_trc(const EFBuffer* buf) {
    return AVCOL_TRC_UNSPECIFIED;
}

static void topscodec_free_buffer(void* opaque, uint8_t* unused) {
    int                  ret;
    EFBuffer*            efbuf;
    EFCodecDecContext_t* ctx;

    if (!opaque) {
        av_log(NULL, AV_LOG_WARNING,
            "topscodec_free_buffer: opaque is NULL\n");
        return;
    }

    efbuf = (EFBuffer*)opaque;
    if (!efbuf->ef_dec_context) {
        av_log(efbuf->avctx, AV_LOG_WARNING,
                "topscodec_free_buffer: efbuf->ef_dec_context is NULL.\n");
        if (atomic_fetch_sub(&efbuf->context_refcount, 1) == 1) {
            av_freep(&efbuf);
        }
        return;
    }

    ctx = (EFCodecDecContext_t*)efbuf->ef_dec_context;

    if (atomic_fetch_sub(&efbuf->context_refcount, 1) == 1) {
        if (efbuf->type == EF_BUFFER_TYPE_FRAME) {
            if (ctx->topscodec_lib_ctx) {
                ret = ctx->topscodec_lib_ctx->lib_topscodecDecFrameUnmap(
                    ctx->handle, &efbuf->ef_frame);
                if (ret != 0)
                    av_log(efbuf->avctx, AV_LOG_ERROR,
                        "[%p]topscodecDecFrameUnmap FAILED.\n", ctx->handle);
                else
                    av_log(efbuf->avctx, AV_LOG_DEBUG,
                        "[%p]topscodecDecFrameUnmap SUCCESS.\n", ctx->handle);
            }
        }

        av_freep(&efbuf);
    }
}

static int topscodec_buf_increase_ref(EFBuffer* efbuf) {
    atomic_fetch_add(&efbuf->context_refcount, 1);
    return 0;
}

int ff_topscodec_buf_to_bufref(const EFBuffer* efbuf, int plane,
                                   AVBufferRef** buf, size_t planesize) {
    int ret = 0;

    if (plane >= efbuf->ef_frame.plane_num) return AVERROR(EINVAL);

    /* even though most encoders return 0 in data_offset encoding vp8 does
    require this value */
    if (efbuf->type == EF_BUFFER_TYPE_FRAME) {
        *buf = av_buffer_create((char*)efbuf->ef_frame.plane[plane].dev_addr
                                /*+ efbuf->ef_frame.plane[plane].offline*/,
                                planesize, topscodec_free_buffer,
                                (EFBuffer*)efbuf, 0);
        if (!*buf) return AVERROR(ENOMEM);
    }

    ret = topscodec_buf_increase_ref((EFBuffer*)efbuf);

    return ret;
}

/******************************************************************************
 *
 *             TOPSCODEC Frame/Pkt interface
 *
 ******************************************************************************/

int ff_topscodec_avframe_to_efbuf(AVFrame* avframe, EFBuffer* efbuf) {
    topsError_t             tops_ret;
    topscodecFrame_t*       ef_frame            = NULL;
    void*                   data                = NULL;
    AVCodecContext*         avctx               = NULL;
    AVHWFramesContext*      av_frames_ctx       = NULL;
    AVHWDeviceContext*      av_device_ctx       = NULL;
    TOPSCodecDeviceContext* tops_device_ctx     = NULL;
    TopsRuntimesFunctions*  topsruntime_lib_ctx = NULL;

    int offset          = 0;
    int nBytes          = 0;
    int av_frame_planes = 0;
    int total_size      = 0;

    ptrdiff_t linesizes1[4] = {0};
    size_t    planesizes[4] = {0};

    av_assert0(avframe);
    av_assert0(efbuf);
    // print_frame(avctx, &efbuf->ef_frame);//for debug

    avctx = efbuf->avctx;

    av_frames_ctx       = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
    av_device_ctx       = av_frames_ctx->device_ctx;
    tops_device_ctx     = av_device_ctx->hwctx;
    topsruntime_lib_ctx = tops_device_ctx->topsruntime_lib_ctx;

    if (topsruntime_lib_ctx == NULL) {
        av_log(avctx, AV_LOG_ERROR,
               "ff_topscodec_avframe_to_efbuf topsruntime is NULL\n");
        return AVERROR(EPERM);
    }
    // print_avframe(avctx, avframe);

    ef_frame         = &efbuf->ef_frame;
    ef_frame->pts    = avframe->pts;
    ef_frame->width  = avframe->width;
    ef_frame->height = avframe->height;
    ef_frame->color_space =
        av_colorspace_2_tops_colorspace(avframe->colorspace);

    if (avframe->hw_frames_ctx) {
        // D2D memcpy
        ef_frame->pixel_format =
            avpixfmt_2_topspixfmt(av_frames_ctx->sw_format);
        av_frame_planes = av_pix_fmt_count_planes(av_frames_ctx->sw_format);
        // av_log(avctx, AV_LOG_TRACE,
        //     "D2D-pixfmt:%s, ef_frame->plane_num: %d, av_frame_planes: %d\n",
        //     av_get_pix_fmt_name(av_frames_ctx->sw_format),
        //     ef_frame->plane_num, av_frame_planes);

        for (int i = 0; i < av_frame_planes; i++) {
            linesizes1[i] = avframe->linesize[i];
            // av_log(avctx, AV_LOG_TRACE, "ptrlinesizes[%d]:%ld\n", i,
            //        linesizes1[i]);
        }

        tops_ret = av_image_fill_plane_sizes(
            planesizes, av_frames_ctx->sw_format, avframe->height, linesizes1);
        if (tops_ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "av_image_fill_plane_sizes failed.\n");
            return AVERROR_BUG;
        }
        // for (int i = 0; i < av_frame_planes; i++) {
        //     av_log(avctx, AV_LOG_TRACE, "planesizes[%d]:%lu\n", i,
        //            planesizes[i]);
        // }

        offset = 0;
        for (int i = 0; i < av_frame_planes; i++) {
            ef_frame->plane[i].stride = avframe->linesize[i];
            data   = (void*)(efbuf->ef_frame.plane[0].dev_addr + offset);
            nBytes = planesizes[i];

            total_size += nBytes;
            if (nBytes > 0) {
                tops_ret = topsruntime_lib_ctx->lib_topsMemcpyDtoD(
                    data, avframe->data[i], nBytes);
                if (tops_ret != topsSuccess) {
                    av_log(avctx, AV_LOG_ERROR,
                           "D2D: host fail %p -> dev %p, size %d fuc: %s, "
                           "line:%d\n",
                           avframe->data[i], data, nBytes, __func__, __LINE__);
                    return AVERROR_BUG;
                }
                // av_log(avctx, AV_LOG_TRACE,
                //     "D2D: host success %p -> dev %p, size %d, offset %d.\n",
                //     avframe->data[i], data, nBytes, offset);
            }
            offset += nBytes;
        }
    } else {
        // H2D
        av_frame_planes = av_pix_fmt_count_planes(avframe->format);
        // av_log(avctx, AV_LOG_TRACE,
        //      "H2D-pixfmt:%s, ef_frame->plane_num: %d, av_frame_planes: %d\n",
        //      av_get_pix_fmt_name(avframe->format), ef_frame->plane_num,
        //      av_frame_planes);

        for (int i = 0; i < av_frame_planes; i++) {
            linesizes1[i] = avframe->linesize[i];
            // av_log(avctx, AV_LOG_TRACE, "ptrlinesizes[%d]:%ld\n", i,
            //        linesizes1[i]);
        }

        tops_ret = av_image_fill_plane_sizes(planesizes, avframe->format,
                                             avframe->height, linesizes1);
        if (tops_ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "av_image_fill_plane_sizes failed.\n");
            return AVERROR_BUG;
        }
        // for (int i = 0; i < av_frame_planes; i++) {
        //     av_log(avctx, AV_LOG_TRACE, "planesizes[%d]:%lu\n", i,
        //            planesizes[i]);
        // }

        offset = 0;
        for (int i = 0; i < av_frame_planes; i++) {
            data   = (void*)(ef_frame->plane[0].dev_addr + offset);
            nBytes = planesizes[i];
            total_size += nBytes;
            if (nBytes > 0) {
                tops_ret = topsruntime_lib_ctx->lib_topsMemcpyHtoD(
                    data, avframe->data[i], nBytes);
                if (tops_ret != topsSuccess) {
                    av_log(avctx, AV_LOG_ERROR,
                           "h2d: host %p -> dev %p, size %d fuc: %s, "
                           "line:%d, fail\n",
                           avframe->data[i], data, nBytes, __func__, __LINE__);
                    return AVERROR_BUG;
                }
                // av_log(
                //   avctx, AV_LOG_TRACE,
                //   "h2d: host  %p -> dev %p, size %d, offset %d ,success.\n",
                //   avframe->data[i], data, nBytes, offset);
            }
            offset += nBytes;
        }
    }
    ef_frame->plane[0].alloc_len = total_size;
    av_frame_unref(avframe);

    return 0;
}

int ff_topscodec_efbuf_to_avframe(const EFBuffer* efbuf, AVFrame* avframe) {
    int                    ret          = 0;
    AVCodecContext*        avctx        = NULL;
    EFCodecDecContext_t*   ctx          = NULL;
    AVHWFramesContext*     hw_frame_ctx = NULL;
    TopsRuntimesFunctions* topsruntime  = NULL;
    TopsCodecFunctions*    topscodec    = NULL;
    int                    avframe_format;

    ptrdiff_t linesizes1[4] = {0};
    size_t    planesizes[4] = {0};
    uint8_t*  data[4]       = {NULL};

    av_assert0(avframe);
    av_assert0(efbuf);

    avctx        = efbuf->avctx;
    ctx          = avctx->priv_data;
    topscodec    = ctx->topscodec_lib_ctx;
    topsruntime  = ctx->topsruntime_lib_ctx;
    hw_frame_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;

    avframe->height = efbuf->ef_frame.height;
    avframe->width  = efbuf->ef_frame.width;
    avframe_format  = topspixfmt_2_avpixfmt(efbuf->ef_frame.pixel_format);

    /*reset w and h*/
    if (hw_frame_ctx->height <= 0 || hw_frame_ctx->width <= 0) {
        hw_frame_ctx->height = efbuf->ef_frame.height;
        hw_frame_ctx->width  = efbuf->ef_frame.width;

        hw_frame_ctx->initial_pool_size = 3;    /*TODO*/
        hw_frame_ctx->pool              = NULL; /*TODO*/
        if ((ret = av_hwframe_ctx_init(ctx->hwframe)) < 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "Error, av_hwframe_ctx_init failed, ret(%d)\n", ret);
            return AVERROR_BUG;
        }
        av_log(avctx, AV_LOG_DEBUG, "hw frame init 2 success.\n");
    }

    if (!ctx->callback) {
        avctx->height     = efbuf->ef_frame.height;  // data race
        avctx->width      = efbuf->ef_frame.width;   // data race
        avctx->codec_type = AVMEDIA_TYPE_VIDEO;
    }

    for (int i = 0; i < 4; i++) {
        // linesizes1[i] = linesizes[i];
        linesizes1[i] = efbuf->ef_frame.plane[i].stride;
        data[i]       = (uint8_t*)efbuf->ef_frame.plane[i].dev_addr;
        av_log(avctx, AV_LOG_DEBUG, "ptrlinesizes[%d]:%ld\n", i, linesizes1[i]);
    }

    ret = av_image_fill_plane_sizes(planesizes, avframe_format, avframe->height,
                                    linesizes1);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "av_image_fill_plane_sizes failed.\n");
        return AVERROR_BUG;
    }

    if (av_pix_fmt_count_planes(avframe_format) != efbuf->ef_frame.plane_num) {
        av_log(avctx, AV_LOG_ERROR,
               "pix:%s,efbuf plane [%d]is not suitable for "
               "ffmpeg[%d].\n",
               av_get_pix_fmt_name(avframe_format), efbuf->ef_frame.plane_num,
               av_pix_fmt_count_planes(avframe_format));
        return AVERROR_BUG;
    }

    /* 1. get references to the actual data */
    if (!ctx->zero_copy) { /*Not support yet*/
        // ff_mutex_lock(&g_buf_mutex);
        av_hwframe_get_buffer(avctx->hw_frames_ctx, avframe, 0);
        // ff_mutex_unlock(&g_buf_mutex);

        for (int i = 0; i < efbuf->ef_frame.plane_num; i++) {
            avframe->linesize[i] = efbuf->ef_frame.plane[i].stride;
            // if (avframe->linesize[i] != linesizes[i]) {
            //     av_log(avctx, AV_LOG_ERROR,
            //            "linesize[%d] is errefbuf linesize:%d,av "
            //            "linesize:%d\n",
            //            i, avframe->linesize[i], linesizes[i]);
            //     av_frame_unref(avframe);
            //     return AVERROR_BUG;
            // }

            if (planesizes[i] == 0) {
                av_log(ctx, AV_LOG_ERROR, "planesizes[%d] err,value:%lu\n", i,
                       planesizes[i]);
                av_frame_unref(avframe);
                return AVERROR_BUG;
            }
            av_assert0(planesizes[i] > 0);
            av_assert0(data[i]);
            av_assert0(avframe->data[i]);
            ret = topsruntime->lib_topsMemcpyDtoD(avframe->data[i], data[i],
                                                  planesizes[i]);
            if (ret != topsSuccess) {
                av_log(ctx, AV_LOG_ERROR, "d2x: dev %p -> dev 0x%p, size %lu\n",
                       data[i], (void*)avframe->data[i], planesizes[i]);
                av_log(ctx, AV_LOG_ERROR,
                       "topsMemcpyDtoD error occur, func: %s, "
                       "line: %d\n",
                       __func__, __LINE__);
                av_frame_unref(avframe);
                return AVERROR_BUG;
            }
            av_log(avctx, AV_LOG_DEBUG, "d2d: dev %p -> dev %p, size %lu\n",
                   data[i], avframe->data[i], planesizes[i]);
        }  // for
        ret = topscodec->lib_topscodecDecFrameUnmap(
            ctx->handle, (topscodecFrame_t*)&efbuf->ef_frame);
        if (ret != 0) {
            av_log(avctx, AV_LOG_ERROR, "topscodecDecFrameUnmap FAILED.\n");
            av_frame_unref(avframe);
            return AVERROR_BUG;
        }
        av_log(avctx, AV_LOG_DEBUG, "topscodecDecFrameUnmap SUCCESS.\n");
    } else { /*zero copy*/
        for (int i = 0; i < efbuf->ef_frame.plane_num; i++) {
            ret = ff_topscodec_buf_to_bufref(efbuf, i, &avframe->buf[i],
                                          planesizes[i]);
            if (ret) return ret;

            avframe->linesize[i] = efbuf->ef_frame.plane[i].stride;
            avframe->data[i]     = avframe->buf[i]->data;
        }
        // 当zero_copy=
        // 0的时候，av_hwframe_get_buffer会执行下面这条命令，所以这条指令务必在这个{}中。
        avframe->hw_frames_ctx = av_buffer_ref(avctx->hw_frames_ctx);
    }

    // if (avctx->pkt_timebase.num && avctx->pkt_timebase.den)
    //     avframe->pts = av_rescale_q(efbuf->ef_frame.pts, (AVRational){1,
    //     10000000}, avctx->pkt_timebase);
    // else

    // get packet prop
    ff_mutex_lock(&ctx->pkt_prop_mutex);
    if ((int)av_fifo_size(ctx->pkt_prop_fifo) > 0) {
        AVFrame* prop_avframe_tmp;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
        av_fifo_read(ctx->pkt_prop_fifo, &prop_avframe_tmp, 1);
#else
        av_fifo_generic_read(ctx->pkt_prop_fifo, &prop_avframe_tmp,
                             sizeof(AVFrame*), NULL);
#endif

        av_log(avctx, AV_LOG_DEBUG, "prop fifo [%p] Get frame ,size:%d\n",
               prop_avframe_tmp, (int)av_fifo_size(ctx->pkt_prop_fifo));
        av_frame_copy_props(avframe, prop_avframe_tmp);

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
        av_fifo_write(ctx->pkt_prop_fifo, &prop_avframe_tmp, 1);
#else
        avframe->pkt_size = 0;
        avframe->pkt_pos  = 0;
        av_fifo_generic_write(ctx->pkt_prop_fifo, &prop_avframe_tmp,
                              sizeof(AVFrame*), NULL);
#endif
        // av_frame_free(&prop_avframe_tmp);
        // dump_frame_info(avframe);
    }
    ff_mutex_unlock(&ctx->pkt_prop_mutex);
    /* get avframe information */
    avframe->pict_type = tops_2_av_pic_type(efbuf->ef_frame.pic_type);
    av_log(avctx, AV_LOG_DEBUG, "pic_type:%d\n", efbuf->ef_frame.pic_type);
    av_log(avctx, AV_LOG_DEBUG, "key_frame:%d\n", tops_is_key_frame(efbuf->ef_frame.pic_type));
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
    if (tops_is_key_frame(efbuf->ef_frame.pic_type))
        avframe->flags = AV_FRAME_FLAG_KEY;
    else
        avframe->flags = 0;
#else
    avframe->key_frame = tops_is_key_frame(efbuf->ef_frame.pic_type);
#endif
    avframe->pts = efbuf->ef_frame.pts;
    av_log(avctx, AV_LOG_DEBUG, "pts:%lu\n", avframe->pts);

    if (!ctx->enable_crop && !ctx->enable_resize) {
        avctx->coded_height = efbuf->ef_frame.height;
        avctx->coded_width  = efbuf->ef_frame.width;
    }

    /* 3. report errors upstream */
    // if (efbuf->ef_frame.pic_type == TOPSCODEC_PIC_TYPE_UNKNOWN) {
    //     av_log(avctx, AV_LOG_ERROR, "driver decode error\n");
    //     avframe->decode_error_flags |= FF_DECODE_ERROR_INVALID_BITSTREAM;
    // }
    if (avctx->pix_fmt == AV_PIX_FMT_TOPSCODEC) {
        avframe->format = AV_PIX_FMT_TOPSCODEC;
    } else {
        avframe->format = topspixfmt_2_avpixfmt(efbuf->ef_frame.pixel_format);
    }
    if (!ctx->zero_copy) av_freep(&efbuf);
    return 0;
}

int ff_topscodec_avpkt_to_efbuf(const AVPacket* avpkt, EFBuffer* efbuf) {
    void*                  data         = NULL;
    topscodecStream_t*     efpkt        = NULL;
    AVCodecContext*        avctx        = NULL;
    EFCodecDecContext_t*   ctx          = NULL;
    TopsRuntimesFunctions* topsruntimes = NULL;

    topsError_t tops_ret;

    av_assert0(avpkt);
    av_assert0(efbuf);

    efpkt        = &efbuf->ef_pkt;
    avctx        = efbuf->avctx;
    ctx          = avctx->priv_data;
    topsruntimes = ctx->topsruntime_lib_ctx;

    if (!topsruntimes || !topsruntimes->lib_topsMemcpyHtoD) {
        av_log(avctx, AV_LOG_ERROR, "topsruntimes or lib_topsMemcpyHtoD is NULL\n");
        return AVERROR(EINVAL);
    }

    efpkt->data_len    = avpkt->size;
    efpkt->data_offset = 0;
    efpkt->mem_type    = TOPSCODEC_MEM_TYPE_HOST;

    if (avpkt->pts < 0)
        efpkt->pts = 0;
    else
        efpkt->pts = avpkt->pts;

    efpkt->stream_type = TOPSCODEC_NALU_TYPE_UNKNOWN;
    // 不能re_alloc  topscodecDecSendStream failed. ret = 6 应该是vpu fw
    // 不支持中间对input buf更改

    data = (void*)efbuf->ef_frame_pkt_virtual_addr;
    if (avpkt->size > 0 && avpkt->data && data) {
        tops_ret = topsruntimes->lib_topsMemcpyHtoD(data, avpkt->data, avpkt->size);
        if (tops_ret != topsSuccess) {
            av_log(avctx, AV_LOG_ERROR, "topsMemcpyHtoD failed!\n");
            return AVERROR(EPERM);
        }
        av_log(avctx, AV_LOG_DEBUG,
                "[DEC] h2d(topsMemcpyHtoD): host %p -> dev %p, size %u \n",
                avpkt->data, data, efpkt->data_len);
    }

    efpkt->mem_addr  = efbuf->ef_frame_pkt_phy_addr;
    efpkt->alloc_len = efbuf->ef_frame_pkt_buf_size_aligned_4k;
    av_log(avctx, AV_LOG_DEBUG, "[DEC] efpkt to hw, buf size:%d, addr:0x%lx \n", efpkt->alloc_len, efpkt->mem_addr);

    if (avpkt->flags & AV_PKT_FLAG_KEY)
        efpkt->stream_type = TOPSCODEC_NALU_TYPE_I;

    return 0;
}

int ff_topscodec_alloc_efbuf_internal_data(EFBuffer* efbuf) {
    AVCodecContext*         avctx               = NULL;
    AVHWFramesContext*      av_frames_ctx       = NULL;
    AVHWDeviceContext*      av_device_ctx       = NULL;
    TOPSCodecDeviceContext* tops_device_ctx     = NULL;
    TopsRuntimesFunctions*  topsruntime_lib_ctx = NULL;

    topsPointerAttribute_t att      = {0};
    topsError_t            tops_ret = TOPS_SUCCESS;
    void*                  tmp      = NULL;

    avctx               = efbuf->avctx;
    av_frames_ctx       = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
    av_device_ctx       = av_frames_ctx->device_ctx;
    tops_device_ctx     = av_device_ctx->hwctx;
    topsruntime_lib_ctx = tops_device_ctx->topsruntime_lib_ctx;

    if (efbuf->ef_frame_pkt_buf_size == 0) {
        av_log(avctx, AV_LOG_ERROR,
               "efbuf->ef_frame_pkt_buf_size is 0, just return\n");
        return 0;
    }

    efbuf->ef_frame_pkt_buf_size_aligned_4k =
        FFALIGN(efbuf->ef_frame_pkt_buf_size, 4096);

    if (topsruntime_lib_ctx == NULL) {
        av_log(avctx, AV_LOG_ERROR,
               "alloc efbuf internal data, topsruntime_lib_ctx is NULL\n");
        return AVERROR(EPERM);
    }

    tops_ret = topsruntime_lib_ctx->lib_topsExtMallocWithFlags(
        &tmp, efbuf->ef_frame_pkt_buf_size_aligned_4k,
        topsMallocHostAccessable);
    if (topsSuccess != tops_ret) {
        av_log(avctx, AV_LOG_ERROR, "Error, topsMalloc failed, ret(%d)\n",
               tops_ret);
        return AVERROR(EPERM);
    }
    efbuf->ef_frame_pkt_virtual_addr = (uint64_t)tmp;
    av_log(avctx, AV_LOG_DEBUG,
           "malloc ef_frame_virtual_addr:0x%lx, size:%ld\n",
           efbuf->ef_frame_pkt_virtual_addr,
           efbuf->ef_frame_pkt_buf_size_aligned_4k);
    tops_ret = topsruntime_lib_ctx->lib_topsPointerGetAttributes(
        &att, (void*)(efbuf->ef_frame_pkt_virtual_addr));
    if (tops_ret != topsSuccess) {
        av_log(avctx, AV_LOG_ERROR, "topsPointerGetAttributes failed!\n");
        return AVERROR(EPERM);
    }
    efbuf->ef_frame_pkt_phy_addr = (u64_t)att.device_pointer;

    return 0;
}

int ff_topscodec_free_efbuf_internal_data(EFBuffer* efbuf) {
    topsError_t tops_ret = TOPS_SUCCESS;

    AVCodecContext*         avctx               = NULL;
    AVHWFramesContext*      av_frames_ctx       = NULL;
    AVHWDeviceContext*      av_device_ctx       = NULL;
    TOPSCodecDeviceContext* tops_device_ctx     = NULL;
    TopsRuntimesFunctions*  topsruntime_lib_ctx = NULL;

    avctx               = efbuf->avctx;
    av_frames_ctx       = (AVHWFramesContext*)avctx->hw_frames_ctx->data;
    av_device_ctx       = av_frames_ctx->device_ctx;
    tops_device_ctx     = av_device_ctx->hwctx;
    topsruntime_lib_ctx = tops_device_ctx->topsruntime_lib_ctx;

    if (topsruntime_lib_ctx == NULL) {
        av_log(avctx, AV_LOG_ERROR,
               "free efbuf internal data, topsruntime_lib_ctx is NULL\n");
        return AVERROR(EPERM);
    }

    if (efbuf->ef_frame_pkt_buf_size > 0 &&
        efbuf->ef_frame_pkt_virtual_addr > 0) {
        tops_ret = topsruntime_lib_ctx->lib_topsFree(
            (void*)efbuf->ef_frame_pkt_virtual_addr);
        if (tops_ret != topsSuccess) {
            av_log(avctx, AV_LOG_ERROR, "topsFree failed!\n");
            return AVERROR(EPERM);
        }
        efbuf->ef_frame_pkt_virtual_addr = 0;
        av_log(avctx, AV_LOG_DEBUG, "topsFree ef_frame_virtual_addr success\n");
    }
    efbuf->ef_frame_pkt_buf_size            = 0;
    efbuf->ef_frame_pkt_buf_size_aligned_4k = 0;
    efbuf->ef_frame_pkt_virtual_addr        = 0;
    efbuf->ef_frame_pkt_phy_addr            = 0;
    return 0;
}
