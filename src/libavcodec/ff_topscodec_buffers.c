/*
 * topscodec buffer helper functions.
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

#include "ff_topscodec_buffers.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "ff_topscodec_dec.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/internal.h"
#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"

static pthread_mutex_t g_buf_mutex = PTHREAD_MUTEX_INITIALIZER;

void dump_frame_info(const AVFrame* avframe) {
    av_log(NULL, AV_LOG_DEBUG, "===== AVFrame Properties =====\n");
    // 基本属性
    av_log(NULL, AV_LOG_DEBUG, "key_frame:%d\n", avframe->key_frame);
    av_log(NULL, AV_LOG_DEBUG, "format(%d): %s\n", avframe->format, av_get_pix_fmt_name(avframe->format));
    av_log(NULL, AV_LOG_DEBUG, "pict_type: %d (%c)\n", avframe->pict_type,
           av_get_picture_type_char(avframe->pict_type));

    // PTS和时间基
    av_log(NULL, AV_LOG_DEBUG, "pts: %" PRId64 "\n", avframe->pts);
    // av_log(NULL, AV_LOG_DEBUG, "time_base: %d/%d\n", avframe->time_base.num, avframe->time_base.den);

    // av_log(NULL, AV_LOG_DEBUG, "frame pos:%ld\n", avframe->pkt_pos);
    av_log(NULL, AV_LOG_DEBUG, "frame duration:%ld\n", avframe->pkt_duration);
    // av_log(NULL, AV_LOG_DEBUG, "frame pkt_size:%d\n", avframe->pkt_size);

    // 色彩空间
    av_log(NULL, AV_LOG_DEBUG, "color_primaries: %d (%s)\n", avframe->color_primaries,
           av_color_primaries_name(avframe->color_primaries));
    av_log(NULL, AV_LOG_DEBUG, "color_trc: %d (%s)\n", avframe->color_trc, av_color_transfer_name(avframe->color_trc));
    av_log(NULL, AV_LOG_DEBUG, "colorspace: %d (%s)\n", avframe->colorspace, av_color_space_name(avframe->colorspace));
    av_log(NULL, AV_LOG_DEBUG, "color_range: %d (%s)\n", avframe->color_range,
           av_color_range_name(avframe->color_range));
}

static enum AVColorPrimaries topscodec_get_color_primaries(const EFBuffer* buf) { return AVCOL_PRI_UNSPECIFIED; }

static enum AVColorRange topscodec_get_color_range(const EFBuffer* buf) { return AVCOL_RANGE_UNSPECIFIED; }

// FixMe
static topscodecColorSpace_t topscodec_get_color_space(const AVFrame* frame) {
    enum AVColorSpace     cs  = frame->colorspace;
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

static enum AVPictureType tops_2_av_pic_type(topscodecPicType_t type) {
    switch (type) {
        case TOPSCODEC_PIC_TYPE_I:
            return AV_PICTURE_TYPE_I;
        case TOPSCODEC_PIC_TYPE_IDR:
            return AV_PICTURE_TYPE_I;
        case TOPSCODEC_PIC_TYPE_P:
            return AV_PICTURE_TYPE_P;
        case TOPSCODEC_PIC_TYPE_B:
            return AV_PICTURE_TYPE_B;
        default:
            return AV_PICTURE_TYPE_NONE;
    }
    return AV_PICTURE_TYPE_NONE;
}

static int key_frame(topscodecPicType_t type) {
    if (type == TOPSCODEC_PIC_TYPE_IDR | type == TOPSCODEC_PIC_TYPE_I) return 1;
    return 0;
}

// FixMe
static enum AVColorSpace topscodec_get_color_space2(const EFBuffer* buf) {
    topscodecColorSpace_t cs  = buf->ef_frame.color_space;
    enum AVColorSpace     ret = AVCOL_SPC_UNSPECIFIED;
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

static enum AVColorTransferCharacteristic topscodec_get_color_trc(const EFBuffer* buf) { return AVCOL_TRC_UNSPECIFIED; }

typedef struct {
    enum AVPixelFormat     av_fmt;
    topscodecPixelFormat_t tops_fmt;
} PixelFormatMap;

static const PixelFormatMap pixels_format_map[] = {
    {AV_PIX_FMT_NV12, TOPSCODEC_PIX_FMT_NV12},                  // 8bit Semi-planar Y4-U1V1.
    {AV_PIX_FMT_NV21, TOPSCODEC_PIX_FMT_NV21},                  // 8bit Semi-planar Y4-V1U1.
    {AV_PIX_FMT_YUV420P, TOPSCODEC_PIX_FMT_I420},               // 8bit Planar Y4-U1-V1.
    {AV_PIX_FMT_YUV420P, TOPSCODEC_PIX_FMT_YV12},               // 8bit Planar Y4-V1-U1.    fixme
    {AV_PIX_FMT_YUYV422, TOPSCODEC_PIX_FMT_YUYV},               // 8bit packed Y2U1Y2V1.
    {AV_PIX_FMT_UYVY422, TOPSCODEC_PIX_FMT_UYVY},               // 8bit packed U1Y2V1Y2.
    {AV_PIX_FMT_YUYV422, TOPSCODEC_PIX_FMT_YVYU},               // 8bit packed Y2V1Y2U1.   fixme
    {AV_PIX_FMT_UYVY422, TOPSCODEC_PIX_FMT_VYUY},               // 8bit packed V1Y2U1Y2.   fixme
    {AV_PIX_FMT_P010BE, TOPSCODEC_PIX_FMT_P010},                // 10bit semi-planar Y4-U1V1.
    {AV_PIX_FMT_P010LE, TOPSCODEC_PIX_FMT_P010LE},              // 10bit semi-planar Y4-U1V1 little end.
    {AV_PIX_FMT_YUV420P10BE, TOPSCODEC_PIX_FMT_I010},           // 10bit planar Y4-U1-V1.
    {AV_PIX_FMT_YUV444P, TOPSCODEC_PIX_FMT_YUV444},             // 8bit planar Y4-U4-V4.
    {AV_PIX_FMT_YUV444P10BE, TOPSCODEC_PIX_FMT_YUV444_10BIT},   // 10bit planar Y4-U4-V4.
    {AV_PIX_FMT_ARGB, TOPSCODEC_PIX_FMT_ARGB},                  // Packed A8R8G8B8.
    {AV_PIX_FMT_BGRA, TOPSCODEC_PIX_FMT_BGRA},                  // Packed B8G8R8A8.
    {AV_PIX_FMT_ABGR, TOPSCODEC_PIX_FMT_ABGR},                  // Packed A8B8G8R8.
    {AV_PIX_FMT_RGBA, TOPSCODEC_PIX_FMT_RGBA},                  // Packed R8G8B8A8.
    {AV_PIX_FMT_RGB565BE, TOPSCODEC_PIX_FMT_RGB565},            // R5G6B5, 16 bits per pixel.
    {AV_PIX_FMT_BGR565BE, TOPSCODEC_PIX_FMT_BGR565},            // B5G6R5, 16 bits per pixel.
    {AV_PIX_FMT_RGB555BE, TOPSCODEC_PIX_FMT_RGB555},            // R5G5B5, 16 bits per pixel.
    {AV_PIX_FMT_BGR555BE, TOPSCODEC_PIX_FMT_BGR555},            // B5G5R5, 16 bits per pixel.
    {AV_PIX_FMT_RGB444BE, TOPSCODEC_PIX_FMT_RGB444},            // R4G4B4, 16 bits per pixel.
    {AV_PIX_FMT_BGR444BE, TOPSCODEC_PIX_FMT_BGR444},            // B4G4R4, 16 bits per pixel.
    {AV_PIX_FMT_RGB24, TOPSCODEC_PIX_FMT_RGB888},               // 8bit packed R8G8B8.
    {AV_PIX_FMT_BGR24, TOPSCODEC_PIX_FMT_BGR888},               // 8bit packed R8G8B8.
    {AV_PIX_FMT_RGB24P, TOPSCODEC_PIX_FMT_RGB3P},               // 8bit planar R-G-B.
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 18, 100)       // n4.x
    {AV_PIX_FMT_X2RGB10BE, TOPSCODEC_PIX_FMT_RGB101010},        // 10bit packed R10G10B10. fixme
    {AV_PIX_FMT_X2RGB10BE, TOPSCODEC_PIX_FMT_BGR101010},        // 10bit packed B10G10R10. fixme
    {AV_PIX_FMT_GRAY10LE, TOPSCODEC_PIX_FMT_MONOCHROME_10BIT},  // 10bit gray scale.
#endif
    {AV_PIX_FMT_GRAY8, TOPSCODEC_PIX_FMT_MONOCHROME},  // 8bit gray scale.
    {AV_PIX_FMT_BGR24P, TOPSCODEC_PIX_FMT_BGR3P}};     // 8bit planar B-G-R.

enum AVPixelFormat topspixfmt_2_avpixfmt(topscodecPixelFormat_t fmt) {
    const size_t map_size = sizeof(pixels_format_map) / sizeof(pixels_format_map[0]);
    for (size_t i = 0; i < map_size; ++i) {
        if (pixels_format_map[i].tops_fmt == fmt) {
            return pixels_format_map[i].av_fmt;
        }
    }
    return AV_PIX_FMT_YUV420P;
}

topscodecPixelFormat_t avpixfmt_2_topspixfmt(enum AVPixelFormat fmt) {
    const size_t map_size = sizeof(pixels_format_map) / sizeof(pixels_format_map[0]);
    for (size_t i = 0; i < map_size; ++i) {
        if (pixels_format_map[i].av_fmt == fmt) {
            return pixels_format_map[i].tops_fmt;
        }
    }
    return TOPSCODEC_PIX_FMT_I420;
}

static void topscodec_free_buffer(void* opaque, uint8_t* unused) {
    int                  ret;
    EFBuffer*            efbuf = opaque;
    EFCodecDecContext_t* ctx   = (EFCodecDecContext_t*)efbuf->ef_context;

    if (atomic_fetch_sub(&efbuf->context_refcount, 1) == 1) {
        ret = ctx->topscodec_lib_ctx->lib_topscodecDecFrameUnmap(ctx->handle, &efbuf->ef_frame);
        if (ret != 0)
            av_log(efbuf->avctx, AV_LOG_ERROR, "topscodecDecFrameUnmap FAILED.\n");
        else
            av_log(efbuf->avctx, AV_LOG_DEBUG, "topscodecDecFrameUnmap SUCCESS.\n");
    }
}

static int topscodec_buf_increase_ref(EFBuffer* efbuf) {
    atomic_fetch_add(&efbuf->context_refcount, 1);
    return 0;
}

static int topscodec_buf_to_bufref(const EFBuffer* efbuf, int plane, AVBufferRef** buf, size_t planesize) {
    int ret = 0;

    if (plane >= efbuf->ef_frame.plane_num) return AVERROR(EINVAL);

    /* even though most encoders return 0 in data_offset encoding vp8 does
    require this value */
    *buf = av_buffer_create((char*)efbuf->ef_frame.plane[plane].dev_addr
                            /*+ efbuf->ef_frame.plane[plane].offline*/,
                            planesize, topscodec_free_buffer, (EFBuffer*)efbuf, 0);
    if (!*buf) return AVERROR(ENOMEM);

    ret = topscodec_buf_increase_ref((EFBuffer*)efbuf);
    if (ret) av_buffer_unref(buf);

    return ret;
}

