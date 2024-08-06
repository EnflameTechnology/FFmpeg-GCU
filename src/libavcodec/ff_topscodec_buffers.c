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

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include "libavcodec/avcodec.h"
#include "libavcodec/internal.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"
#include "ff_topscodec_buffers.h"
#include "libavutil/imgutils.h"
#include "libavutil/avassert.h"
#include "ff_topscodec_dec.h"

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

static enum AVColorPrimaries topscodec_get_color_primaries(const EFBuffer *buf)
{
    return AVCOL_PRI_UNSPECIFIED;
}

static enum AVColorRange topscodec_get_color_range(const EFBuffer *buf)
{
     return AVCOL_RANGE_UNSPECIFIED;
}

//FixMe
static topscodecColorSpace_t topscodec_get_color_space(const AVFrame *frame)
{
    enum AVColorSpace cs = frame->colorspace;
    topscodecColorSpace_t ret = TOPSCODEC_COLOR_SPACE_BT_601;
    if (cs == AVCOL_SPC_SMPTE170M){
        ret = TOPSCODEC_COLOR_SPACE_BT_601;
    } else if (cs == AVCOL_SPC_BT709){
        ret = TOPSCODEC_COLOR_SPACE_BT_709;
    } else if (cs == AVCOL_SPC_BT2020_CL){
        ret = TOPSCODEC_COLOR_SPACE_BT_2020;
    } else if (cs == AVCOL_SPC_BT470BG){
        ret = TOPSCODEC_COLOR_SPACE_BT_601_ER;
    } else if (cs == AVCOL_SPC_RGB){
        ret = TOPSCODEC_COLOR_SPACE_BT_709_ER;
    } else if (cs == AVCOL_SPC_BT2020_NCL){
        ret = TOPSCODEC_COLOR_SPACE_BT_2020_ER;
    }
    return ret;
}

//FixMe
static enum AVColorSpace topscodec_get_color_space2(const EFBuffer *buf)
{
    topscodecColorSpace_t cs = buf->ef_frame.color_space;
    enum AVColorSpace ret = AVCOL_SPC_UNSPECIFIED;
    if (cs == TOPSCODEC_COLOR_SPACE_BT_601){
        ret = AVCOL_SPC_SMPTE170M;
    } else if (cs == TOPSCODEC_COLOR_SPACE_BT_709){
        ret = AVCOL_SPC_BT709;
    } else if (cs == TOPSCODEC_COLOR_SPACE_BT_2020){
        ret = AVCOL_SPC_BT2020_CL;
    } else if (cs == TOPSCODEC_COLOR_SPACE_BT_601_ER){
        ret = AVCOL_SPC_BT470BG;
    } else if (cs == TOPSCODEC_COLOR_SPACE_BT_709_ER){
        ret = AVCOL_SPC_RGB;
    } else if (cs == TOPSCODEC_COLOR_SPACE_BT_2020_ER){
        ret = AVCOL_SPC_BT2020_NCL;
    }
    return ret;
}

static 
enum AVColorTransferCharacteristic topscodec_get_color_trc(const EFBuffer *buf)
{
    return AVCOL_TRC_UNSPECIFIED;
}

static void topscodec_free_buffer(void *opaque, uint8_t *unused)
{
    int ret;
    EFBuffer* efbuf = opaque;
    EFCodecDecContext_t *ctx = (EFCodecDecContext_t*)efbuf->ef_context;

    if (atomic_fetch_sub(&efbuf->context_refcount, 1) == 1) {
        ret = ctx->topscodec_lib_ctx->lib_topscodecDecFrameUnmap(ctx->handle, 
                                                        &efbuf->ef_frame);
        if (ret != 0)
            av_log(efbuf->avctx, AV_LOG_ERROR, "topscodecDecFrameUnmap FAILED.\n");
        else
            av_log(efbuf->avctx, AV_LOG_DEBUG, "topscodecDecFrameUnmap SUCCESS.\n");
    }
}

