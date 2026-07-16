/*
 * TOPSCODEC DEC Video Acceleration API decode sample
 * Copyright (C) [2019] by Enflame, Inc. All rights reserved
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * Enflame TOPSCODEC-HW-Accelerated  encoding example.
 *
 * @example hw_encode_tops.c
 */

#include <libavcodec/avcodec.h>
#include <libavcodec/ff_topscodec_buffers.h>
#include <libavcodec/ff_topscodec_enc.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/mem.h>
#include <libavutil/pixfmt.h>
#include <libavutil/time.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libavutil/pixdesc.h"

#define CHECK_RET(ret, msg)                                                    \
    if (ret < 0) {                                                             \
        fprintf(stderr, "Error: %s, line: %d, ret: %d\n", msg, __LINE__, ret); \
        exit(1);                                                               \
    }

typedef void (*ffmpeg_log_callback)(void* ptr, int level, const char* fmt,
                                    va_list vl);

#define LOG_BUF_PREFIX_SIZE 512
#define LOG_BUF_SIZE 1024
static char            logBufPrefix[LOG_BUF_PREFIX_SIZE] = {0};
static char            logBuffer[LOG_BUF_SIZE]           = {0};
static FILE*           fp_output_file                    = NULL;
static FILE*           fp_input_file                     = NULL;
static AVCodecContext* g_enc_ctx                         = NULL;
static int64_t         g_packet_count                    = 0;
static int64_t         g_frame_count                     = 0;
static int64_t         g_guess_file_frame_count          = 0;
static int             g_flush_flag         = 0;  // 0: not flush, 1: flush
static int64_t         g_input_file_size    = 0;
static int             g_save_enc_file_flag = 1;  // 0: not save, 1: save

static uint64_t g_start_time          = 0;
static uint64_t g_first_frame_time    = 0;
static uint64_t g_end_time            = 0;
static float    g_avg_fps             = 0;
static uint64_t g_first_frame_latency = 0;

static pthread_mutex_t cb_av_log_lock;

typedef struct {
    const char* codec_name;
    const char* input_fmt;
    const char* input_file;
    const char* output_file;

    // ffmpeg options
    int width;   // video size
    int height;  // video size
    int level;
    int profile;
    int gop;
    int b_frame_num;
    int bitrate;
    int preset;
    int quality;
    int tier;

    // topscodec options
    int card_id;
    int dev_id;
    int sf;
    int qp_i;
    int qp_p;
    int qp_b;
    int init_qp_i;
    int init_qp_p;
    int init_qp_b;
    int enc_fps;
    int enc_gop_type;
    int enc_rate_control;
    int conv_mode;
    int out_port_num;
    int in_port_num;
    // rotation options
    int enable_rotation;
    int rotation;
    // crop options
    int enable_crop;
    int crop_top;
    int crop_bottom;
    int crop_left;
    int crop_right;
    // flip options
    int enable_flip;
    int flip_model;

    // hdr options
    int hdr_mode;
    int hdr_flags;
    int hdr_matrix;
    int hdr_matrix_range;
    int hdr_primaries;
    int hdr_transfer;
    int hdr_display_r_x;
    int hdr_display_r_y;
    int hdr_display_g_x;
    int hdr_display_g_y;
    int hdr_display_b_x;
    int hdr_display_b_y;
    int hdr_display_w_x;
    int hdr_display_w_y;
    int hdr_display_lum_min;
    int hdr_display_lum_max;
    int hdr_content_lum_max;
    int hdr_content_lum_avg;
    int hdr_aspect_ratio_idc;
    int hdr_sar_width;
    int hdr_sar_height;
    int hdr_num_units_in_tick;
    int hdr_time_scale;

    int color_range;
    int color_primaries;
    int color_trc;
    int colorspace;

    int stride_align;
    int balance;
} EncoderParams;

static EncoderParams g_params = {
    .input_file            = NULL,
    .output_file           = NULL,
    .codec_name            = "h264_topscodec_enc",
    .input_fmt             = "yuv420p",
    .width                 = 1920,
    .height                = 1080,
    .level                 = 51,
    .profile               = 0,
    .gop                   = 30,
    .b_frame_num           = 0,
    .bitrate               = 10000000,
    .preset                = 0,
    .quality               = 0,
    .tier                  = 0,
    .card_id               = 0,
    .dev_id                = 0,
    .sf                    = 0,
    .qp_i                  = 23,
    .qp_p                  = 25,
    .qp_b                  = 27,
    .init_qp_i             = -1,
    .init_qp_p             = -1,
    .init_qp_b             = -1,
    .enc_fps               = 30,
    .enc_gop_type          = 1,
    .enc_rate_control      = 1,
    .conv_mode             = 0,
    .out_port_num          = 8,
    .in_port_num           = 8,
    .enable_rotation       = 0,
    .rotation              = 0,
    .enable_crop           = 0,
    .crop_top              = 0,
    .crop_bottom           = 0,
    .crop_left             = 0,
    .crop_right            = 0,
    .enable_flip           = 0,
    .flip_model            = 0,
    .hdr_mode              = 0,
    .hdr_flags             = 0,
    .hdr_matrix_range      = 0,
    .hdr_matrix            = 0,
    .hdr_primaries         = 0,
    .hdr_transfer          = 0,
    .hdr_display_r_x       = 0,
    .hdr_display_r_y       = 0,
    .hdr_display_g_x       = 0,
    .hdr_display_g_y       = 0,
    .hdr_display_b_x       = 0,
    .hdr_display_b_y       = 0,
    .hdr_display_w_x       = 0,
    .hdr_display_w_y       = 0,
    .hdr_display_lum_min   = 0,
    .hdr_display_lum_max   = 0,
    .hdr_content_lum_max   = 0,
    .hdr_content_lum_avg   = 0,
    .hdr_aspect_ratio_idc  = 0,
    .hdr_sar_width         = 0,
    .hdr_sar_height        = 0,
    .hdr_num_units_in_tick = 0,
    .hdr_time_scale        = 0,
    .color_range           = 0,
    .color_primaries       = 0,
    .color_trc             = 0,
    .colorspace            = 0,
    .stride_align          = 1,
    .balance               = 0,
};

