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

#ifndef PLATFORMS_GCU_FFMPEG_PLUGIN_SRC_LIBAVCODEC_FF_TOPSCODEC_UTILS_H_
#define PLATFORMS_GCU_FFMPEG_PLUGIN_SRC_LIBAVCODEC_FF_TOPSCODEC_UTILS_H_

#include <dlfcn.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "libavcodec/avcodec.h"
#include "libavcodec/version.h"
#include "libavutil/pixfmt.h"
#include "libavutil/thread.h"
#include "tops/dynlink_tops_loader.h"

#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 18, 100)  // 4.0
#include "libavcodec/codec_id.h"
#else
#endif

/* FFmpeg-GCU plugin version */
#define FFMPEG_GCU_VERSION_MAJOR 3
#define FFMPEG_GCU_VERSION_MINOR 1
#define FFMPEG_GCU_VERSION_PATCH 9

#define FF_TOPSCODEC_MAX_CARD_ID   (31)
#define FF_TOPSCODEC_MAX_DEVICE_ID (7)

#define FFMPEG_GCU_STR_HELPER(x) #x
#define FFMPEG_GCU_STR(x)        FFMPEG_GCU_STR_HELPER(x)
#define FFMPEG_GCU_VERSION_STRING                         \
    FFMPEG_GCU_STR(FFMPEG_GCU_VERSION_MAJOR) "."          \
    FFMPEG_GCU_STR(FFMPEG_GCU_VERSION_MINOR) "."          \
    FFMPEG_GCU_STR(FFMPEG_GCU_VERSION_PATCH)

typedef struct {
    topscodecType_t type;
    const char*     name;
    enum AVCodecID  av_codec;
} efCodecInputDescriptor_t;

int                             get_card_id_from_env(void);
int                             get_device_id_from_env(void);
int                             get_ap_log_on_off_from_env(void);
const efCodecInputDescriptor_t* topscode_descriptor_get_by_type(
    topscodecType_t type);
const efCodecInputDescriptor_t* topscode_descriptor_get_by_name(
    const char* name);
const char* get_event_type_string(topscodecEventType_t eventType);

/* useful pix trans func */
topscodecPixelFormat_t avpixfmt_2_topspixfmt(enum AVPixelFormat fmt);
enum AVPixelFormat     topspixfmt_2_avpixfmt(topscodecPixelFormat_t fmt);
const char*            get_avpixfmt_name(enum AVPixelFormat fmt);
const char*            get_topspixfmt_name(topscodecPixelFormat_t fmt);

void print_frame(AVCodecContext* avctx, topscodecFrame_t* frame,
                 const char* name);
void print_stream(AVCodecContext* avctx, topscodecStream_t* stream);
void print_avframe(AVCodecContext* avctx, const AVFrame* avframe);

/**
 * Calculate width from stride for a given pixel format
 * @param pix_fmt pixel format
 * @param stride stride value in bytes
 * @param plane plane index (0 for Y/luma, 1 for U/Cb, 2 for V/Cr)
 *              For packed formats, use plane 0
 * @return calculated width, or -1 if invalid
 */
int calculate_width_from_stride(enum AVPixelFormat pix_fmt, int stride,
                                int plane);

enum AVPictureType tops_2_av_pic_type(topscodecPicType_t type);
int tops_is_key_frame(topscodecPicType_t type);

void ff_ffmpeg_gcu_print_version(void);

#endif  // PLATFORMS_GCU_FFMPEG_PLUGIN_SRC_LIBAVCODEC_FF_TOPSCODEC_UTILS_H_
