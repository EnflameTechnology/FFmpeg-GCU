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
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include "avcodec.h"
#include "ff_topscodec_buffers.h"
#include "tops/dynlink_tops_loader.h"

#ifndef AVCODEC_EF_TOPSCODEC_DEC_H
#define AVCODEC_EF_TOPSCODEC_DEC_H

typedef struct {
    AVClass *avclass;
    int      device_id;
    int      card_id;
    int      hw_id;
    int      sf;
    int      zero_copy;
    int      output_buf_num;
    int      input_buf_num;

    int trace_flag;
    int enable_crop;
    int enable_resize;
    int enable_rotation;
    int enable_sfo;
    /*crop online*/
    struct {
        int top;
        int bottom;
        int left;
        int right;
    } crop;
    struct {
        int width;
        int height;
        /*!< Downscale mode: 0-Bilinear, 1-Nearest*/
        int mode;
    } resize;
    int rotation;/*90/180/270*/

    u32_t sfo;          /*!< Frame sampling interval*/
    u32_t sf_idr;       /*!< IDR Frame sampling*/

    int in_width;
    int in_height;
    int out_width;
    int out_height;
    int progressive;

    /* null frame/packet received */
    int draining;

    topscodecHandle_t  handle;
    topscodecDecCaps_t caps;
    char *           color_space; /*topscodecColorSpace_t*/
    topscodecType_t    codec_type;
    topscodecRunMode_t run_mode;
    u32_t            stream_buf_size;
    u64_t            stream_addr;

    enum AVPixelFormat output_pixfmt;
    char *             str_output_pixfmt;

    AVBufferRef *      hwdevice;
    AVBufferRef *      hwframe;
    AVHWFramesContext *hwframes_ctx;
    AVCodecContext *   avctx;

    AVPacket av_pkt;
    AVFrame  mid_frame;

    EFBuffer *ef_buf_frame;
    EFBuffer *ef_buf_pkt;

    int64_t last_send_pkt_time;
    volatile int decoder_start;
    volatile int decoder_init_flag;

    // AVMutex count_mutex;
    unsigned long long total_frame_count;
    unsigned long long total_packet_count;

    TopsCodecFunctions *topscodec_lib_ctx;
    TopsRuntimesFunctions *topsruntime_lib_ctx;
    int           recv_first_frame;
    int           recv_outport_eos;
    int           first_packet;
} EFCodecDecContext_t;

#endif  // AVCODEC_EF_TOPSCODEC_DEC_H