static void print_encoder_params(EncoderParams* encoder_params) {
    fprintf(stderr, "Configuration:\n");
    fprintf(stderr, "  Input file: %s\n", encoder_params->input_file);
    fprintf(stderr, "  Output file: %s\n", encoder_params->output_file);
    fprintf(stderr, "  Codec: %s\n", encoder_params->codec_name);
    fprintf(stderr, "  Input format: %s\n", encoder_params->input_fmt);
    fprintf(stderr, "  Resolution: %dx%d\n", encoder_params->width,
            encoder_params->height);
    fprintf(stderr, "  Level: %d\n", encoder_params->level);
    fprintf(stderr, "  Profile: %d\n", encoder_params->profile);
    fprintf(stderr, "  GOP: %d\n", encoder_params->gop);
    fprintf(stderr, "  B-frame num: %d\n", encoder_params->b_frame_num);
    fprintf(stderr, "  Bitrate: %d\n", encoder_params->bitrate);
    fprintf(stderr, "  Preset: %d\n", encoder_params->preset);
    fprintf(stderr, "  Quality: %d\n", encoder_params->quality);
    fprintf(stderr, "  Tier: %d\n", encoder_params->tier);
    fprintf(stderr, "  Card ID: %d\n", encoder_params->card_id);
    fprintf(stderr, "  Device ID: %d\n", encoder_params->dev_id);
    fprintf(stderr, "  SF: %d\n", encoder_params->sf);
    fprintf(stderr, "  QP I: %d\n", encoder_params->qp_i);
    fprintf(stderr, "  QP P: %d\n", encoder_params->qp_p);
    fprintf(stderr, "  QP B: %d\n", encoder_params->qp_b);
    fprintf(stderr, "  Init QP I: %d\n", encoder_params->init_qp_i);
    fprintf(stderr, "  Init QP P: %d\n", encoder_params->init_qp_p);
    fprintf(stderr, "  Init QP B: %d\n", encoder_params->init_qp_b);
    fprintf(stderr, "  Enc FPS: %d\n", encoder_params->enc_fps);
    fprintf(stderr, "  Enc GOP type: %d\n", encoder_params->enc_gop_type);
    fprintf(stderr, "  Enc rate control: %d\n",
            encoder_params->enc_rate_control);
    fprintf(stderr, "  Conv mode: %d\n", encoder_params->conv_mode);
    fprintf(stderr, "  Out port num: %d\n", encoder_params->out_port_num);
    fprintf(stderr, "  In port num: %d\n", encoder_params->in_port_num);
    fprintf(stderr, "  Enable rotation: %d\n", encoder_params->enable_rotation);
    fprintf(stderr, "  Rotation: %d\n", encoder_params->rotation);
    fprintf(stderr, "  Enable crop: %d\n", encoder_params->enable_crop);
    fprintf(stderr, "  Crop top: %d\n", encoder_params->crop_top);
    fprintf(stderr, "  Crop bottom: %d\n", encoder_params->crop_bottom);
    fprintf(stderr, "  Crop left: %d\n", encoder_params->crop_left);
    fprintf(stderr, "  Crop right: %d\n", encoder_params->crop_right);
    fprintf(stderr, "  Enable flip: %d\n", encoder_params->enable_flip);
    fprintf(stderr, "  Flip model: %d\n", encoder_params->flip_model);
    fprintf(stderr, "  HDR mode: %d\n", encoder_params->hdr_mode);
    fprintf(stderr, "  HDR flags: %d\n", encoder_params->hdr_flags);
    fprintf(stderr, "  HDR matrix range: %d\n",
            encoder_params->hdr_matrix_range);
    fprintf(stderr, "  HDR matrix: %d\n", encoder_params->hdr_matrix);
    fprintf(stderr, "  HDR primaries: %d\n", encoder_params->hdr_primaries);
    fprintf(stderr, "  HDR transfer: %d\n", encoder_params->hdr_transfer);
    fprintf(stderr, "  HDR display R X: %d\n", encoder_params->hdr_display_r_x);
    fprintf(stderr, "  HDR display R Y: %d\n", encoder_params->hdr_display_r_y);
    fprintf(stderr, "  HDR display G X: %d\n", encoder_params->hdr_display_g_x);
    fprintf(stderr, "  HDR display G Y: %d\n", encoder_params->hdr_display_g_y);
    fprintf(stderr, "  HDR display B X: %d\n", encoder_params->hdr_display_b_x);
    fprintf(stderr, "  HDR display B Y: %d\n", encoder_params->hdr_display_b_y);
    fprintf(stderr, "  HDR display W X: %d\n", encoder_params->hdr_display_w_x);
    fprintf(stderr, "  HDR display W Y: %d\n", encoder_params->hdr_display_w_y);
    fprintf(stderr, "  HDR display lum min: %d\n",
            encoder_params->hdr_display_lum_min);
    fprintf(stderr, "  HDR display lum max: %d\n",
            encoder_params->hdr_display_lum_max);
    fprintf(stderr, "  HDR content lum max: %d\n",
            encoder_params->hdr_content_lum_max);
    fprintf(stderr, "  HDR content lum avg: %d\n",
            encoder_params->hdr_content_lum_avg);
    fprintf(stderr, "  HDR aspect ratio IDC: %d\n",
            encoder_params->hdr_aspect_ratio_idc);
    fprintf(stderr, "  HDR SAR width: %d\n", encoder_params->hdr_sar_width);
    fprintf(stderr, "  HDR SAR height: %d\n", encoder_params->hdr_sar_height);
    fprintf(stderr, "  HDR num units in tick: %d\n",
            encoder_params->hdr_num_units_in_tick);
    fprintf(stderr, "  HDR time scale: %d\n", encoder_params->hdr_time_scale);
    fprintf(stderr, "  Color range: %s\n",
            av_color_range_name(encoder_params->color_range));
    fprintf(stderr, "  Color primaries: %s\n",
            av_color_primaries_name(encoder_params->color_primaries));
    fprintf(stderr, "  Color trc: %s\n",
            av_color_transfer_name(encoder_params->color_trc));
    fprintf(stderr, "  Colorspace: %s\n",
            av_color_space_name(encoder_params->colorspace));
    fprintf(stderr, "  Stride align: %d\n", encoder_params->stride_align);
}

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 18, 100)
static enum AVPixelFormat get_hw_format(AVCodecContext*           ctx,
                                        const enum AVPixelFormat* pix_fmts) {
    return AV_PIX_FMT_TOPSCODEC;
}
#else
static enum AVPixelFormat get_hw_format(AVCodecContext*           ctx,
                                        const enum AVPixelFormat* pix_fmts) {
    const enum AVPixelFormat* p;

    for (p = pix_fmts; *p != -1; p++) {
        if (*p == AV_PIX_FMT_TOPSCODEC) return *p;
    }

    av_log(ctx, AV_LOG_ERROR, "Failed to get HW surface format.\n");
    return AV_PIX_FMT_NONE;
}
#endif

static void log_callback_null(void* ptr, int level, const char* fmt,
                              va_list vl) {
    pthread_mutex_lock(&cb_av_log_lock);
    snprintf(logBufPrefix, LOG_BUF_PREFIX_SIZE, "%s", fmt);
    vsnprintf(logBuffer, LOG_BUF_SIZE, logBufPrefix, vl);
    printf("%s", logBuffer);
    pthread_mutex_unlock(&cb_av_log_lock);
}

