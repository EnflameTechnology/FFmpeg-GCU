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

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(59, 27, 100)  // 5.1
#include "config_components.h"                             // NOLINT
#include "libavcodec/codec_desc.h"
#include "libavcodec/codec_id.h"
#include "libavcodec/codec_internal.h"
#include "libavcodec/encode.h"
#include "libavcodec/hwconfig.h"
#elif LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 18, 100)  // 4.0
#include "libavcodec/encode.h"    //3.2 is not support
#include "libavcodec/hwconfig.h"  //3.2 is not support
#else
#endif

#ifndef PLATFORMS_GCU_FFMPEG_PLUGIN_SRC_LIBAVCODEC_FF_TOPSCODEC_ENC_H_
#define PLATFORMS_GCU_FFMPEG_PLUGIN_SRC_LIBAVCODEC_FF_TOPSCODEC_ENC_H_

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
#define AVFifoBuffer AVFifo
#define av_fifo_size av_fifo_can_read
// #define av_fifo_alloc av_fifo_alloc2
// #define av_fifo_generic_read av_fifo_read
// #define av_fifo_generic_write av_fifo_write
#define av_fifo_freep av_fifo_freep2
#endif

typedef struct EFCodecEncContext {
    AVClass* avclass;
    int      device_id;
    int      card_id;

    int extra_sei;

    int in_width;
    int in_height;
    int out_width;
    int out_height;
    int frame_size;

    int encoder_init_flag;

    atomic_int recv_outport_eos;
    int        close_flag;
    int        total_packet_count;
    int        total_frame_count;
    int        first_iframe;

    AVFifoBuffer*   mid_avpacket_fifo;
    pthread_mutex_t mid_avpacket_fifo_mutex;
    pthread_cond_t  mid_avpacket_fifo_cond;
    int             mid_avpacket_fifo_mutex_inited;
    int             mid_avpacket_fifo_cond_inited;

    AVFifoBuffer*   import_frame_fifo;
    pthread_mutex_t import_frame_fifo_mutex;
    sem_t           import_frame_fifo_sem;
    int             import_frame_fifo_mutex_inited;
    int             import_frame_fifo_sem_inited;

    AVFifoBuffer*   frame_timestamp_queue;
    uint64_t        output_frame_num;
    int64_t         initial_delay_time;

    int enable_crop;
    int enable_rotation;
    int enable_flip;  // horizontal flip and vertiacal flip
    int flip_model;   // 1-horizontal, 2-vertical
    uint8_t sf; // switch frame number

    int balance;

    struct {
        int top;
        int bottom;
        int left;
        int right;
    } crop;

    int rotation; /*90/180/270*/


    int                      draining;
    topscodecHandle_t        handle;
    topscodecEncCaps_t       caps;
    topscodecEncCreateInfo_t create_info;
    topscodecEncParams_t     encode_param;
    topscodecEncPicAttr_t    frame_attr;

    EFBuffer* ef_buf_frame;
    EFBuffer* ef_buf_pkt;

    AVFrame*               frame;
    AVFrame*               sw_frame;
    AVPacket*              pkt;
    AVPacket*              spspps_pkt;
    AVBufferRef*           hwdevice;
    AVBufferRef*           hwframe;
    AVHWFramesContext*     hwframe_ctx;
    AVCodecContext*        avctx;
    enum AVPixelFormat     data_pix_fmt_av;
    topscodecPixelFormat_t data_pix_fmt_topscodec;

    TopsCodecFunctions*    topscodec_lib_ctx;
    TopsRuntimesFunctions* topsruntime_lib_ctx;

    topscodecType_t codec_type;

    int out_port_num;
    int in_port_num;

    size_t last_frame_pts;

    u64_t  L3_addr_output;
    u64_t  L3_addr_input;
    void*  buffer_input_frame;
    void*  buffer_output_pkt;
    size_t buffer_input_frame_size;
    size_t buffer_output_pkt_size;

    unsigned int output_buf_size_each_frame;
    unsigned int input_buf_size_each_frame;
    unsigned int output_stream_stride_align;
    unsigned int input_frame_stride_align;
    unsigned int color_range;
    unsigned int color_primaries;
    unsigned int color_trc;
    unsigned int colorspace;
    unsigned int conv_mode;
    unsigned int enc_fps;
    unsigned int enc_bitrate;
    unsigned int enc_rate_control;  // 0-fixedqp, 1-cqp, 2-cbr, 3-vbr, 4-cvbr
    unsigned int enc_gop_type;
    unsigned int enc_b_frame_num;

    int enc_gop;
    int profile;
    int level;
    int gop_size;
    int qp_i;
    int qp_p;
    int qp_b;
    int init_qp_i;
    int init_qp_p;
    int init_qp_b;

    // hdr mode
    int hdr_mode;  // 0-none, 1-sdr, 2-hdr10, 3-hdr10+, 4-dolbyvision
    // hdr params
    unsigned int hdr_flags;
    unsigned int hdr_matrix;
    unsigned int hdr_matrix_range;
    unsigned int hdr_primaries;
    unsigned int hdr_transfer;
    unsigned int hdr_display_r_x;
    unsigned int hdr_display_r_y;
    unsigned int hdr_display_g_x;
    unsigned int hdr_display_g_y;
    unsigned int hdr_display_b_x;
    unsigned int hdr_display_b_y;
    unsigned int hdr_display_w_x;
    unsigned int hdr_display_w_y;
    unsigned int hdr_display_lum_min;
    unsigned int hdr_display_lum_max;
    unsigned int hdr_content_lum_max;
    unsigned int hdr_content_lum_avg;
    unsigned int hdr_aspect_ratio_idc;
    unsigned int hdr_sar_width;
    unsigned int hdr_sar_height;
    unsigned int hdr_num_units_in_tick;
    unsigned int hdr_time_scale;

    topscodecEncSeiAttr_t* sei_attr;
    char*                  sei_payload_L3_addr;

    char* hdr10plus_t35_buff_host_addr;
    char* hdr10plus_t35_buff_L3_addr;
    int   hdr10plus_t35_buff_alloc_size;
    int   hdr10plus_t35_buff_L3_data_size;
} EFCodecEncContext_t;

#endif  // PLATFORMS_GCU_FFMPEG_PLUGIN_SRC_LIBAVCODEC_FF_TOPSCODEC_ENC_H_
