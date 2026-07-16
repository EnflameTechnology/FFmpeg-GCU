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

#include "libavcodec/ff_topscodec_utils.h"

#include <stdio.h>
#include <stdlib.h>

#include "libavcodec/avcodec.h"
#include "libavcodec/version.h"
#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"

/****************************************************************************
 * topscodec descriptor
 ****************************************************************************/
#define ARRAY_ELEMS(a) (sizeof(a) / sizeof((a)[0]))

static const efCodecInputDescriptor_t topscodec_input_descriptors[] = {
    {.type     = TOPSCODEC_MPEG1,
     .name     = "mpeg1video",
     .av_codec = AV_CODEC_ID_MPEG1VIDEO},
    {.type     = TOPSCODEC_MPEG2,
     .name     = "mpeg2video",
     .av_codec = AV_CODEC_ID_MPEG2VIDEO},
    {.type = TOPSCODEC_MPEG4, .name = "mpeg4", .av_codec = AV_CODEC_ID_MPEG4},
    {.type = TOPSCODEC_H263, .name = "h263", .av_codec = AV_CODEC_ID_H263},
    {.type = TOPSCODEC_H264, .name = "h264", .av_codec = AV_CODEC_ID_H264},
    {.type = TOPSCODEC_HEVC, .name = "hevc", .av_codec = AV_CODEC_ID_HEVC},
    {.type = TOPSCODEC_VP8, .name = "vp8", .av_codec = AV_CODEC_ID_VP8},
    {.type = TOPSCODEC_VP9, .name = "vp9", .av_codec = AV_CODEC_ID_VP9},
    {.type = TOPSCODEC_AVS, .name = "cavs", .av_codec = AV_CODEC_ID_CAVS},
    {.type = TOPSCODEC_JPEG, .name = "mjpeg", .av_codec = AV_CODEC_ID_MJPEG},
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 18, 100)  // 4.0
    {.type = TOPSCODEC_AV1, .name = "av1", .av_codec = AV_CODEC_ID_AV1},
    {.type = TOPSCODEC_AVS2, .name = "avs2", .av_codec = AV_CODEC_ID_AVS2},
    {.type = TOPSCODEC_VC1, .name = "vc1", .av_codec = AV_CODEC_ID_VC1},
#else
#endif
};

static const efCodecInputDescriptor_t* topscodec_descriptor_next(
    const efCodecInputDescriptor_t* prev) {
    ptrdiff_t index;
    if (!prev) return &topscodec_input_descriptors[0];

    index = prev - topscodec_input_descriptors;
    if (index >= 0 &&
        index < (ptrdiff_t)ARRAY_ELEMS(topscodec_input_descriptors) - 1)
        return prev + 1;
    return NULL;
}

static int topscodec_descriptor_compare(const void* key, const void* member) {
    topscodecType_t                 type = *(const topscodecType_t*)key;
    const efCodecInputDescriptor_t* desc =
        (const efCodecInputDescriptor_t*)member;
    return type - desc->type;
}

const efCodecInputDescriptor_t* topscode_descriptor_get_by_type(
    topscodecType_t type) {
    const efCodecInputDescriptor_t* desc = NULL;
    desc                                 = (efCodecInputDescriptor_t*)bsearch(
        (const void*)&type, (const void*)topscodec_input_descriptors,
        ARRAY_ELEMS(topscodec_input_descriptors),
        sizeof(topscodec_input_descriptors[0]), topscodec_descriptor_compare);
    return desc;
}

const efCodecInputDescriptor_t* topscode_descriptor_get_by_name(
    const char* name) {
    const efCodecInputDescriptor_t* desc = NULL;
    if (!name) return NULL;

    while ((desc = topscodec_descriptor_next(desc)))
        if (!strcmp(desc->name, name)) return desc;
    return NULL;
}

const char* get_event_type_string(topscodecEventType_t eventType) {
    switch (eventType) {
        case TOPSCODEC_EVENT_NEW_FRAME:
            return "NEW_FRAME";
        case TOPSCODEC_EVENT_SEQUENCE:
            return "SEQUENCE";
        case TOPSCODEC_EVENT_EOS:
            return "EOS";
        case TOPSCODEC_EVENT_FRAME_PROCESSED:
            return "FRAME_PROCESSED";
        case TOPSCODEC_EVENT_BITSTREAM_PROCESSED:
            return "BITSTREAM_PROCESSED";
        case TOPSCODEC_EVENT_OUT_OF_MEMORY:
            return "OUT_OF_MEMORY";
        case TOPSCODEC_EVENT_STREAM_CORRUPT:
            return "STREAM_CORRUPT";
        case TOPSCODEC_EVENT_STREAM_NOT_SUPPORTED:
            return "STREAM_NOT_SUPPORTED";
        case TOPSCODEC_EVENT_BUFFER_OVERFLOW:
            return "BUFFER_OVERFLOW";
        case TOPSCODEC_EVENT_FATAL_ERROR:
            return "FATAL_ERROR";
        default:
            return "UNKNOWN_EVENT";
    }
}