static int topscodec_buf_increase_ref(EFBuffer *efbuf)
{
    atomic_fetch_add(&efbuf->context_refcount, 1);
    return 0;
}

static int topscodec_buf_to_bufref(const EFBuffer *efbuf, int plane, 
                                    AVBufferRef **buf, size_t planesize)
{
    int ret = 0;

    if (plane >= efbuf->ef_frame.plane_num)
        return AVERROR(EINVAL);

    /* even though most encoders return 0 in data_offset encoding vp8 does 
    require this value */
    *buf = av_buffer_create((char *)efbuf->ef_frame.plane[plane].dev_addr 
                                /*+ efbuf->ef_frame.plane[plane].offline*/, 
                                planesize, 
                                topscodec_free_buffer, (EFBuffer *)efbuf, 0);
    if (!*buf)
        return AVERROR(ENOMEM);

    ret = topscodec_buf_increase_ref((EFBuffer *)efbuf);
    if (ret)
        av_buffer_unref(buf);

    return ret;
}

/******************************************************************************
 *
 *             TOPSCODEC Frame/Pkt interface
 *
 ******************************************************************************/

int ff_topscodec_avframe_to_efbuf(const AVFrame *avframe, EFBuffer *efbuf)
{
    topsError_t tops_ret;
    void *data;
    int nBytes;
    AVCodecContext *log_ctx = NULL;
    EFCodecDecContext_t *ctx = NULL;
    TopsRuntimesFunctions *topsruntime = NULL;
    av_assert0(avframe);
    av_assert0(efbuf);

    log_ctx = efbuf->avctx;
    ctx = log_ctx->priv_data;
    topsruntime = ctx->topsruntime_lib_ctx;

    memset(efbuf, 0, sizeof(topscodecFrame_t));
    efbuf->ef_frame.pts          = avframe->pts;
    efbuf->ef_frame.pixel_format = avpixfmt_2_topspixfmt(avframe->format);
    efbuf->ef_frame.color_space  = topscodec_get_color_space(avframe); 
    efbuf->ef_frame.width        = avframe->width;
    efbuf->ef_frame.height       = avframe->height;
    efbuf->ef_frame.plane_num    = av_pix_fmt_count_planes(avframe->format);
    
    for (int i = 0; i < efbuf->ef_frame.plane_num; i++){
        efbuf->ef_frame.plane[i].stride = avframe->linesize[i];
        data = (void*)efbuf->ef_frame.plane[i].dev_addr;
        /*for encodeing, only support yuv420p*/
        nBytes = efbuf->ef_frame.plane[i].stride * 
                    efbuf->ef_frame.height * (i ? 1.0 / 2 : 1);
        av_assert0(data);
        av_assert0(avframe->data[i]);
        av_assert0(nBytes > 0);
        tops_ret = topsruntime->lib_topsMemcpyHtoD(data,
                                                    avframe->data[i], nBytes);
        if (tops_ret != topsSuccess) {
            av_log(log_ctx, AV_LOG_ERROR,
                    "h2d: host %p -> dev %p, size %d fuc: %s, line:%d\n", 
                    avframe->data[i], data, nBytes, __func__, __LINE__);
            return AVERROR_BUG;
        }
    }
    return 0;
}