static void print_usage(const char* prog_name) {
    fprintf(stderr, "Usage: %s [OPTIONS] -i <input_file> -o <output_file>\n",
            prog_name);
    fprintf(stderr, "\nOptions:\n");
    // save enc file
    fprintf(stderr, "   -s <save_enc_file_flag> set save enc file flag\n");
    // flush
    fprintf(stderr, "   -flush <flush_flag> set flush flag\n");
    // balance
    fprintf(stderr, "   -balance <balance> set balance flag\n");
    // input
    fprintf(stderr, "  -i <input_file>     set input file\n");
    // output
    fprintf(stderr, "  -o <output_file>    set output file\n");
    // codec name
    fprintf(stderr, "  -c <codec_name>     set codec name\n");
    fprintf(stderr, "  -pixel_format <input_fmt>      set input format\n");

    // ffmpeg options
    fprintf(stderr,
            "  -video_size <WIDTHxHEIGHT>   set width and height (e.g., "
            "1920x1080)\n");
    fprintf(stderr, "  -level <level>      set level\n");
    fprintf(stderr, "  -profile <profile>  set profile\n");
    fprintf(stderr, "  -g <gop>          set gop\n");
    fprintf(stderr, "  -bf <b_frame_num> set b frame num\n");
    fprintf(stderr, "  -b <bitrate>  set bitrate\n");
    fprintf(stderr, "  -preset <preset>    set preset\n");
    fprintf(stderr, "  -quality <quality>  set quality\n");
    fprintf(stderr, "  -tier <tier>        set tier\n");

    // topscodec options
    fprintf(stderr, "  -card_id <card_id>     set card id\n");
    fprintf(stderr, "  -device_id <device_id>       set device id\n");
    fprintf(stderr, "   -sf <sf>            set sf\n");
    fprintf(stderr, "   -qp_i <qp_i>        set qp i\n");
    fprintf(stderr, "   -qp_p <qp_p>        set qp p\n");
    fprintf(stderr, "   -qp_b <qp_b>        set qp b\n");
    fprintf(stderr, "   -init_qp_i <init_qp_i>        set init qp i\n");
    fprintf(stderr, "   -init_qp_p <init_qp_p>        set init qp p\n");
    fprintf(stderr, "   -init_qp_b <init_qp_b>        set init qp b\n");
    fprintf(stderr, "   -enc_fps <enc_fps>  set enc fps\n");
    fprintf(stderr, "   -enc_gop_type <enc_gop_type> set enc gop type\n");
    fprintf(stderr,
            "   -enc_rate_control <enc_rate_control> set enc rate control\n");
    fprintf(stderr, "   -conv_mode <conv_mode> set conv mode\n");
    fprintf(stderr, "   -out_port_num <out_port_num> set out port num\n");
    fprintf(stderr, "   -in_port_num <in_port_num> set in port num\n");
    fprintf(stderr,
            "   -enable_rotation <enable_rotation> set enable rotation\n");
    fprintf(stderr, "   -rotation <rotation>    set rotation\n");
    fprintf(stderr, "   -enable_crop <enable_crop> set enable crop\n");
    fprintf(stderr,
            "   -crop_size <WIDTHxHEIGHT> set crop size (e.g., 1280x720)\n");
    fprintf(stderr, "   -enable_flip <enable_flip> set enable flip\n");
    fprintf(stderr, "   -flip_model <flip_model> set flip model\n");

    // hdr options
    // hdr options
    fprintf(stderr, "   -hdr_mode <hdr_mode> set hdr mode\n");
    fprintf(stderr, "0-none, 1-hdr10, 2-hdr10+, 3-dolbyvision\n");
    fprintf(stderr, "   -hdr_flags <hdr_flags> set hdr flags\n");
    fprintf(stderr,
            "0-disable, 1-MASTERING_DISPLAY_DATA_VALID, "
            "2-CONTENT_LIGHT_DATA_VALID, 3-DISPLAY_CONTENT_BOTH_VALID\n");
    fprintf(stderr,
            "   -hdr_matrix_range <hdr_matrix_range> set hdr matrix range\n");
    fprintf(stderr, "   -hdr_matrix <hdr_matrix> set hdr matrix\n");
    fprintf(stderr, "   -hdr_primaries <hdr_primaries> set hdr primaries\n");
    fprintf(stderr, "   -hdr_transfer <hdr_transfer> set hdr transfer\n");
    fprintf(stderr,
            "   -hdr_display_r_x <hdr_display_r_x> set hdr display r x\n");
    fprintf(stderr,
            "   -hdr_display_r_y <hdr_display_r_y> set hdr display r y\n");
    fprintf(stderr,
            "   -hdr_display_g_x <hdr_display_g_x> set hdr display g x\n");
    fprintf(stderr,
            "   -hdr_display_g_y <hdr_display_g_y> set hdr display g y\n");
    fprintf(stderr,
            "   -hdr_display_b_x <hdr_display_b_x> set hdr display b x\n");
    fprintf(stderr,
            "   -hdr_display_b_y <hdr_display_b_y> set hdr display b y\n");
    fprintf(stderr,
            "   -hdr_display_w_x <hdr_display_w_x> set hdr display w x\n");
    fprintf(stderr,
            "   -hdr_display_w_y <hdr_display_w_y> set hdr display w y\n");
    fprintf(stderr,
            "   -hdr_display_lum_min <hdr_display_lum_min> set hdr display lum "
            "min\n");
    fprintf(stderr,
            "   -hdr_display_lum_max <hdr_display_lum_max> set hdr display lum "
            "max\n");
    fprintf(stderr,
            "   -hdr_content_lum_max <hdr_content_lum_max> set hdr content lum "
            "max\n");
    fprintf(stderr,
            "   -hdr_content_lum_avg <hdr_content_lum_avg> set hdr content lum "
            "avg\n");
    fprintf(stderr,
            "   -hdr_aspect_ratio_idc <hdr_aspect_ratio_idc> set hdr aspect "
            "ratio idc\n");
    fprintf(stderr, "   -hdr_sar_width <hdr_sar_width> set hdr sar width\n");
    fprintf(stderr, "   -hdr_sar_height <hdr_sar_height> set hdr sar height\n");
    fprintf(stderr,
            "   -hdr_num_units_in_tick <hdr_num_units_in_tick> set hdr num "
            "units in tick\n");
    fprintf(stderr, "   -hdr_time_scale <hdr_time_scale> set hdr time scale\n");

    // color space
    fprintf(stderr, "   -color_range <color_range> set color range\n");
    fprintf(stderr,
            "   -color_primaries <color_primaries> set color primaries\n");
    fprintf(stderr, "   -color_trc <color_trc> set color trc\n");
    fprintf(stderr, "   -colorspace <colorspace> set colorspace\n");

    fprintf(stderr, "   -stride_align <stride_align> set stride align\n");

    fprintf(stderr, "\nSupported input formats:\n");
    fprintf(stderr, "8bit: yuv420p, yuv444p,\n");
    fprintf(stderr, "10bit: yuv420p10le, p010le_lsb, p010le ,yuv444p10be\n");
    fprintf(stderr, "\nSupported codecs:\n");
    fprintf(stderr,
            "  h264_topscodec_enc, hevc_topscodec_enc, mjpeg_topscodec_enc,\n");
    fprintf(stderr,
            "supported mtx_range:\n"
            "HDR: Set CSC matrix range when HDR enabled.\n"
            "\t\t 0 - unspecified \n"
            "\t\t 1 - full range \n"
            "\t\t 2 - limited range \n");
    fprintf(stderr,
            "supported color space:\n"
            "HDR: Set CSC color space when HDR enabled.\n"
            "\t\t 0 - 601 \n"
            "\t\t 1 - 601-ER\n"
            "\t\t 2 - 709\n"
            "\t\t 3 - 709-ER\n"
            "\t\t 4 - 2020\n"
            "\t\t 5 - 2020-ER\n");
    fprintf(stderr,
            "supported primaries:\n"
            "HDR: Set CSC primaries range when HDR enabled.\n"
            "\t\t 0 - unspecified \n"
            "\t\t 1 - BT709\n"
            "\t\t 2 - BT470M\n"
            "\t\t 3 - BT601_625\n"
            "\t\t 4 - BT601-525\n"
            "\t\t 5 - Generic Film\n"
            "\t\t 6 - BT2020\n");

    fprintf(stderr,
            "supported transfer:\n"
            "HDR: Set transfer characteristics when HDR enabled.\n"
            "\t\t 0  - unspecified\n"
            "\t\t 1  - Linear\n"
            "\t\t 2  - sRGB\n"
            "\t\t 3  - SMPTE170M\n"
            "\t\t 4  - Gamma 2.2\n"
            "\t\t 5  - Gamma 2.8\n"
            "\t\t 6  - SMPTE ST 2084\n"
            "\t\t 7  - ARIB STD-B67 hybrid-log-gamma\n"
            "\t\t 8  - SMPTE 240M\n"
            "\t\t 9  - IEC 61966-2-4\n"
            "\t\t 10 - Rec.ITU-R BT.1361 extended gamut.\n"
            "\t\t 11 - SMPTE ST 428-1\n");
    fprintf(stderr,
            "supported matrix:\n"
            "HDR: Select CSC matrix standard.\n"
            "\t\t 0 - unspecified\n"
            "\t\t 1 - BT.709\n"
            "\t\t 2 - BT470M\n"
            "\t\t 3 - BT.601_625\n"
            "\t\t 4 - SMPTE240M\n"
            "\t\t 5 - BT.2020 non-const luma\n"
            "\t\t 6 - BT.2020 constant luma\n");

    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  Basic usage:\n");
    fprintf(stderr,
            " DEBUG=1   %s -video_size 1920x1080 -pixel_format yuv420p -c "
            "h264_topscodec_enc "
            "-card_id 0 -device_id "
            "0 -i input.yuv -o output.h264\n",
            prog_name);
    fprintf(stderr, "  With encoder parameters:\n");
    fprintf(stderr,
            " DEBUG=1   %s -video_size 1920x1080 -pixel_format yuv420p -c "
            "h264_topscodec_enc "
            "-qp_i 20 "
            "-qp_p 22 -qp_b 24 -fps 25 -rc 2 -i input.yuv -o output.h264\n",
            prog_name);
    fprintf(stderr, "  With rotation and crop:\n");
    fprintf(stderr,
            " DEBUG=1   %s -video_size 1920x1080 -pixel_format yuv420p -c "
            "h264_topscodec_enc "
            "-rotation 90 "
            "-enable_crop 1  -crop 10,10,10,10 -i input.yuv -o output.h264\n",
            prog_name);
    fprintf(stderr, "  With flip:\n");
    fprintf(stderr,
            " DEBUG=1   %s -video_size 1920x1080 -pixel_format yuv420p -c "
            "h264_topscodec_enc "
            "-enable_flip 1 -flip_model 1 -i input.yuv -o output.h264\n",
            prog_name);
}