int get_card_id_from_env(void) {
    char* card_id_str = getenv("TOPSCODEC_CARD_ID");
    if (card_id_str == NULL) {
        return 0;
    }
    return atoi(card_id_str);
}

int get_device_id_from_env(void) {
    char* device_id_str = getenv("TOPSCODEC_DEVICE_ID");
    if (device_id_str == NULL) {
        return 0;
    }
    return atoi(device_id_str);
}

int get_ap_log_on_off_from_env(void) {
    char* ap_log_on_off_str = getenv("TOPSCODEC_AP_LOG");
    if (ap_log_on_off_str == NULL) {
        return 0;
    }
    return atoi(ap_log_on_off_str);
}

void print_stream(AVCodecContext* avctx, topscodecStream_t* stream) {
    av_log(avctx, AV_LOG_DEBUG, "stream info {                   \n");
    av_log(avctx, AV_LOG_DEBUG, "\t stream addr(0x%lx)           \t\n",
           stream->mem_addr);
    av_log(avctx, AV_LOG_DEBUG, "\t data_offset(%d)              \t\n",
           stream->data_offset);
    av_log(avctx, AV_LOG_DEBUG, "\t alloc_len(%d)                \t\n",
           stream->alloc_len);
    av_log(avctx, AV_LOG_DEBUG, "\t data_len(%d)                 \t\n",
           stream->data_len);
    av_log(avctx, AV_LOG_DEBUG, "\t pts(%ld)                     \t\n",
           stream->pts);
    av_log(avctx, AV_LOG_DEBUG, "\t stream_type(%u)              \t\n",
           stream->stream_type);
    av_log(avctx, AV_LOG_DEBUG, "\t mem_type(%s)                 \t\n",
           stream->mem_type ? "TOPSCODEC_MEM_TYPE_DEV"
                            : "TOPSCODEC_MEM_TYPE_HOST");
    av_log(avctx, AV_LOG_DEBUG, "\t                             }\t\n");
}

void print_frame(AVCodecContext* avctx, topscodecFrame_t* frame,
                 const char* name) {
    av_log(avctx, AV_LOG_DEBUG, "frame info(%s) {  \n", name);
    av_log(avctx, AV_LOG_DEBUG, "\t frame->plane_num(%d)            \t\n",
           frame->plane_num);
    for (int i = 0; i < frame->plane_num; i++) {
        av_log(avctx, AV_LOG_DEBUG, "\t frame addr(0x%lx)            \t\n",
               frame->plane[i].dev_addr);
        av_log(avctx, AV_LOG_DEBUG, "\t stride(%d)                   \t\n",
               frame->plane[i].stride);
        av_log(avctx, AV_LOG_DEBUG, "\t alloc_len(%d)                \t\n",
               frame->plane[i].alloc_len);
    }
    av_log(avctx, AV_LOG_DEBUG, "\t width(%d)                    \t\n",
           frame->width);
    av_log(avctx, AV_LOG_DEBUG, "\t height(%d)                    \t\n",
           frame->height);
    av_log(avctx, AV_LOG_DEBUG, "\t type(%d)                     \t\n",
           frame->pic_type);
    av_log(avctx, AV_LOG_DEBUG, "\t pixel_fmt(%d)(%s)                \t\n",
           frame->pixel_format, get_topspixfmt_name(frame->pixel_format));
    av_log(avctx, AV_LOG_DEBUG, "\t pts(%lu)                     \t\n",
           frame->pts);
    av_log(avctx, AV_LOG_DEBUG, "\t mem_channel(%d)                 \t\n",
           frame->mem_channel);
    av_log(avctx, AV_LOG_DEBUG, "\t                             }\t\n");
}