/******************************************************************************
 *
 *             TOPSCODEC Frame/Pkt interface
 *
 ******************************************************************************/

int ff_topscodec_avframe_to_efbuf(const AVFrame* avframe, EFBuffer* efbuf) {
    topsError_t            tops_ret;
    void*                  data;
    int                    nBytes;
    AVCodecContext*        avctx       = NULL;
    EFCodecDecContext_t*   ctx         = NULL;
    TopsRuntimesFunctions* topsruntime = NULL;
    av_assert0(avframe);
    av_assert0(efbuf);

    avctx       = efbuf->avctx;
    ctx         = avctx->priv_data;
    topsruntime = ctx->topsruntime_lib_ctx;

    memset(efbuf, 0, sizeof(topscodecFrame_t));
    efbuf->ef_frame.pts          = avframe->pts;
    efbuf->ef_frame.pixel_format = avpixfmt_2_topspixfmt(avframe->format);
    efbuf->ef_frame.color_space  = topscodec_get_color_space(avframe);
    efbuf->ef_frame.width        = avframe->width;
    efbuf->ef_frame.height       = avframe->height;
    efbuf->ef_frame.plane_num    = av_pix_fmt_count_planes(avframe->format);

    for (int i = 0; i < efbuf->ef_frame.plane_num; i++) {
        efbuf->ef_frame.plane[i].stride = avframe->linesize[i];
        data                            = (void*)efbuf->ef_frame.plane[i].dev_addr;
        /*for encodeing, only support yuv420p*/
        nBytes = efbuf->ef_frame.plane[i].stride * efbuf->ef_frame.height * (i ? 1.0 / 2 : 1);
        av_assert0(data);
        av_assert0(avframe->data[i]);
        av_assert0(nBytes > 0);
        tops_ret = topsruntime->lib_topsMemcpyHtoD(data, avframe->data[i], nBytes);
        if (tops_ret != topsSuccess) {
            av_log(avctx, AV_LOG_ERROR, "h2d: host %p -> dev %p, size %d fuc: %s, line:%d\n", avframe->data[i], data,
                   nBytes, __func__, __LINE__);
            return AVERROR_BUG;
        }
    }
    return 0;
}