static void parse_params(int argc, char** argv) {
    int i = 0;
    /* Parse command line arguments */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "-c") == 0) {
            g_params.codec_name = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "-pixel_format") == 0) {
            g_params.input_fmt = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "-s") == 0) {
            g_save_enc_file_flag = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-flush") == 0) {
            g_flush_flag = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-balance") == 0) {
            g_params.balance = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-i") == 0) {
            g_params.input_file = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "-o") == 0) {
            g_params.output_file = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "-video_size") == 0) {
            // Parse size in format "WIDTHxHEIGHT"
            char* size_str = argv[i + 1];
            char* x_pos    = strchr(size_str, 'x');
            if (x_pos == NULL) {
                fprintf(stderr,
                        "Error: Invalid size format. Use WIDTHxHEIGHT (e.g., "
                        "1920x1080)\n");
                exit(1);
            }
            *x_pos          = '\0';  // Split the string at 'x'
            g_params.width  = atoi(size_str);
            g_params.height = atoi(x_pos + 1);
            if (g_params.width <= 0 || g_params.height <= 0) {
                fprintf(stderr, "Error: Invalid width or height values\n");
                exit(1);
            }
            i++;
        } else if (strcmp(argv[i], "-level") == 0) {
            g_params.level = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-profile") == 0) {
            g_params.profile = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-g") == 0) {
            g_params.gop = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-bf") == 0) {
            g_params.b_frame_num = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-b") == 0) {
            g_params.bitrate = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-preset") == 0) {
            g_params.preset = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-quality") == 0) {
            g_params.quality = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-tier") == 0) {
            g_params.tier = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-card_id") == 0) {
            g_params.card_id = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-device_id") == 0) {
            g_params.dev_id = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-sf") == 0) {
            g_params.sf = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-qp_i") == 0) {
            g_params.qp_i = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-qp_p") == 0) {
            g_params.qp_p = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-qp_b") == 0) {
            g_params.qp_b = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-init_qp_i") == 0) {
            g_params.init_qp_i = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-init_qp_p") == 0) {
            g_params.init_qp_p = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-init_qp_b") == 0) {
            g_params.init_qp_b = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-enc_fps") == 0) {
            g_params.enc_fps = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-enc_gop_type") == 0) {
            g_params.enc_gop_type = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-enc_rate_control") == 0) {
            g_params.enc_rate_control = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-conv_mode") == 0) {
            g_params.conv_mode = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-out_port_num") == 0) {
            g_params.out_port_num = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-in_port_num") == 0) {
            g_params.in_port_num = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-enable_rotation") == 0) {
            g_params.enable_rotation = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-rotation") == 0) {
            g_params.rotation = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-enable_crop") == 0) {
            g_params.enable_crop = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-crop") == 0) {
            // Parse crop in format "top,bottom,left,right"
            char* crop_str = argv[i + 1];
            char* values[4];
            int   count = 0;
            char* saveptr;
            char* token = strtok_r(crop_str, ",", &saveptr);

            while (token != NULL && count < 4) {
                values[count] = token;
                count++;
                token = strtok_r(NULL, ",", &saveptr);
            }

            if (count != 4) {
                fprintf(stderr,
                        "Error: Invalid crop format. Use top,bottom,left,right "
                        "(e.g., 10,10,10,10)\n");
                exit(1);
            }

            g_params.crop_top    = atoi(values[0]);
            g_params.crop_bottom = atoi(values[1]);
            g_params.crop_left   = atoi(values[2]);
            g_params.crop_right  = atoi(values[3]);

            if (g_params.crop_top < 0 || g_params.crop_bottom < 0 ||
                g_params.crop_left < 0 || g_params.crop_right < 0) {
                fprintf(stderr,
                        "Error: Invalid crop values (must be non-negative)\n");
                exit(1);
            }
            i++;
        } else if (strcmp(argv[i], "-enable_flip") == 0) {
            g_params.enable_flip = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-flip_model") == 0) {
            g_params.flip_model = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-hdr_mode") == 0) {
            g_params.hdr_mode = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-hdr_flags") == 0) {
            g_params.hdr_flags = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-hdr_matrix_range") == 0) {
            g_params.hdr_matrix_range = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-hdr_matrix") == 0) {
            g_params.hdr_matrix = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-hdr_primaries") == 0) {
            g_params.hdr_primaries = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-hdr_transfer") == 0) {
            g_params.hdr_transfer = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-hdr_display_r_x") == 0) {
            g_params.hdr_display_r_x = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-hdr_display_r_y") == 0) {
            g_params.hdr_display_r_y = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-hdr_display_g_x") == 0) {
            g_params.hdr_display_g_x = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-hdr_display_g_y") == 0) {
            g_params.hdr_display_g_y = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-hdr_display_b_x") == 0) {
            g_params.hdr_display_b_x = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-hdr_display_b_y") == 0) {
            g_params.hdr_display_b_y = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-hdr_display_w_x") == 0) {
            g_params.hdr_display_w_x = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-hdr_display_w_y") == 0) {
            g_params.hdr_display_w_y = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-hdr_display_lum_min") == 0) {
            g_params.hdr_display_lum_min = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-hdr_display_lum_max") == 0) {
            g_params.hdr_display_lum_max = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-hdr_content_lum_max") == 0) {
            g_params.hdr_content_lum_max = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-hdr_content_lum_avg") == 0) {
            g_params.hdr_content_lum_avg = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-hdr_aspect_ratio_idc") == 0) {
            g_params.hdr_aspect_ratio_idc = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-hdr_sar_width") == 0) {
            g_params.hdr_sar_width = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-hdr_sar_height") == 0) {
            g_params.hdr_sar_height = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-hdr_num_units_in_tick") == 0) {
            g_params.hdr_num_units_in_tick = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-hdr_time_scale") == 0) {
            g_params.hdr_time_scale = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-color_range") == 0) {
            g_params.color_range = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-color_primaries") == 0) {
            g_params.color_primaries = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-color_trc") == 0) {
            g_params.color_trc = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-colorspace") == 0) {
            g_params.colorspace = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-stride_align") == 0) {
            g_params.stride_align = atoi(argv[i + 1]);
            i++;
        } else {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            exit(1);
        }
    }
}