void print_avframe(AVCodecContext* avctx, const AVFrame* avframe) {
    av_log(avctx, AV_LOG_DEBUG, "===== AVFrame Properties =====\n");
    // 基本属性
    // av_log(NULL, AV_LOG_DEBUG, "key_frame:%d\n", avframe->key_frame);
    av_log(avctx, AV_LOG_DEBUG, "format(%d): %s\n", avframe->format,
           av_get_pix_fmt_name(avframe->format));
    av_log(avctx, AV_LOG_DEBUG, "pict_type: %d (%c)\n", avframe->pict_type,
           av_get_picture_type_char(avframe->pict_type));

    // PTS和时间基
    av_log(avctx, AV_LOG_DEBUG, "pts: %" PRId64 "\n", avframe->pts);
    // av_log(NULL, AV_LOG_DEBUG, "time_base: %d/%d\n", avframe->time_base.num,
    // avframe->time_base.den);

    // av_log(avctx, AV_LOG_DEBUG, "frame pos:%ld\n", avframe->pkt_pos);
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 3, 100)  // n7.0
    av_log(avctx, AV_LOG_DEBUG, "frame duration:%ld\n", avframe->duration);
#else
    av_log(avctx, AV_LOG_DEBUG, "frame duration:%ld\n", avframe->pkt_duration);
#endif
    // av_log(NULL, AV_LOG_DEBUG, "frame pkt_size:%d\n", avframe->pkt_size);

    // 色彩空间
    av_log(avctx, AV_LOG_DEBUG, "color_primaries: %d (%s)\n",
           avframe->color_primaries,
           av_color_primaries_name(avframe->color_primaries));
    av_log(avctx, AV_LOG_DEBUG, "color_trc: %d (%s)\n", avframe->color_trc,
           av_color_transfer_name(avframe->color_trc));
    av_log(avctx, AV_LOG_DEBUG, "colorspace: %d (%s)\n", avframe->colorspace,
           av_color_space_name(avframe->colorspace));
    av_log(avctx, AV_LOG_DEBUG, "color_range: %d (%s)\n", avframe->color_range,
           av_color_range_name(avframe->color_range));
}

typedef struct {
    enum AVPixelFormat     av_fmt;
    topscodecPixelFormat_t tops_fmt;
    const char*            name;
} PixelFormatMap;

