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

#ifndef PLATFORMS_GCU_FFMPEG_PLUGIN_SRC_LIBAVCODEC_FF_TOPSCODEC_BUFFERS_H_
#define PLATFORMS_GCU_FFMPEG_PLUGIN_SRC_LIBAVCODEC_FF_TOPSCODEC_BUFFERS_H_

#include <stdatomic.h>
#include <stddef.h>
#include <tops/dynlink_tops_loader.h>

#include "libavcodec/avcodec.h"
#include "libavcodec/version.h"
#include "libavutil/buffer.h"
#include "libavutil/frame.h"
#include "libavutil/hwcontext.h"

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 18, 100)
#include "libavcodec/packet.h"  //not support for 3.2
#endif

typedef enum EFBufferType {
    EF_BUFFER_TYPE_FRAME,
    EF_BUFFER_TYPE_PKT,
} EFBufferType;

typedef struct {
    /* each buffer needs to have a reference to its context */
    AVCodecContext* avctx;
    /* reference back to EFCodecDecContext_t */
    void* ef_dec_context;
    /* reference back to EFCodecEncContext_t */
    void* ef_enc_context;

    /* This object is refcounted per-plane, so we need to keep track
     * of how many context-refs we are holding. */
    AVBufferRef* context_ref;
    atomic_uint  context_refcount;

    AVPacket* av_pkt;
    /* Reference to a frame. Only used during encoding */
    AVFrame* av_frame;
    /*
    for decoding , libavcodec do not use topMalloc() to specify ef_frame.plane.
    dev_addr space,caller use the codec core addr.
    for encodeing , libavcodec should use topMalloc() to specify ef_frame.plane.
    dev_addr space,and copy host frame to device frame.then sendto codec core
    */
    topscodecFrame_t ef_frame;

    /*
    for decoding , libavcodec should use topMalloc() to specify ef_pkt.mem_addr
    space.and copy host pkt to device ,then sendto codec core
    for encodeing ,libavcodec do not use topMalloc() to specify ef_pkt.mem_addr
    space.caller use the codec core addr.
    */
    topscodecStream_t ef_pkt;

    EFBufferType type;

    u64_t ef_frame_pkt_buf_size;
    u64_t ef_frame_pkt_buf_size_aligned_4k;
    u64_t ef_frame_pkt_virtual_addr;
    u64_t ef_frame_pkt_phy_addr;
} EFBuffer;

/**
 * Allocates memory for an EFBuffer
 *
 * @param[in] efbuf The EFBuffer to allocate memory for
 *
 * @returns 0 in case of success, a negative AVERROR code otherwise
 */

int ff_topscodec_alloc_efbuf_internal_data(EFBuffer* efbuf);

/**
 * Creates an AVBufferRef wrapping a single plane of an EFBuffer (zero-copy).
 * The EFBuffer's context_refcount is incremented; when all plane refs are
 * released, topscodecDecFrameUnmap is called and the EFBuffer is freed.
 */
int ff_topscodec_buf_to_bufref(const EFBuffer* efbuf, int plane, AVBufferRef** buf, size_t planesize);

/**
 * @brief
 *
 *
 *
 * @param[in] efbuf The EFBuffer to free memory for
 *
 * @returns 0 in case of success, a negative AVERROR code otherwise
 */
int ff_topscodec_free_efbuf_internal_data(EFBuffer* efbuf);

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
int ff_topscodec_avframe_to_efbuf(AVFrame* avframe, EFBuffer* efbuf);

/**
 * Extracts the data from an AVPacket to a EFBuffer
 *
 * @param[in]  pkt AVPacket to get the data from
 * @param[in]  efbuf EFBuffer to push the information to
 *
 * @returns 0 in case of success, a negative AVERROR code otherwise
 */
int ff_topscodec_avpkt_to_efbuf(const AVPacket* pkt, EFBuffer* efbuf);

#endif  // PLATFORMS_GCU_FFMPEG_PLUGIN_SRC_LIBAVCODEC_FF_TOPSCODEC_BUFFERS_H_