int ff_topscodec_efbuf_to_avframe(const EFBuffer *efbuf, AVFrame *avframe)
{
    int ret                            = 0;
    AVCodecContext *log_ctx            = NULL;
    EFCodecDecContext_t *ctx           = NULL;
    AVHWFramesContext *hw_frame_ctx    = NULL;
    TopsRuntimesFunctions *topsruntime = NULL;
    TopsCodecFunctions   *topscodec    = NULL;

    int linesizes[4]                = {0};
    ptrdiff_t linesizes1[4]         = {0};
    size_t planesizes[4]            = {0};
    uint8_t *data[4]                = {NULL};

    av_assert0(avframe);
    av_assert0(efbuf);

    log_ctx = efbuf->avctx;
    ctx = log_ctx->priv_data;
    topscodec    = ctx->topscodec_lib_ctx;
    topsruntime  = ctx->topsruntime_lib_ctx;
    hw_frame_ctx = (AVHWFramesContext *)log_ctx->hw_frames_ctx->data;

    avframe->height  = efbuf->ef_frame.height;
    avframe->width   = efbuf->ef_frame.width;

    /*reset w and h*/
    hw_frame_ctx->height = efbuf->ef_frame.height;
    hw_frame_ctx->width  = efbuf->ef_frame.width;
    log_ctx->height      = efbuf->ef_frame.height;
    log_ctx->width       = efbuf->ef_frame.width;

    avframe->format  = topspixfmt_2_avpixfmt(efbuf->ef_frame.pixel_format);

    /* 1. get references to the actual data */
    if (!ctx->zero_copy) {/*Not support yet*/
        ret = av_image_fill_linesizes(linesizes, avframe->format, 
                                        avframe->width);
        if (ret < 0) {
            av_log(log_ctx, AV_LOG_ERROR,"av_image_fill_plane_sizes failed.\n");
            return AVERROR_BUG;
        }
                
        for (int i = 0; i < 4; i++) {
            linesizes1[i] = linesizes[i];
            data[i] = (uint8_t*)efbuf->ef_frame.plane[i].dev_addr;
            av_log(log_ctx, AV_LOG_DEBUG, 
                    "ptrlinesizes[%d]:%ld\n", i, linesizes1[i]);
        }

        ret = av_image_fill_plane_sizes(planesizes, avframe->format,
                                        avframe->height, linesizes1);
        if (ret < 0) {
            av_log(log_ctx, AV_LOG_ERROR,"av_image_fill_plane_sizes failed.\n");
            return AVERROR_BUG;
        }

        if (av_pix_fmt_count_planes(avframe->format) != 
                                    efbuf->ef_frame.plane_num) {
            av_log(log_ctx, AV_LOG_ERROR,
                    "pix:%s,efbuf plane [%d]is not suitable for ffmpeg[%d].\n", 
                    av_get_pix_fmt_name(avframe->format),
                    efbuf->ef_frame.plane_num,
                    av_pix_fmt_count_planes(avframe->format));
            return AVERROR_BUG;
        }

        av_hwframe_get_buffer(log_ctx->hw_frames_ctx, avframe, 0);

        for (int i = 0; i < efbuf->ef_frame.plane_num; i++) {
            avframe->linesize[i] = efbuf->ef_frame.plane[i].stride;
            if (avframe->linesize[i] != linesizes[i]) {
                av_log(log_ctx, AV_LOG_ERROR,
                        "linesize[%d] is errefbuf linesize:%d,av linesize:%d\n",
                        i, avframe->linesize[i], linesizes[i]);
                av_frame_unref(avframe);
                return AVERROR_BUG;
            }
            
            if (planesizes[i] == 0) {
                av_log(ctx, AV_LOG_ERROR,
                        "planesizes[%d] err,value:%lu\n", i, planesizes[i]);
                av_frame_unref(avframe);
                return AVERROR_BUG;
            }
            av_assert0(planesizes[i] > 0);
            av_assert0(data[i]);
            av_assert0(avframe->data[i]);
            ret = topsruntime->lib_topsMemcpyDtoD(avframe->data[i],
                                    data[i],
                                    planesizes[i]);
            if (ret != topsSuccess) {
                av_log(ctx, AV_LOG_ERROR,
                        "d2x: host %p -> dev 0x%lx, size %lu\n",
                        data[i], avframe->data[i], planesizes[i]);
                av_log(ctx, AV_LOG_ERROR,
                        "topsMemcpyDtoD error occur, func: %s, line: %d\n",
                        __func__, __LINE__);
                av_frame_unref(avframe);
                return AVERROR_BUG;
            }
            av_log(log_ctx, AV_LOG_DEBUG, 
                    "d2d: host %p -> dev %p, size %lu\n",
                    data[i], avframe->data[i], planesizes[i]);
        }//for
       ret = topscodec->lib_topscodecDecFrameUnmap(ctx->handle, &efbuf->ef_frame);
       if (ret != 0) {
            av_log(log_ctx, AV_LOG_ERROR, "topscodecDecFrameUnmap FAILED.\n");
            av_frame_unref(avframe);
            return AVERROR_BUG;
       }
        av_log(log_ctx, AV_LOG_DEBUG, "topscodecDecFrameUnmap SUCCESS.\n");
    } else {/*zero copy*/
        for (int i = 0; i < efbuf->ef_frame.plane_num; i++){
            ret = topscodec_buf_to_bufref(efbuf, i, &avframe->buf[i], 
                                            planesizes[i]);
            if (ret)
                return ret;

            avframe->linesize[i] = efbuf->ef_frame.plane[i].stride;
            avframe->data[i]     = avframe->buf[i]->data;
        }
        avframe->hw_frames_ctx = av_buffer_ref(log_ctx->hw_frames_ctx);
    }

    /* 2. get avframe information */
    avframe->key_frame = 0;
    if (efbuf->ef_frame.pic_type == TOPSCODEC_PIC_TYPE_IDR  || 
        efbuf->ef_frame.pic_type == TOPSCODEC_PIC_TYPE_I)
        avframe->key_frame = 1;

    avframe->color_primaries = topscodec_get_color_primaries(efbuf);
    avframe->colorspace      = topscodec_get_color_space2(efbuf);
    avframe->color_range     = topscodec_get_color_range(efbuf);
    avframe->color_trc       = topscodec_get_color_trc(efbuf);

    if (log_ctx->pkt_timebase.num && log_ctx->pkt_timebase.den)
        avframe->pts = av_rescale_q(efbuf->ef_frame.pts, (AVRational){1, 10000000}, log_ctx->pkt_timebase);
    else
        avframe->pts = efbuf->ef_frame.pts;

    avframe->pkt_pos = -1;
    avframe->pkt_duration = 0;
    avframe->pkt_size = -1;

    if (!ctx->enable_crop && !ctx->enable_resize){
        log_ctx->coded_height    = efbuf->ef_frame.height;
        log_ctx->coded_width     = efbuf->ef_frame.width;
    }

    avframe->sample_aspect_ratio = (AVRational){0, 1};//unknow
    avframe->interlaced_frame    = ctx->progressive ? 0 : 1;

    /* 3. report errors upstream */
    // if (efbuf->ef_frame.pic_type == TOPSCODEC_PIC_TYPE_UNKNOWN) {
    //     av_log(log_ctx, AV_LOG_ERROR, "driver decode error\n");
    //     avframe->decode_error_flags |= FF_DECODE_ERROR_INVALID_BITSTREAM;
    // }

    return 0;
}