static const PixelFormatMap pixels_format_map[] = {
    {AV_PIX_FMT_NV12, TOPSCODEC_PIX_FMT_NV12,
     "NV12"},  // 8bit Semi-planar Y4-U1V1.
    {AV_PIX_FMT_NV21, TOPSCODEC_PIX_FMT_NV21,
     "NV21"},  // 8bit Semi-planar Y4-V1U1.
    {AV_PIX_FMT_YUV420P, TOPSCODEC_PIX_FMT_I420,
     "YUV420"},  // 8bit Planar Y4-U1-V1.
    {AV_PIX_FMT_YUV420P, TOPSCODEC_PIX_FMT_YV12,
     "YV12"},  // 8bit Planar Y4-V1-U1.    fixme
    {AV_PIX_FMT_YUYV422, TOPSCODEC_PIX_FMT_YUYV,
     "YUYV422"},  // 8bit packed Y2U1Y2V1.
    {AV_PIX_FMT_UYVY422, TOPSCODEC_PIX_FMT_UYVY,
     "UYVY422"},  // 8bit packed U1Y2V1Y2.
    {AV_PIX_FMT_YUYV422, TOPSCODEC_PIX_FMT_YVYU,
     "YVYU422"},  // 8bit packed Y2V1Y2U1.   fixme
    {AV_PIX_FMT_UYVY422, TOPSCODEC_PIX_FMT_VYUY,
     "VYUY422"},  // 8bit packed V1Y2U1Y2.   fixme
    {AV_PIX_FMT_P010LE, TOPSCODEC_PIX_FMT_P010,
     "P010LE"},  // 10bit semi-planar Y4-U1V1.ffmpeg: p010le ->topscodec p010
                 // (都是小端存储，都是MSB)
    {AV_PIX_FMT_P010LE_LSB, TOPSCODEC_PIX_FMT_P010LE,
     "P010LE_LSB"},  // 10bit semi-planar Y4-U1V1  ffmpeg:p010le_lsb ->topscodec
                     // p010le (都是小端存储， 都是LSB)
    {AV_PIX_FMT_YUV420P10LE, TOPSCODEC_PIX_FMT_I010,
     "YUV420P10E"},  // 10bit planar Y4-U1-V1.
    {AV_PIX_FMT_YUV444P, TOPSCODEC_PIX_FMT_YUV444,
     "YUV444P"},  // 8bit planar Y4-U4-V4.
    {AV_PIX_FMT_YUV444P10LE, TOPSCODEC_PIX_FMT_YUV444_10BIT,
     "YUV444P10LE"},  // 10bit planar Y4-U4-V4.
    {AV_PIX_FMT_ARGB, TOPSCODEC_PIX_FMT_ARGB, "ARGB"},  // Packed A8R8G8B8.
    {AV_PIX_FMT_BGRA, TOPSCODEC_PIX_FMT_BGRA, "BGRA"},  // Packed B8G8R8A8.
    {AV_PIX_FMT_ABGR, TOPSCODEC_PIX_FMT_ABGR, "ABGR"},  // Packed A8B8G8R8.
    {AV_PIX_FMT_RGBA, TOPSCODEC_PIX_FMT_RGBA, "RGBA"},  // Packed R8G8B8A8.
    {AV_PIX_FMT_RGB565BE, TOPSCODEC_PIX_FMT_RGB565,
     "RGB565BE"},  // R5G6B5, 16 bits per pixel.
    {AV_PIX_FMT_BGR565BE, TOPSCODEC_PIX_FMT_BGR565,
     "BGR565BE"},  // B5G6R5, 16 bits per pixel.
    {AV_PIX_FMT_RGB555BE, TOPSCODEC_PIX_FMT_RGB555,
     "RGB555BE"},  // R5G5B5, 16 bits per pixel.
    {AV_PIX_FMT_BGR555BE, TOPSCODEC_PIX_FMT_BGR555,
     "BGR555BE"},  // B5G5R5, 16 bits per pixel.
    {AV_PIX_FMT_RGB444BE, TOPSCODEC_PIX_FMT_RGB444,
     "RGB444BE"},  // R4G4B4, 16 bits per pixel.
    {AV_PIX_FMT_BGR444BE, TOPSCODEC_PIX_FMT_BGR444,
     "BGR444BE"},  // B4G4R4, 16 bits per pixel.
    {AV_PIX_FMT_RGB24, TOPSCODEC_PIX_FMT_RGB888,
     "RGB24"},  // 8bit packed R8G8B8.
    {AV_PIX_FMT_BGR24, TOPSCODEC_PIX_FMT_BGR888,
     "BGR24"},  // 8bit packed R8G8B8.
    {AV_PIX_FMT_RGB24P, TOPSCODEC_PIX_FMT_RGB3P,
     "RGB24P"},                                            // 8bit planar R-G-B.
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 18, 100)  // n4.x
    {AV_PIX_FMT_X2RGB10BE, TOPSCODEC_PIX_FMT_RGB101010,
     "X2RGB10BE"},  // 10bit packed R10G10B10. fixme
    {AV_PIX_FMT_X2RGB10BE, TOPSCODEC_PIX_FMT_BGR101010,
     "X2RGB10BE"},  // 10bit packed B10G10R10. fixme
    {AV_PIX_FMT_GRAY10LE, TOPSCODEC_PIX_FMT_MONOCHROME_10BIT,
     "GRAY10LE"},  // 10bit gray scale.
#endif
    {AV_PIX_FMT_GRAY8, TOPSCODEC_PIX_FMT_MONOCHROME,
     "GRAY8"},  // 8bit gray scale.
    {AV_PIX_FMT_BGR24P, TOPSCODEC_PIX_FMT_BGR3P,
     "BGR24P"}};  // 8bit planar B-G-R.

enum AVPixelFormat topspixfmt_2_avpixfmt(topscodecPixelFormat_t fmt) {
    const size_t map_size =
        sizeof(pixels_format_map) / sizeof(pixels_format_map[0]);
    for (size_t i = 0; i < map_size; ++i) {
        if (pixels_format_map[i].tops_fmt == fmt) {
            return pixels_format_map[i].av_fmt;
        }
    }
    return AV_PIX_FMT_YUV420P;
}

topscodecPixelFormat_t avpixfmt_2_topspixfmt(enum AVPixelFormat fmt) {
    const size_t map_size =
        sizeof(pixels_format_map) / sizeof(pixels_format_map[0]);
    for (size_t i = 0; i < map_size; ++i) {
        if (pixels_format_map[i].av_fmt == fmt) {
            return pixels_format_map[i].tops_fmt;
        }
    }
    return TOPSCODEC_PIX_FMT_I420;
}