int ff_topscodec_efbuf_to_avframe(const EFBuffer* efbuf, AVFrame* avframe) {
    int                    ret          = 0;
    AVCodecContext*        avctx        = NULL;
    EFCodecDecContext_t*   ctx          = NULL;
    AVHWFramesContext*     hw_frame_ctx = NULL;
    TopsRuntimesFunctions* topsruntime  = NULL;
    TopsCodecFunctions*    topscodec    = NULL;

    int       linesizes[4]  = {0};
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

    /*reset w and h*/
    hw_frame_ctx->height = efbuf->ef_frame.height;
    hw_frame_ctx->width  = efbuf->ef_frame.width;
    avctx->height        = efbuf->ef_frame.height;
    avctx->width         = efbuf->ef_frame.width;
    avframe->format      = topspixfmt_2_avpixfmt(efbuf->ef_frame.pixel_format);

    ret = av_image_fill_linesizes(linesizes, avframe->format, avframe->width);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "av_image_fill_linesizes failed.\n");
        return AVERROR_BUG;
    }

    for (int i = 0; i < 4; i++) {
        linesizes1[i] = linesizes[i];
        data[i]       = (uint8_t*)efbuf->ef_frame.plane[i].dev_addr;
        av_log(avctx, AV_LOG_DEBUG, "ptrlinesizes[%d]:%ld\n", i, linesizes1[i]);
    }

    ret = av_image_fill_plane_sizes(planesizes, avframe->format, avframe->height, linesizes1);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "av_image_fill_plane_sizes failed.\n");
        return AVERROR_BUG;
    }

    if (av_pix_fmt_count_planes(avframe->format) != efbuf->ef_frame.plane_num) {
        av_log(avctx, AV_LOG_ERROR,
               "pix:%s,efbuf plane [%d]is not suitable for "
               "ffmpeg[%d].\n",
               av_get_pix_fmt_name(avframe->format), efbuf->ef_frame.plane_num,
               av_pix_fmt_count_planes(avframe->format));
        return AVERROR_BUG;
    }

    /* 1. get references to the actual data */
    if (!ctx->zero_copy) { /*Not support yet*/
        // pthread_mutex_lock(&g_buf_mutex);
        av_hwframe_get_buffer(avctx->hw_frames_ctx, avframe, 0);
        // pthread_mutex_unlock(&g_buf_mutex);

        for (int i = 0; i < efbuf->ef_frame.plane_num; i++) {
            avframe->linesize[i] = efbuf->ef_frame.plane[i].stride;
            if (avframe->linesize[i] != linesizes[i]) {
                av_log(avctx, AV_LOG_ERROR,
                       "linesize[%d] is errefbuf linesize:%d,av "
                       "linesize:%d\n",
                       i, avframe->linesize[i], linesizes[i]);
                av_frame_unref(avframe);
                return AVERROR_BUG;
            }

            if (planesizes[i] == 0) {
                av_log(ctx, AV_LOG_ERROR, "planesizes[%d] err,value:%lu\n", i, planesizes[i]);
                av_frame_unref(avframe);
                return AVERROR_BUG;
            }
            av_assert0(planesizes[i] > 0);
            av_assert0(data[i]);
            av_assert0(avframe->data[i]);
            ret = topsruntime->lib_topsMemcpyDtoD(avframe->data[i], data[i], planesizes[i]);
            if (ret != topsSuccess) {
                av_log(ctx, AV_LOG_ERROR, "d2x: dev %p -> dev 0x%p, size %lu\n", data[i], (void*)avframe->data[i],
                       planesizes[i]);
                av_log(ctx, AV_LOG_ERROR,
                       "topsMemcpyDtoD error occur, func: %s, "
                       "line: %d\n",
                       __func__, __LINE__);
                av_frame_unref(avframe);
                return AVERROR_BUG;
            }
            av_log(avctx, AV_LOG_DEBUG, "d2d: dev %p -> dev %p, size %lu\n", data[i], avframe->data[i], planesizes[i]);
        }  // for
        ret = topscodec->lib_topscodecDecFrameUnmap(ctx->handle, &efbuf->ef_frame);
        if (ret != 0) {
            av_log(avctx, AV_LOG_ERROR, "topscodecDecFrameUnmap FAILED.\n");
            av_frame_unref(avframe);
            return AVERROR_BUG;
        }
        av_log(avctx, AV_LOG_DEBUG, "topscodecDecFrameUnmap SUCCESS.\n");
    } else { /*zero copy*/
        for (int i = 0; i < efbuf->ef_frame.plane_num; i++) {
            ret = topscodec_buf_to_bufref(efbuf, i, &avframe->buf[i], planesizes[i]);
            if (ret) return ret;

            avframe->linesize[i] = efbuf->ef_frame.plane[i].stride;
            avframe->data[i]     = avframe->buf[i]->data;
        }
        // 当zero_copy= 1的时候，av_hwframe_get_buffer会执行下面这条命令，所以这条指令务必在这个{}中。
        avframe->hw_frames_ctx = av_buffer_ref(avctx->hw_frames_ctx);
    }

    // if (avctx->pkt_timebase.num && avctx->pkt_timebase.den)
    //     avframe->pts = av_rescale_q(efbuf->ef_frame.pts, (AVRational){1, 10000000}, avctx->pkt_timebase);
    // else

    // get packet prop
    if (av_fifo_size(ctx->pkt_prop_fifo) > 0) {
        AVFrame* prop_avframe_tmp;
        av_fifo_generic_read(ctx->pkt_prop_fifo, &prop_avframe_tmp, sizeof(AVFrame*), NULL);
        av_log(avctx, AV_LOG_DEBUG, "prop fifo [%p] Get frame ,size:%d\n", prop_avframe_tmp,
               av_fifo_size(ctx->pkt_prop_fifo));
        av_frame_copy_props(avframe, prop_avframe_tmp);
        avframe->pkt_size = 0;
        avframe->pkt_pos  = 0;
        av_fifo_generic_write(ctx->pkt_prop_fifo, &prop_avframe_tmp, sizeof(AVFrame*), NULL);
        // av_frame_free(&prop_avframe_tmp);
        // dump_frame_info(avframe);
    }

    /* get avframe information */
    avframe->pict_type = tops_2_av_pic_type(efbuf->ef_frame.pic_type);
    av_log(avctx, AV_LOG_DEBUG, "pic_type:%d\n", efbuf->ef_frame.pic_type);
    avframe->key_frame = key_frame(efbuf->ef_frame.pic_type);
    av_log(avctx, AV_LOG_DEBUG, "key_frame:%d\n", avframe->key_frame);
    avframe->pts = efbuf->ef_frame.pts;
    av_log(avctx, AV_LOG_DEBUG, "ef pts:%llu\n", avframe->pts);

    if (!ctx->enable_crop && !ctx->enable_resize) {
        avctx->coded_height = efbuf->ef_frame.height;
        avctx->coded_width  = efbuf->ef_frame.width;
    }

    /* 3. report errors upstream */
    // if (efbuf->ef_frame.pic_type == TOPSCODEC_PIC_TYPE_UNKNOWN) {
    //     av_log(avctx, AV_LOG_ERROR, "driver decode error\n");
    //     avframe->decode_error_flags |= FF_DECODE_ERROR_INVALID_BITSTREAM;
    // }

    return 0;
}