int ff_topscodec_efbuf_to_avpkt(const EFBuffer *efbuf, AVPacket *avpkt)
{
    AVCodecContext *log_ctx = efbuf->avctx;
    av_assert0(avpkt);
    av_assert0(efbuf);

    av_packet_unref(avpkt);

    avpkt->size = efbuf->ef_pkt.data_len;
    avpkt->data = (void*)efbuf->ef_pkt.mem_addr;
    avpkt->dts  = avpkt->pts = efbuf->ef_pkt.pts;

    if (efbuf->ef_pkt.stream_type == TOPSCODEC_NALU_TYPE_IDR ||
        efbuf->ef_pkt.stream_type == TOPSCODEC_NALU_TYPE_I)
        avpkt->flags |= AV_PKT_FLAG_KEY;

    if (efbuf->ef_pkt.stream_type == TOPSCODEC_NALU_TYPE_UNKNOWN) {
        av_log(log_ctx, AV_LOG_ERROR, "driver encode error\n");
        avpkt->flags |= AV_PKT_FLAG_CORRUPT;
    }

    return 0;
}

int ff_topscodec_avpkt_to_efbuf(const AVPacket *avpkt, EFBuffer *efbuf)
{
    void *data               = NULL;
    void *tmp                = NULL;
    topscodecStream_t *efpkt = NULL;
    AVCodecContext *avctx    = NULL;
    EFCodecDecContext_t *ctx = NULL;
    TopsRuntimesFunctions *topsruntimes = NULL;
        
    topsError_t tops_ret;
    topsPointerAttribute_t att;
    int reget_addr = 0;

    av_assert0(avpkt);
    av_assert0(efbuf);

    efpkt = &efbuf->ef_pkt;
    avctx = efbuf->avctx;
    ctx = avctx->priv_data;
    topsruntimes = ctx->topsruntime_lib_ctx;

    efpkt->data_len = avpkt->size;
    efpkt->data_offset = 0;
    efpkt->mem_type = TOPSCODEC_MEM_TYPE_HOST;
    if (avpkt->pts < 0)
         efpkt->pts = 0;
    else
        efpkt->pts = avpkt->pts;
        
    efpkt->stream_type = TOPSCODEC_NALU_TYPE_UNKNOWN;

    if (!ctx->stream_addr){
        pthread_mutex_lock(&g_mutex);
        tops_ret = topsruntimes->lib_topsExtMallocWithFlags(&tmp, 
                            ctx->stream_buf_size, topsMallocHostAccessable);
        if (topsSuccess != tops_ret) {
            av_log(avctx, AV_LOG_ERROR,
                    "Error, topsMalloc failed, ret(%d)\n", tops_ret);
            return AVERROR(EPERM);
        }
        ctx->stream_addr = (uint64_t)tmp;
        reget_addr = 1;
        av_log(avctx, AV_LOG_DEBUG, "malloc stream_addr:0x%lx\n", 
                                                            ctx->stream_addr);
        pthread_mutex_unlock(&g_mutex);
    }

    data = (void*)ctx->stream_addr;
    
    if (avpkt->size > 0 && avpkt->data && data){
        // memcpy(data, avpkt->data, avpkt->size);
        pthread_mutex_lock(&g_mutex);
        tops_ret = topsruntimes->lib_topsMemcpyHtoD(data, avpkt->data, 
                                                    avpkt->size);
        if (tops_ret != topsSuccess) {
            av_log(avctx, AV_LOG_ERROR, "topsMemcpyHtoD failed!\n");
            return AVERROR(EPERM);
        }
        av_log(avctx, AV_LOG_DEBUG, 
                    "h2d(topsMemcpyHtoD): host %p -> dev %p, size %u \n",
                    avpkt->data, data, efpkt->data_len);
        pthread_mutex_unlock(&g_mutex);
    }

    /*get device addr*/
    if (reget_addr) {
        pthread_mutex_lock(&g_mutex);
        tops_ret = topsruntimes->lib_topsPointerGetAttributes(&att, 
                                                    (void *)(ctx->stream_addr));
        if (tops_ret != topsSuccess) {
            av_log(avctx, AV_LOG_ERROR, "topsPointerGetAttributes failed!\n");
            return AVERROR(EPERM);
        }
        ctx->mem_addr  = (u64_t)att.device_pointer;
        pthread_mutex_unlock(&g_mutex);
    }
    efpkt->mem_addr  = ctx->mem_addr;
    efpkt->alloc_len = ctx->stream_buf_size;
    av_log(avctx, AV_LOG_DEBUG, "Buf size:%d, addr:0x%lx \n",
            ctx->stream_buf_size, efpkt->mem_addr);
    
    if (avpkt->flags & AV_PKT_FLAG_KEY)
        efpkt->stream_type = TOPSCODEC_NALU_TYPE_I;

    return 0;
}