const char* get_avpixfmt_name(enum AVPixelFormat fmt) {
    const size_t map_size =
        sizeof(pixels_format_map) / sizeof(pixels_format_map[0]);
    for (size_t i = 0; i < map_size; ++i) {
        if (pixels_format_map[i].av_fmt == fmt) {
            return pixels_format_map[i].name;
        }
    }
    return NULL;
}
const char* get_topspixfmt_name(topscodecPixelFormat_t fmt) {
    const size_t map_size =
        sizeof(pixels_format_map) / sizeof(pixels_format_map[0]);
    for (size_t i = 0; i < map_size; ++i) {
        if (pixels_format_map[i].tops_fmt == fmt) {
            return pixels_format_map[i].name;
        }
    }
    return NULL;
}

/**
 * Calculate width from stride for a given pixel format
 * @param pix_fmt pixel format
 * @param stride stride value in bytes
 * @param plane plane index (0 for Y/luma, 1 for U/Cb, 2 for V/Cr)
 *              For packed formats, use plane 0
 * @return calculated width, or -1 if invalid
 */
int calculate_width_from_stride(enum AVPixelFormat pix_fmt, int stride,
                                int plane) {
    if (stride <= 0) {
        return -1;
    }

    switch (pix_fmt) {
        // Packed RGB formats: stride = width * 3
        case AV_PIX_FMT_RGB24:
        case AV_PIX_FMT_BGR24:
            if (plane == 0) {
                return stride / 3;
            }
            return -1;

        // Planar RGB formats: each plane stride = width
        case AV_PIX_FMT_RGB24P:
        case AV_PIX_FMT_BGR24P:
            if (plane >= 0 && plane <= 2) {
                return stride;
            }
            return -1;

        // YUV420P: Y plane stride = width, U/V plane stride = width/2
        case AV_PIX_FMT_YUV420P:
            if (plane == 0) {
                // Y plane
                return stride;
            } else if (plane == 1 || plane == 2) {
                // U or V plane
                return stride * 2;
            }
            return -1;

        // NV12/NV21: Y plane stride = width, UV plane stride = width
        case AV_PIX_FMT_NV12:
        case AV_PIX_FMT_NV21:
            if (plane == 0) {
                // Y plane
                return stride;
            } else if (plane == 1) {
                // UV plane
                return stride;
            }
            return -1;

        // YUV444P: all planes stride = width
        case AV_PIX_FMT_YUV444P:
            if (plane >= 0 && plane <= 2) {
                return stride;
            }
            return -1;

        // YUV444P10BE: stride = width * 2 (10bit = 2 bytes)
        case AV_PIX_FMT_YUV444P10LE:
            if (plane >= 0 && plane <= 2) {
                return stride / 2;
            }
            return -1;

        // P010LE/P010LE_LSB: Y plane stride = width * 2, UV plane stride =
        // width *
        // 2
        case AV_PIX_FMT_P010LE:
        case AV_PIX_FMT_P010LE_LSB:
            if (plane == 0) {
                // Y plane
                return stride / 2;
            } else if (plane == 1) {
                // UV plane
                return stride / 2;
            }
            return -1;

        // GRAY8: stride = width
        case AV_PIX_FMT_GRAY8:
            if (plane == 0) {
                return stride;
            }
            return -1;

            // // GRAY10LE: stride = width * 2
            // case AV_PIX_FMT_GRAY10LE:
            //     if (plane == 0) {
            //         return stride / 2;
            //     }
            //     return -1;

            // // TOPSCODEC: cannot determine without additional info
            // case AV_PIX_FMT_TOPSCODEC:
            //     return -1;

        default:
            // For unknown formats, try to use FFmpeg's pixdesc
            {
                const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(pix_fmt);
                if (desc && plane >= 0 && plane < desc->nb_components) {
                    int comp = desc->comp[plane].step;
                    if (comp > 0) {
                        return stride / comp;
                    }
                }
                return -1;
            }
    }
}

enum AVPictureType tops_2_av_pic_type(topscodecPicType_t type) {
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

int tops_is_key_frame(topscodecPicType_t type) {
    if (type == TOPSCODEC_PIC_TYPE_IDR || type == TOPSCODEC_PIC_TYPE_I) return 1;
    return 0;
}

static void print_version_impl(void) {
    av_log(NULL, AV_LOG_INFO, "Enflame FFmpeg-GCU Version: %s\n",
           FFMPEG_GCU_VERSION_STRING);
}

void ff_ffmpeg_gcu_print_version(void) {
    static AVOnce once = AV_ONCE_INIT;
    ff_thread_once(&once, print_version_impl);
}