static int build_encoder_options(AVDictionary** enc_opts) {
    int  ret          = 0;
    char opt_str[128] = {0};
    // ffmpeg options
    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.level);
    ret = av_dict_set(enc_opts, "level", opt_str, 0);
    CHECK_RET(ret, "av_dict_set level");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.profile);
    ret = av_dict_set(enc_opts, "profile", opt_str, 0);
    CHECK_RET(ret, "av_dict_set profile");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.gop);
    ret = av_dict_set(enc_opts, "g", opt_str, 0);
    CHECK_RET(ret, "av_dict_set gop");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.b_frame_num);
    ret = av_dict_set(enc_opts, "bf", opt_str, 0);
    CHECK_RET(ret, "av_dict_set b_frame_num");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.bitrate);
    ret = av_dict_set(enc_opts, "b", opt_str, 0);
    CHECK_RET(ret, "av_dict_set bitrate");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.preset);
    ret = av_dict_set(enc_opts, "preset", opt_str, 0);
    CHECK_RET(ret, "av_dict_set preset");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.color_range);
    ret = av_dict_set(enc_opts, "color_range", opt_str, 0);
    CHECK_RET(ret, "av_dict_set color_range");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.color_primaries);
    ret = av_dict_set(enc_opts, "color_primaries", opt_str, 0);
    CHECK_RET(ret, "av_dict_set color_primaries");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.color_trc);
    ret = av_dict_set(enc_opts, "color_trc", opt_str, 0);
    CHECK_RET(ret, "av_dict_set color_trc");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.colorspace);
    ret = av_dict_set(enc_opts, "colorspace", opt_str, 0);
    CHECK_RET(ret, "av_dict_set colorspace");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.stride_align);
    ret = av_dict_set(enc_opts, "stride_align", opt_str, 0);
    CHECK_RET(ret, "av_dict_set stride_align");

    //-------------------topscodec options---------------------------//
    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.card_id);
    ret = av_dict_set(enc_opts, "card_id", opt_str, 0);
    CHECK_RET(ret, "av_dict_set card_id");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.dev_id);
    ret = av_dict_set(enc_opts, "device_id", opt_str, 0);
    CHECK_RET(ret, "av_dict_set device_id");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.balance);
    ret = av_dict_set(enc_opts, "balance", opt_str, 0);
    CHECK_RET(ret, "av_dict_set balance");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.sf);
    ret = av_dict_set(enc_opts, "sf", opt_str, 0);
    CHECK_RET(ret, "av_dict_set sf");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.qp_i);
    ret = av_dict_set(enc_opts, "qp_i", opt_str, 0);
    CHECK_RET(ret, "av_dict_set qp_i");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.qp_p);
    ret = av_dict_set(enc_opts, "qp_p", opt_str, 0);
    CHECK_RET(ret, "av_dict_set qp_p");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.qp_b);
    ret = av_dict_set(enc_opts, "qp_b", opt_str, 0);
    CHECK_RET(ret, "av_dict_set qp_b");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.init_qp_i);
    ret = av_dict_set(enc_opts, "init_qp_i", opt_str, 0);
    CHECK_RET(ret, "av_dict_set init_qp_i");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.init_qp_p);
    ret = av_dict_set(enc_opts, "init_qp_p", opt_str, 0);
    CHECK_RET(ret, "av_dict_set init_qp_p");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.init_qp_b);
    ret = av_dict_set(enc_opts, "init_qp_b", opt_str, 0);
    CHECK_RET(ret, "av_dict_set init_qp_b");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.enc_fps);
    ret = av_dict_set(enc_opts, "enc_fps", opt_str, 0);
    CHECK_RET(ret, "av_dict_set enc_fps");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.enc_gop_type);
    ret = av_dict_set(enc_opts, "enc_gop_type", opt_str, 0);
    CHECK_RET(ret, "av_dict_set enc_gop_type");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.enc_rate_control);
    ret = av_dict_set(enc_opts, "enc_rate_control", opt_str, 0);
    CHECK_RET(ret, "av_dict_set enc_rate_control");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.conv_mode);
    ret = av_dict_set(enc_opts, "conv_mode", opt_str, 0);
    CHECK_RET(ret, "av_dict_set conv_mode");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.out_port_num);
    ret = av_dict_set(enc_opts, "out_port_num", opt_str, 0);
    CHECK_RET(ret, "av_dict_set out_port_num");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.in_port_num);
    ret = av_dict_set(enc_opts, "in_port_num", opt_str, 0);
    CHECK_RET(ret, "av_dict_set in_port_num");

    // Set rotation parameters
    if (g_params.enable_rotation) {
        memset(opt_str, 0, sizeof(opt_str));
        snprintf(opt_str, sizeof(opt_str), "%d", g_params.enable_rotation);
        ret = av_dict_set(enc_opts, "enable_rotation", opt_str, 0);
        CHECK_RET(ret, "av_dict_set enable_rotation");

        memset(opt_str, 0, sizeof(opt_str));
        snprintf(opt_str, sizeof(opt_str), "%d", g_params.rotation);
        ret = av_dict_set(enc_opts, "rotation", opt_str, 0);
        CHECK_RET(ret, "av_dict_set rotation");
    }

    // Set crop parameters
    if (g_params.enable_crop) {
        memset(opt_str, 0, sizeof(opt_str));
        snprintf(opt_str, sizeof(opt_str), "%d", g_params.enable_crop);
        ret = av_dict_set(enc_opts, "enable_crop", opt_str, 0);
        CHECK_RET(ret, "av_dict_set enable_crop");

        memset(opt_str, 0, sizeof(opt_str));
        snprintf(opt_str, sizeof(opt_str), "%d", g_params.crop_top);
        ret = av_dict_set(enc_opts, "crop_top", opt_str, 0);
        CHECK_RET(ret, "av_dict_set crop_top");

        memset(opt_str, 0, sizeof(opt_str));
        snprintf(opt_str, sizeof(opt_str), "%d", g_params.crop_bottom);
        ret = av_dict_set(enc_opts, "crop_bottom", opt_str, 0);
        CHECK_RET(ret, "av_dict_set crop_bottom");

        memset(opt_str, 0, sizeof(opt_str));
        snprintf(opt_str, sizeof(opt_str), "%d", g_params.crop_left);
        ret = av_dict_set(enc_opts, "crop_left", opt_str, 0);
        CHECK_RET(ret, "av_dict_set crop_left");

        memset(opt_str, 0, sizeof(opt_str));
        snprintf(opt_str, sizeof(opt_str), "%d", g_params.crop_right);
        ret = av_dict_set(enc_opts, "crop_right", opt_str, 0);
        CHECK_RET(ret, "av_dict_set crop_right");
    }

    // Set flip parameters
    if (g_params.enable_flip) {
        memset(opt_str, 0, sizeof(opt_str));
        snprintf(opt_str, sizeof(opt_str), "%d", g_params.enable_flip);
        ret = av_dict_set(enc_opts, "enable_flip", opt_str, 0);
        CHECK_RET(ret, "av_dict_set enable_flip");

        memset(opt_str, 0, sizeof(opt_str));
        snprintf(opt_str, sizeof(opt_str), "%d", g_params.flip_model);
        ret = av_dict_set(enc_opts, "flip_model", opt_str, 0);
        CHECK_RET(ret, "av_dict_set flip_model");
    }

    //-----------------hdr10 options---------------------------//
    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_mode);
    ret = av_dict_set(enc_opts, "hdr_mode", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_mode");

    //-----------------hdr10 options---------------------------//
    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_flags);
    ret = av_dict_set(enc_opts, "hdr_flags", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_flags");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_matrix_range);
    ret = av_dict_set(enc_opts, "hdr_matrix_range", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_matrix_range");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_matrix);
    ret = av_dict_set(enc_opts, "hdr_matrix", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_matrix");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_primaries);
    ret = av_dict_set(enc_opts, "hdr_primaries", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_primaries");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_transfer);
    ret = av_dict_set(enc_opts, "hdr_transfer", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_transfer");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_display_r_x);
    ret = av_dict_set(enc_opts, "hdr_display_r_x", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_display_r_x");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_display_r_y);
    ret = av_dict_set(enc_opts, "hdr_display_r_y", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_display_r_y");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_display_g_x);
    ret = av_dict_set(enc_opts, "hdr_display_g_x", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_display_g_x");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_display_g_y);
    ret = av_dict_set(enc_opts, "hdr_display_g_y", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_display_g_y");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_display_b_x);
    ret = av_dict_set(enc_opts, "hdr_display_b_x", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_display_b_x");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_display_b_y);
    ret = av_dict_set(enc_opts, "hdr_display_b_y", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_display_b_y");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_display_w_x);
    ret = av_dict_set(enc_opts, "hdr_display_w_x", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_display_w_x");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_display_w_y);
    ret = av_dict_set(enc_opts, "hdr_display_w_y", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_display_w_y");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_display_lum_min);
    ret = av_dict_set(enc_opts, "hdr_display_lum_min", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_display_lum_min");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_display_lum_max);
    ret = av_dict_set(enc_opts, "hdr_display_lum_max", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_display_lum_max");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_content_lum_max);
    ret = av_dict_set(enc_opts, "hdr_content_lum_max", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_content_lum_max");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_content_lum_avg);
    ret = av_dict_set(enc_opts, "hdr_content_lum_avg", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_content_lum_avg");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_aspect_ratio_idc);
    ret = av_dict_set(enc_opts, "hdr_aspect_ratio_idc", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_aspect_ratio_idc");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_sar_width);
    ret = av_dict_set(enc_opts, "hdr_sar_width", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_sar_width");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_sar_height);
    ret = av_dict_set(enc_opts, "hdr_sar_height", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_sar_height");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_num_units_in_tick);
    ret = av_dict_set(enc_opts, "hdr_num_units_in_tick", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_num_units_in_tick");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_time_scale);
    ret = av_dict_set(enc_opts, "hdr_time_scale", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_time_scale");

    // examples for other crop:
    // (w * h)
    // (0,0)-------------------------------------------+
    // +              |              |                 +
    // +            crop_top         |                 +
    // +              |              |                 +
    // +---crop_left--+              |                 +
    // +                             |                 +
    // +                           crop_bottom         +
    // +                             |                 +
    // +--------------crop_right-----+                 +
    // +                                               +
    // +-------------------------------------------(w,h)
    return 0;
}

static int init_encode(void) {
    int            ret       = -1;
    AVDictionary*  enc_opts  = NULL;
    const AVCodec* enc_codec = NULL;

    enum AVPixelFormat pix_fmt = AV_PIX_FMT_NONE;

    fp_input_file = fopen(g_params.input_file, "rb+");
    if (!fp_input_file) {
        av_log(g_enc_ctx, AV_LOG_ERROR, "Failed to open input file[%s].\n",
               g_params.input_file);
        return -1;
    }
    fp_output_file = fopen(g_params.output_file, "wb+");
    if (!fp_output_file) {
        av_log(g_enc_ctx, AV_LOG_ERROR, "Failed to open output file[%s].\n",
               g_params.output_file);
        return -1;
    }

    // count input file data size
    fseek(fp_input_file, 0, SEEK_END);
    g_input_file_size = ftell(fp_input_file);
    fseek(fp_input_file, 0, SEEK_SET);
    if (g_input_file_size < 0) {
        av_log(g_enc_ctx, AV_LOG_ERROR,
               "ftell failed, g_input_file_size:%ld.\n", g_input_file_size);
        return -1;
    }

    pix_fmt = av_get_pix_fmt(g_params.input_fmt);
    if (pix_fmt == AV_PIX_FMT_NONE) {
        fprintf(stderr, "Invalid pixel format: %s\n", g_params.input_fmt);
        exit(1);
    }

    // find encoder codec
    enc_codec = avcodec_find_encoder_by_name(g_params.codec_name);
    if (!enc_codec) {
        fprintf(stderr, "Codec '%s' not found\n", g_params.codec_name);
        exit(1);
    }

    g_enc_ctx = avcodec_alloc_context3(enc_codec);
    if (!g_enc_ctx) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    g_enc_ctx->get_format    = get_hw_format;
    g_enc_ctx->time_base.num = 1;
    g_enc_ctx->time_base.den = g_params.enc_fps;
    g_enc_ctx->pix_fmt       = AV_PIX_FMT_TOPSCODEC;
    g_enc_ctx->sw_pix_fmt    = pix_fmt;  // yuv420p
    g_enc_ctx->width         = g_params.width;
    g_enc_ctx->height        = g_params.height;

    g_start_time = av_gettime();

    build_encoder_options(&enc_opts);

    if (avcodec_open2(g_enc_ctx, enc_codec, &enc_opts) < 0) {
        av_log(g_enc_ctx, AV_LOG_INFO, "Could not open codec, ret(%d)\n", ret);
        return -1;
    }
    av_dict_free(&enc_opts);

    if (!g_enc_ctx->hw_frames_ctx) {
        av_log(g_enc_ctx, AV_LOG_ERROR, "avctx hw_frames_ctx is NULL.\n");
        ret = AVERROR(ENOMEM);
        return -1;
    }

    return 0;
}

static void save_enc_file(AVCodecContext* g_enc_ctx, AVPacket* pkt) {
    int ret = 0;
    if (g_save_enc_file_flag == 0) {
        return;
    }
    if ((ret = fwrite(pkt->data, 1, pkt->size, fp_output_file)) < 0) {
        av_log(g_enc_ctx, AV_LOG_ERROR, "Failed to dump raw data.\n");
        return;
    }
    av_log(g_enc_ctx, AV_LOG_DEBUG, "save packet to enc file[%d]\n",
           g_packet_count);
}

static int init_hw_frame(AVFrame* frame_hw) {
    int ret = 0;
    av_frame_unref(frame_hw);
    enum AVPixelFormat pix_fmt;
    pix_fmt          = av_get_pix_fmt(g_params.input_fmt);
    frame_hw->format = AV_PIX_FMT_TOPSCODEC;
    frame_hw->width  = g_params.width;
    frame_hw->height = g_params.height;

    // 申请device memory for frame_hw
    frame_hw->hw_frames_ctx = av_buffer_ref(g_enc_ctx->hw_frames_ctx);
    if (!frame_hw->hw_frames_ctx) {
        av_log(g_enc_ctx, AV_LOG_ERROR, "av_buffer_ref failed.\n");
        return -1;
    }

    ret = av_image_fill_linesizes(frame_hw->linesize, pix_fmt, frame_hw->width);
    if (ret < 0) {
        av_log(g_enc_ctx, AV_LOG_ERROR,
               "hw_encode_tops-2  av_image_fill_linesizes failed.\n");
        return -1;
    }

    for (int i = 0; i < 4; i++) {
        frame_hw->linesize[i] =
            FFALIGN(frame_hw->linesize[i], g_params.stride_align);
    }

    ret = av_hwframe_get_buffer(frame_hw->hw_frames_ctx, frame_hw, 0);
    if (ret < 0) {
        av_log(g_enc_ctx, AV_LOG_ERROR, "av_hwframe_get_buffer failed.\n");
        return -1;
    }

    return 0;
}

static int encode(void) {
    int                ret      = 0;
    int                eos      = 0;
    AVPacket*          packet   = NULL;
    AVFrame*           frame    = NULL;
    AVFrame*           frame_hw = NULL;
    int                count_planes;
    ptrdiff_t          linesizes1[4] = {0};
    size_t             planesizes[4] = {0};
    enum AVPixelFormat pix_fmt;
    int                data_size = 0;

    pix_fmt = av_get_pix_fmt(g_params.input_fmt);

    packet   = av_packet_alloc();
    frame    = av_frame_alloc();
    frame_hw = av_frame_alloc();
    if (!packet || !frame || !frame_hw) {
        av_log(g_enc_ctx, AV_LOG_ERROR,
               "Failed to allocate packet and frame\n");
        return -1;
    }

    count_planes = av_pix_fmt_count_planes(pix_fmt);

    frame->width  = g_params.width;
    frame->height = g_params.height;
    frame->format = pix_fmt;

    data_size = av_image_get_buffer_size(
        pix_fmt, g_params.width, g_params.height, g_params.stride_align);
    if (data_size < 0) {
        av_log(g_enc_ctx, AV_LOG_ERROR, "av_image_get_buffer_size failed.\n");
        goto end;
    }

    g_guess_file_frame_count = g_input_file_size / data_size;
    if (g_guess_file_frame_count < 0) {
        av_log(g_enc_ctx, AV_LOG_ERROR, "g_guess_file_frame_count failed.\n");
        goto end;
    }

    ret = av_image_fill_linesizes(frame->linesize, frame->format, frame->width);
    if (ret < 0) {
        av_log(g_enc_ctx, AV_LOG_ERROR,
               "hw_encode_tops-1  av_image_fill_linesizes failed.\n");
        goto end;
    }

    for (int i = 0; i < count_planes; i++) {
        frame->linesize[i] = FFALIGN(frame->linesize[i], g_params.stride_align);
        linesizes1[i]      = frame->linesize[i];
        av_log(g_enc_ctx, AV_LOG_DEBUG, "ptrlinesizes[%d]:%ld\n", i,
               linesizes1[i]);
    }

    ret = av_frame_get_buffer(frame, g_params.stride_align);
    if (ret < 0) {
        av_log(g_enc_ctx, AV_LOG_ERROR, "av_frame_get_buffer failed.\n");
        goto end;
    }

    ret = av_image_fill_plane_sizes(planesizes, frame->format, frame->height,
                                    linesizes1);
    if (ret < 0) {
        av_log(g_enc_ctx, AV_LOG_ERROR, "av_image_fill_plane_sizes failed.\n");
        goto end;
    }
    while (!eos) {
        fflush(stdout);

        if (g_packet_count == 1) {
            g_first_frame_time = av_gettime();
        }
        init_hw_frame(frame_hw);
        /* Make sure the frame data is writable.
           On the first round, the frame is fresh from av_frame_get_buffer()
           and therefore we know it is writable.
           But on the next rounds, encode() will have called
           avcodec_send_frame(), and the codec may have kept a reference to
           the frame in its internal structures, that makes the frame
           unwritable.
           av_frame_make_writable() checks that and allocates a new buffer
           for the frame only if necessary.
         */
        ret = av_frame_make_writable(frame_hw);
        if (ret < 0) {
            av_log(g_enc_ctx, AV_LOG_ERROR, "av_frame_make_writable failed.\n");
            goto end;
        }
        /* fill frame data */
        for (int i = 0; i < count_planes; i++) {
            ret = fread(frame->data[i], 1, planesizes[i], fp_input_file);
            if (ret < 0) {
                av_log(g_enc_ctx, AV_LOG_ERROR, "fread failed.\n");
                goto end;
            } else if (ret == 0) {
                eos = 1;
                av_log(g_enc_ctx, AV_LOG_INFO, "fread eof.\n");
                break;
            }
            frame->linesize[i] = linesizes1[i];
            av_log(g_enc_ctx, AV_LOG_DEBUG, "linesize[%d]:%d\n", i,
                   frame->linesize[i]);
        }

        if (eos) continue;

        // transfer data from frame to frame_hw
        ret = av_hwframe_transfer_data(frame_hw, frame, 0);
        if (ret < 0) {
            av_log(g_enc_ctx, AV_LOG_ERROR,
                   "av_hwframe_transfer_data failed.\n");
            goto end;
        }
        av_log(g_enc_ctx, AV_LOG_DEBUG,
               "transfer data from frame to frame_hw success\n");
        frame_hw->pts = g_frame_count;
        g_frame_count++;
        av_log(g_enc_ctx, AV_LOG_DEBUG, "set pts to frame_hw success\n");

        // flush encoder when reach the half of the file,only for test
        if (g_flush_flag == 1 &&
            g_frame_count == g_guess_file_frame_count / 2) {
            g_flush_flag = 0;
            avcodec_flush_buffers(g_enc_ctx);
            av_log(g_enc_ctx, AV_LOG_INFO, "flush encoder...\n");
            while (ret == 0) {
                ret = avcodec_receive_packet(g_enc_ctx, packet);
                if (ret == AVERROR_EOF) {
                    av_log(g_enc_ctx, AV_LOG_INFO,
                           "flush encoder receive eos\n");
                    break;
                } else if (ret == 0) {
                    g_packet_count++;
                    save_enc_file(g_enc_ctx, packet);
                    av_packet_unref(packet);
                    av_log(g_enc_ctx, AV_LOG_DEBUG,
                           "flush encoder encode frame %d success\n",
                           g_frame_count);
                } else if (ret < 0 && ret != AVERROR(EAGAIN)) {
                    av_log(g_enc_ctx, AV_LOG_ERROR,
                           "flush encoder receive frame failed\n");
                    goto end;
                }
            }
        }

        ret = avcodec_send_frame(g_enc_ctx, frame_hw);
        if (ret < 0) {
            av_log(g_enc_ctx, AV_LOG_ERROR,
                   "avcodec_send_frame failed, ret(%d)\n", ret);
            goto end;
        }

        while (ret >= 0) {
            ret = avcodec_receive_packet(g_enc_ctx, packet);
            if (ret == AVERROR_EOF) {
                av_log(g_enc_ctx, AV_LOG_INFO, "enc receive eos\n");
                ret = 0;
                goto end;
            } else if (ret == 0) {
                g_packet_count++;
                save_enc_file(g_enc_ctx, packet);
                av_packet_unref(packet);
                av_log(g_enc_ctx, AV_LOG_DEBUG, "encode frame %d success\n",
                       g_frame_count);
            } else if (ret < 0 && ret != AVERROR(EAGAIN)) {
                av_log(g_enc_ctx, AV_LOG_ERROR, "receive frame failed\n");
                goto end;
            }
        }
    }

    ret = avcodec_send_frame(g_enc_ctx, NULL);
    if (ret < 0) {
        av_log(g_enc_ctx, AV_LOG_ERROR, "avcodec_send_frame failed\n");
        goto end;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(g_enc_ctx, packet);
        if (ret == AVERROR_EOF) {
            av_log(g_enc_ctx, AV_LOG_INFO, "enc receive eos\n");
            ret = 0;
            goto end;
        } else if (ret == 0) {
            g_packet_count++;
            save_enc_file(g_enc_ctx, packet);
            av_packet_unref(packet);
            av_log(g_enc_ctx, AV_LOG_DEBUG, "encode frame %d success\n",
                   g_packet_count);
        } else if (ret < 0 && ret != AVERROR(EAGAIN)) {
            av_log(g_enc_ctx, AV_LOG_ERROR, "receive frame failed\n");
            goto end;
        }
    }
end:

    av_packet_free(&packet);
    av_frame_free(&frame);
    av_frame_free(&frame_hw);
    avcodec_free_context(&g_enc_ctx);

    return ret;
}

/*
 * one topscodec card, has 64 cores.
 */
int main(int argc, char** argv) {
    int                 ret = -1;
    ffmpeg_log_callback fptrLog;

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 18, 100)
    /* register all formats and codecs */
    av_register_all();
#endif

    parse_params(argc, argv);

    /* Validate required parameters */
    if (g_params.input_file == NULL) {
        fprintf(stderr, "Error: Input file (-i) is required\n");
        print_usage(argv[0]);
        return -1;
    }

    // Get the DEBUG environment variable
    const char* debug_env    = getenv("DEBUG");
    int         log_level    = 0;
    int         ff_log_level = 0;

    if (debug_env != NULL) {
        log_level    = atoi(debug_env);
        ff_log_level = log_level == 0 ? AV_LOG_PANIC : AV_LOG_DEBUG;
        fptrLog      = log_callback_null;
        av_log_set_level(ff_log_level);
        av_log_set_callback(fptrLog);
        printf("DEBUG level: %d\n", log_level);
    }

    /* Print configuration */
    print_encoder_params(&g_params);

    ret = init_encode();
    if (ret < 0) {
        av_log(g_enc_ctx, AV_LOG_INFO, "init encode failed\n");
        return -1;
    }
    ret = encode();
    if (ret < 0) {
        av_log(g_enc_ctx, AV_LOG_INFO, "encode failed\n");
    }

    fclose(fp_output_file);
    fclose(fp_input_file);
    g_end_time = av_gettime();
    g_avg_fps  = g_packet_count * 1000000.0 / (g_end_time - g_start_time);
    g_first_frame_latency = (g_first_frame_time - g_start_time) / 1000;
    printf(
        "Input file size :%ld bytes,Raw video size :%dx%d [%ld], Total "
        "frames[output]:%ld,Total packets[output]:%ld, avg fps:%.2f, "
        "first frame latency:%ld ms\n",
        g_input_file_size, g_params.width, g_params.height,
        g_guess_file_frame_count, g_frame_count, g_packet_count, g_avg_fps,
        g_first_frame_latency);

    return 0;
}
