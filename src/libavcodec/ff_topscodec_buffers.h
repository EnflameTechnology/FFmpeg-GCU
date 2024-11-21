/*
 * topscodec buffer helper functions.
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

#ifndef AVCODEC_EF_BUFFERS_H
#define AVCODEC_EF_BUFFERS_H

#include <stdatomic.h>
#include <stddef.h>
#include <tops/dynlink_tops_loader.h>

#include "libavcodec/avcodec.h"
#include "libavutil/buffer.h"
#include "libavutil/frame.h"
#include "libavutil/hwcontext.h"

#if AV_VERSION_INT(LIBAVCODEC_VERSION_MAJOR, LIBAVCODEC_VERSION_MINOR, LIBAVCODEC_VERSION_MICRO) >= \
    AV_VERSION_INT(58, 134, 100)
#include "packet.h"  //not support for 3.2
#endif

typedef struct {
    /* each buffer needs to have a reference to its context */
    AVCodecContext* avctx;
    /* reference back to EFCodecDecContext_t */
    void* ef_context;

    /* This object is refcounted per-plane, so we need to keep track
     * of how many context-refs we are holding. */
    AVBufferRef* context_ref;
    atomic_uint  context_refcount;

    AVPacket* av_pkt;
    /* Reference to a frame. Only used during encoding */
    AVFrame* av_frame;
    /*
    for decodeing , libavcodec do not use topMalloc() to specify ef_frame.plane.
    dev_addr space,caller use the codec core addr.
    for encodeing , libavcodec should use topMalloc() to specify ef_frame.plane.
    dev_addr space,and copy host frame to device frame.then sendto codec core
    */
    topscodecFrame_t ef_frame;
    /*
    for decodeing , libavcodec should use topMalloc() to specify ef_pkt.mem_addr
    space.and copy host pkt to device ,then sendto codec core
    for encodeing ,libavcodec do not use topMalloc() to specify ef_pkt.mem_addr
    space.caller use the codec core addr.
    */
    topscodecStream_t ef_pkt;
} EFBuffer;

/**
 * Extracts the data from a EFBuffer to an AVFrame
 *
 * @param[in] efbuf The EFBuffer to get the information from
 * @param[out] avframe The AVFRame to push the information to
 *
 * @returns 0 in case of success, AVERROR(EINVAL) if the number of planes is
 * incorrect,AVERROR(ENOMEM) if the AVBufferRef can't be created.
 */
int ff_topscodec_efbuf_to_avframe(const EFBuffer* efbuf, AVFrame* avframe);

/**
 * Extracts the data from an AVFrame to a EFBuffer
 *
 * @param[in]  avframe AVFrame to get the data from
 * @param[out]  efbuf EFBuffer to push the information to
 *
 * @returns 0 in case of success, a negative AVERROR code otherwise
 */
int ff_topscodec_avframe_to_efbuf(const AVFrame* avframe, EFBuffer* efbuf);

/**
 * Extracts the data from a EFBuffer to an AVPacket
 *
 * @param[in] efbuf The EFBuffer to get the information from
 * @param[out] avpkt The AVPacket to push the information to
 *
 * @returns 0 in case of success, AVERROR(EINVAL) if the number of planes is
 * incorrect, AVERROR(ENOMEM) if the AVBufferRef can't be created.
 *
 */
int ff_topscodec_efbuf_to_avpkt(const EFBuffer* efbuf, AVPacket* avpkt);

/**
 * Extracts the data from an AVPacket to a EFBuffer
 *
 * @param[in]  pkt AVPacket to get the data from
 * @param[in]  efbuf EFBuffer to push the information to
 *
 * @returns 0 in case of success, a negative AVERROR code otherwise
 */
int ff_topscodec_avpkt_to_efbuf(const AVPacket* pkt, EFBuffer* efbuf);

/* useful pix trans func */
topscodecPixelFormat_t avpixfmt_2_topspixfmt(enum AVPixelFormat fmt);
enum AVPixelFormat     topspixfmt_2_avpixfmt(topscodecPixelFormat_t fmt);

#endif  // AVCODEC_EF_BUFFERS_H