topscodecPixelFormat_t avpixfmt_2_topspixfmt(enum AVPixelFormat fmt)
{
    topscodecPixelFormat_t out = TOPSCODEC_PIX_FMT_I420;

    if (fmt == AV_PIX_FMT_NV12)
        out = TOPSCODEC_PIX_FMT_NV12;
    else if (fmt == AV_PIX_FMT_NV21)
        out = TOPSCODEC_PIX_FMT_NV21;
    else if (fmt == AV_PIX_FMT_YUV420P)
        out = TOPSCODEC_PIX_FMT_I420;
    else if (fmt == AV_PIX_FMT_YUV444P)
        out = TOPSCODEC_PIX_FMT_YUV444;
    else if (fmt == AV_PIX_FMT_RGB24)
        out = TOPSCODEC_PIX_FMT_RGB888;
    else if (fmt == AV_PIX_FMT_BGR24)
        out = TOPSCODEC_PIX_FMT_BGR888;
    else if (fmt == AV_PIX_FMT_RGB24P)
        out = TOPSCODEC_PIX_FMT_RGB3P;
    else if (fmt == AV_PIX_FMT_BGR24P)
        out = TOPSCODEC_PIX_FMT_BGR3P;
    else if (fmt == AV_PIX_FMT_GRAY8)
        out = TOPSCODEC_PIX_FMT_MONOCHROME;
    else if (fmt == AV_PIX_FMT_GRAY10LE)
        out = TOPSCODEC_PIX_FMT_MONOCHROME_10BIT;
    else if (fmt == AV_PIX_FMT_YUV444P10LE)
        out = TOPSCODEC_PIX_FMT_YUV444_10BIT;
    else if (fmt == AV_PIX_FMT_P010LE_EF)
        out = TOPSCODEC_PIX_FMT_P010LE;
    else if (fmt == AV_PIX_FMT_P010LE)
        out = TOPSCODEC_PIX_FMT_P010;

    return out;
}