int ff_topscodec_efbuf_to_avpkt(const EFBuffer* efbuf, AVPacket* avpkt) {
    AVCodecContext* avctx = efbuf->avctx;
    av_assert0(avpkt);
    av_assert0(efbuf);

    av_packet_unref(avpkt);

    avpkt->size = efbuf->ef_pkt.data_len;
    avpkt->data = (void*)efbuf->ef_pkt.mem_addr;
    avpkt->pts = avpkt->pts = efbuf->ef_pkt.pts;

    if (efbuf->ef_pkt.stream_type == TOPSCODEC_NALU_TYPE_IDR || efbuf->ef_pkt.stream_type == TOPSCODEC_NALU_TYPE_I)
        avpkt->flags |= AV_PKT_FLAG_KEY;

    if (efbuf->ef_pkt.stream_type == TOPSCODEC_NALU_TYPE_UNKNOWN) {
        av_log(avctx, AV_LOG_ERROR, "driver encode error\n");
        avpkt->flags |= AV_PKT_FLAG_CORRUPT;
    }

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

    efpkt->data_len    = avpkt->size;
    efpkt->data_offset = 0;
    efpkt->mem_type    = TOPSCODEC_MEM_TYPE_HOST;

    if (avpkt->pts < 0)
        efpkt->pts = 0;
    else
        efpkt->pts = avpkt->pts;

    // ctx->count++;
    // efpkt->pts         = ctx->count; //
    efpkt->stream_type = TOPSCODEC_NALU_TYPE_UNKNOWN;
    data               = (void*)ctx->stream_addr;

    if (avpkt->size > 0 && avpkt->data && data) {
        if (avpkt->size < 512) {
            memcpy(data, avpkt->data, avpkt->size);
        } else {
            // pthread_mutex_lock(&g_buf_mutex);
            tops_ret = topsruntimes->lib_topsMemcpyHtoD(data, avpkt->data, avpkt->size);
            if (tops_ret != topsSuccess) {
                av_log(avctx, AV_LOG_ERROR, "topsMemcpyHtoD failed!\n");
                pthread_mutex_unlock(&g_buf_mutex);
                return AVERROR(EPERM);
            }
            av_log(avctx, AV_LOG_DEBUG, "h2d(topsMemcpyHtoD): host %p -> dev %p, size %u \n", avpkt->data, data,
                   efpkt->data_len);
            // pthread_mutex_unlock(&g_buf_mutex);
        }
    }

    efpkt->mem_addr  = ctx->mem_addr;
    efpkt->alloc_len = ctx->stream_buf_size;
    av_log(avctx, AV_LOG_DEBUG, "Buf size:%d, addr:0x%lx \n", ctx->stream_buf_size, efpkt->mem_addr);

    if (avpkt->flags & AV_PKT_FLAG_KEY) efpkt->stream_type = TOPSCODEC_NALU_TYPE_I;

    return 0;
}
