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
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#include "libavcodec/avcodec.h"
#include "libavcodec/ff_topscodec_buffers.h"
#include "libavcodec/version.h"
#include "libavutil/fifo.h"
#include "tops/dynlink_tops_loader.h"

#ifndef PLATFORMS_GCU_FFMPEG_PLUGIN_SRC_LIBAVCODEC_FF_TOPSCODEC_DEC_H_
#define PLATFORMS_GCU_FFMPEG_PLUGIN_SRC_LIBAVCODEC_FF_TOPSCODEC_DEC_H_
#define MAX_FRAME_NUM 16

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
#define AVFifoBuffer AVFifo
#define av_fifo_size av_fifo_can_read
#define av_fifo_freep av_fifo_freep2
#endif

typedef struct {
    AVClass* avclass;
    int      device_id;
    int      card_id;
    int      callback;
    int      hw_id;
    uint8_t  sf;
    int      zero_copy;
    int      output_buf_num;
    int      input_buf_num;

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
    int rotation; /*90/180/270*/

    u32_t sfo;    /*!< Frame sampling interval*/
    u32_t sf_idr; /*!< IDR Frame sampling*/

    int in_width;
    int in_height;
    int out_width;
    int out_height;

    int balance;

    int                draining;
    topscodecHandle_t  handle;
    topscodecDecCaps_t caps;
    char*              color_space; /*topscodecColorSpace_t*/
    topscodecType_t    codec_type;

    enum AVPixelFormat output_pixfmt;
    char*              str_output_pixfmt;

    AVBufferRef*       hwdevice;
    AVBufferRef*       hwframe;

    AVPacket*       av_pkt;
    AVFrame         mid_frame;  // sync mode only

    pthread_mutex_t frame_fifo_mutex;
    pthread_cond_t  frame_fifo_cond;
    AVFifoBuffer*   frame_fifo;          // async mode: topscodecFrame_t by value
    sem_t           send_avpacket_sem;   // async mode

    AVFifoBuffer*   mid_avframe_fifo;    // sync mode: AVFrame*

    pthread_mutex_t pkt_prop_mutex;
    AVFifoBuffer*   pkt_prop_fifo;
    AVFrame*        pkt_prop_frame;      // async mode: replaces FIFO

    EFBuffer* ef_buf_pkt;

    int decoder_init_flag;
    int first_packet;
    uint64_t total_frame_count;
    uint64_t total_packet_count;

    TopsCodecFunctions*    topscodec_lib_ctx;
    TopsRuntimesFunctions* topsruntime_lib_ctx;
    atomic_uint eos_event_flag;
    atomic_uint close_flag;
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 18, 100)  // 3.x
    AVBSFContext* bsf;
#endif
    uint32_t stride_align;
} EFCodecDecContext_t;

#endif  // PLATFORMS_GCU_FFMPEG_PLUGIN_SRC_LIBAVCODEC_FF_TOPSCODEC_DEC_H_