enum AVPixelFormat topspixfmt_2_avpixfmt(topscodecPixelFormat_t fmt)
{
    enum AVPixelFormat out = AV_PIX_FMT_YUV420P;

    if (fmt == TOPSCODEC_PIX_FMT_NV12)
        out = AV_PIX_FMT_NV12;
    else if (fmt == TOPSCODEC_PIX_FMT_NV21)
        out = AV_PIX_FMT_NV21;
    else if (fmt == TOPSCODEC_PIX_FMT_RGB888)
        out = AV_PIX_FMT_RGB24;
    else if (fmt == TOPSCODEC_PIX_FMT_BGR888)
        out = AV_PIX_FMT_BGR24;
    else if (fmt == TOPSCODEC_PIX_FMT_MONOCHROME)
        out = AV_PIX_FMT_GRAY8;
    else if (fmt == TOPSCODEC_PIX_FMT_YUV444)
        out = AV_PIX_FMT_YUV444P;
    else if (fmt == TOPSCODEC_PIX_FMT_P010LE)
        out = AV_PIX_FMT_P010LE_EF;
    else if (fmt == TOPSCODEC_PIX_FMT_P010)
        out = AV_PIX_FMT_P010LE;
    else if (fmt == TOPSCODEC_PIX_FMT_RGB3P)
        out = AV_PIX_FMT_RGB24P;
    else if (fmt == TOPSCODEC_PIX_FMT_BGR3P)
        out = AV_PIX_FMT_BGR24P;
    else if (fmt == TOPSCODEC_PIX_FMT_MONOCHROME_10BIT)
        out = AV_PIX_FMT_GRAY10LE;
    else if (fmt == TOPSCODEC_PIX_FMT_YUV444_10BIT)
        out = AV_PIX_FMT_YUV444P10LE;

    return out;
}

