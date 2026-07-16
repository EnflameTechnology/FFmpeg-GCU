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
 * Enflame TOPSCODEC-HW-Accelerated encoding example.
 *
 * @example multi_encode_tops.c
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
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef void (*ffmpeg_log_callback)(void* ptr, int level, const char* fmt,
                                    va_list vl);

#define CHECK_RET(ret, msg)                                                    \
    if (ret < 0) {                                                             \
        fprintf(stderr, "Error: %s, line: %d, ret: %d\n", msg, __LINE__, ret); \
        return -1;                                                             \
    }

#define LOG_BUF_PREFIX_SIZE 512
#define LOG_BUF_SIZE 1024
static char logBufPrefix[LOG_BUF_PREFIX_SIZE] = {0};
static char logBuffer[LOG_BUF_SIZE]           = {0};

static int g_save_enc_file_flag = 1;

static pthread_mutex_t cb_av_log_lock;

static void log_callback_null(void* ptr, int level, const char* fmt,
                              va_list vl) {
    pthread_mutex_lock(&cb_av_log_lock);
    snprintf(logBufPrefix, LOG_BUF_PREFIX_SIZE, "%s", fmt);
    vsnprintf(logBuffer, LOG_BUF_SIZE, logBufPrefix, vl);
    printf("%s", logBuffer);
    pthread_mutex_unlock(&cb_av_log_lock);
}

// Thread management structures
typedef struct {
    int             thread_id;
    int             card_id;
    int             device_id;
    int             session_id;
    const char*     input_file;
    const char*     output_file;
    FILE*           output_file_stream;
    size_t          read_offset;
    pthread_t       thread;
    int             result;
    AVCodecContext* enc_ctx;
    const AVCodec*  enc_codec;
    AVDictionary*   enc_opts;
    int64_t         frame_count;
    int64_t         packet_count;
    int64_t         guess_file_frame_count;
    int64_t         input_file_size;
    uint64_t        start_time;
    uint64_t        first_frame_time;
    uint64_t        end_time;
    uint64_t        first_frame_latency;
    float           avg_fps;
} ThreadInfo;

static ThreadInfo* g_threads       = NULL;
static int         g_total_threads = 0;

typedef struct {
    uint8_t* data;
    size_t   size;
    size_t   frame_size;
    int64_t  frame_count;
    int      fd;
} SharedYUVBuffer;

static SharedYUVBuffer g_yuv_buffer = { .data = NULL, .size = 0,
                                       .frame_size = 0, .frame_count = 0,
                                       .fd = -1 };

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

    // multi-threading options
    int card_start;
    int card_end;
    int device_start;
    int device_end;
    int sessions;
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
    .card_start            = 0,
    .card_end              = 0,
    .device_start          = 0,
    .device_end            = 0,
    .sessions              = 1,
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
    .color_primaries       = 2,
    .color_trc             = 2,
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
    fprintf(stderr, "  Card start: %d\n", encoder_params->card_start);
    fprintf(stderr, "  Card end: %d\n", encoder_params->card_end);
    fprintf(stderr, "  Device start: %d\n", encoder_params->device_start);
    fprintf(stderr, "  Device end: %d\n", encoder_params->device_end);
    fprintf(stderr, "  Sessions: %d\n", encoder_params->sessions);
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
    fprintf(stderr, "  Balance: %d\n", encoder_params->balance);
}

static int build_encoder_options(ThreadInfo* info) {
    int  ret          = 0;
    char opt_str[128] = {0};

    //-----------------ffmpeg options---------------------------//
    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.level);
    ret = av_dict_set(&info->enc_opts, "level", opt_str, 0);
    CHECK_RET(ret, "av_dict_set level");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.profile);
    ret = av_dict_set(&info->enc_opts, "profile", opt_str, 0);
    CHECK_RET(ret, "av_dict_set profile");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.gop);
    ret = av_dict_set(&info->enc_opts, "g", opt_str, 0);
    CHECK_RET(ret, "av_dict_set gop");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.b_frame_num);
    ret = av_dict_set(&info->enc_opts, "bf", opt_str, 0);
    CHECK_RET(ret, "av_dict_set b_frame_num");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.bitrate);
    ret = av_dict_set(&info->enc_opts, "b", opt_str, 0);
    CHECK_RET(ret, "av_dict_set bitrate");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.preset);
    ret = av_dict_set(&info->enc_opts, "preset", opt_str, 0);
    CHECK_RET(ret, "av_dict_set preset");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.color_range);
    ret = av_dict_set(&info->enc_opts, "color_range", opt_str, 0);
    CHECK_RET(ret, "av_dict_set color_range");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.color_primaries);
    ret = av_dict_set(&info->enc_opts, "color_primaries", opt_str, 0);
    CHECK_RET(ret, "av_dict_set color_primaries");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.color_trc);
    ret = av_dict_set(&info->enc_opts, "color_trc", opt_str, 0);
    CHECK_RET(ret, "av_dict_set color_trc");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.colorspace);
    ret = av_dict_set(&info->enc_opts, "colorspace", opt_str, 0);
    CHECK_RET(ret, "av_dict_set colorspace");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.stride_align);
    ret = av_dict_set(&info->enc_opts, "stride_align", opt_str, 0);
    CHECK_RET(ret, "av_dict_set stride_align");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.balance);
    ret = av_dict_set(&info->enc_opts, "balance", opt_str, 0);
    CHECK_RET(ret, "av_dict_set balance");

    //-------------------topscodec options---------------------------//
    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", info->card_id);
    ret = av_dict_set(&info->enc_opts, "card_id", opt_str, 0);
    CHECK_RET(ret, "av_dict_set card_id");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", info->device_id);
    ret = av_dict_set(&info->enc_opts, "device_id", opt_str, 0);
    CHECK_RET(ret, "av_dict_set device_id");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.sf);
    ret = av_dict_set(&info->enc_opts, "sf", opt_str, 0);
    CHECK_RET(ret, "av_dict_set sf");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.qp_i);
    ret = av_dict_set(&info->enc_opts, "qp_i", opt_str, 0);
    CHECK_RET(ret, "av_dict_set qp_i");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.qp_p);
    ret = av_dict_set(&info->enc_opts, "qp_p", opt_str, 0);
    CHECK_RET(ret, "av_dict_set qp_p");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.qp_b);
    ret = av_dict_set(&info->enc_opts, "qp_b", opt_str, 0);
    CHECK_RET(ret, "av_dict_set qp_b");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.init_qp_i);
    ret = av_dict_set(&info->enc_opts, "init_qp_i", opt_str, 0);
    CHECK_RET(ret, "av_dict_set init_qp_i");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.init_qp_p);
    ret = av_dict_set(&info->enc_opts, "init_qp_p", opt_str, 0);
    CHECK_RET(ret, "av_dict_set init_qp_p");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.init_qp_b);
    ret = av_dict_set(&info->enc_opts, "init_qp_b", opt_str, 0);
    CHECK_RET(ret, "av_dict_set init_qp_b");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.enc_fps);
    ret = av_dict_set(&info->enc_opts, "enc_fps", opt_str, 0);
    CHECK_RET(ret, "av_dict_set enc_fps");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.enc_gop_type);
    ret = av_dict_set(&info->enc_opts, "enc_gop_type", opt_str, 0);
    CHECK_RET(ret, "av_dict_set enc_gop_type");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.enc_rate_control);
    ret = av_dict_set(&info->enc_opts, "enc_rate_control", opt_str, 0);
    CHECK_RET(ret, "av_dict_set enc_rate_control");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.conv_mode);
    ret = av_dict_set(&info->enc_opts, "conv_mode", opt_str, 0);
    CHECK_RET(ret, "av_dict_set conv_mode");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.out_port_num);
    ret = av_dict_set(&info->enc_opts, "out_port_num", opt_str, 0);
    CHECK_RET(ret, "av_dict_set out_port_num");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.in_port_num);
    ret = av_dict_set(&info->enc_opts, "in_port_num", opt_str, 0);
    CHECK_RET(ret, "av_dict_set in_port_num");

    // Set rotation parameters
    if (g_params.enable_rotation) {
        memset(opt_str, 0, sizeof(opt_str));
        snprintf(opt_str, sizeof(opt_str), "%d", g_params.enable_rotation);
        ret = av_dict_set(&info->enc_opts, "enable_rotation", opt_str, 0);
        CHECK_RET(ret, "av_dict_set enable_rotation");

        memset(opt_str, 0, sizeof(opt_str));
        snprintf(opt_str, sizeof(opt_str), "%d", g_params.rotation);
        ret = av_dict_set(&info->enc_opts, "rotation", opt_str, 0);
        CHECK_RET(ret, "av_dict_set rotation");
    }

    // Set crop parameters
    if (g_params.enable_crop) {
        memset(opt_str, 0, sizeof(opt_str));
        snprintf(opt_str, sizeof(opt_str), "%d", g_params.enable_crop);
        ret = av_dict_set(&info->enc_opts, "enable_crop", opt_str, 0);
        CHECK_RET(ret, "av_dict_set enable_crop");

        memset(opt_str, 0, sizeof(opt_str));
        snprintf(opt_str, sizeof(opt_str), "%d", g_params.crop_top);
        ret = av_dict_set(&info->enc_opts, "crop_top", opt_str, 0);
        CHECK_RET(ret, "av_dict_set crop_top");

        memset(opt_str, 0, sizeof(opt_str));
        snprintf(opt_str, sizeof(opt_str), "%d", g_params.crop_bottom);
        ret = av_dict_set(&info->enc_opts, "crop_bottom", opt_str, 0);
        CHECK_RET(ret, "av_dict_set crop_bottom");

        memset(opt_str, 0, sizeof(opt_str));
        snprintf(opt_str, sizeof(opt_str), "%d", g_params.crop_left);
        ret = av_dict_set(&info->enc_opts, "crop_left", opt_str, 0);
        CHECK_RET(ret, "av_dict_set crop_left");

        memset(opt_str, 0, sizeof(opt_str));
        snprintf(opt_str, sizeof(opt_str), "%d", g_params.crop_right);
        ret = av_dict_set(&info->enc_opts, "crop_right", opt_str, 0);
        CHECK_RET(ret, "av_dict_set crop_right");
    }

    // Set flip parameters
    if (g_params.enable_flip) {
        memset(opt_str, 0, sizeof(opt_str));
        snprintf(opt_str, sizeof(opt_str), "%d", g_params.enable_flip);
        ret = av_dict_set(&info->enc_opts, "enable_flip", opt_str, 0);
        CHECK_RET(ret, "av_dict_set enable_flip");

        memset(opt_str, 0, sizeof(opt_str));
        snprintf(opt_str, sizeof(opt_str), "%d", g_params.flip_model);
        ret = av_dict_set(&info->enc_opts, "flip_model", opt_str, 0);
        CHECK_RET(ret, "av_dict_set flip_model");
    }


    //-----------------hdr10 options---------------------------//
    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_mode);
    ret = av_dict_set(&info->enc_opts, "hdr_mode", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_mode");

    //-----------------hdr10 options---------------------------//
    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_flags);
    ret = av_dict_set(&info->enc_opts, "hdr_flags", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_flags");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_matrix_range);
    ret = av_dict_set(&info->enc_opts, "hdr_matrix_range", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_matrix_range");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_matrix);
    ret = av_dict_set(&info->enc_opts, "hdr_matrix", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_matrix");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_primaries);
    ret = av_dict_set(&info->enc_opts, "hdr_primaries", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_primaries");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_transfer);
    ret = av_dict_set(&info->enc_opts, "hdr_transfer", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_transfer");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_display_r_x);
    ret = av_dict_set(&info->enc_opts, "hdr_display_r_x", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_display_r_x");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_display_r_y);
    ret = av_dict_set(&info->enc_opts, "hdr_display_r_y", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_display_r_y");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_display_g_x);
    ret = av_dict_set(&info->enc_opts, "hdr_display_g_x", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_display_g_x");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_display_g_y);
    ret = av_dict_set(&info->enc_opts, "hdr_display_g_y", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_display_g_y");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_display_b_x);
    ret = av_dict_set(&info->enc_opts, "hdr_display_b_x", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_display_b_x");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_display_b_y);
    ret = av_dict_set(&info->enc_opts, "hdr_display_b_y", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_display_b_y");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_display_w_x);
    ret = av_dict_set(&info->enc_opts, "hdr_display_w_x", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_display_w_x");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_display_w_y);
    ret = av_dict_set(&info->enc_opts, "hdr_display_w_y", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_display_w_y");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_display_lum_min);
    ret = av_dict_set(&info->enc_opts, "hdr_display_lum_min", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_display_lum_min");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_display_lum_max);
    ret = av_dict_set(&info->enc_opts, "hdr_display_lum_max", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_display_lum_max");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_content_lum_max);
    ret = av_dict_set(&info->enc_opts, "hdr_content_lum_max", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_content_lum_max");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_content_lum_avg);
    ret = av_dict_set(&info->enc_opts, "hdr_content_lum_avg", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_content_lum_avg");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_aspect_ratio_idc);
    ret = av_dict_set(&info->enc_opts, "hdr_aspect_ratio_idc", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_aspect_ratio_idc");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_sar_width);
    ret = av_dict_set(&info->enc_opts, "hdr_sar_width", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_sar_width");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_sar_height);
    ret = av_dict_set(&info->enc_opts, "hdr_sar_height", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_sar_height");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_num_units_in_tick);
    ret = av_dict_set(&info->enc_opts, "hdr_num_units_in_tick", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_num_units_in_tick");

    memset(opt_str, 0, sizeof(opt_str));
    snprintf(opt_str, sizeof(opt_str), "%d", g_params.hdr_time_scale);
    ret = av_dict_set(&info->enc_opts, "hdr_time_scale", opt_str, 0);
    CHECK_RET(ret, "av_dict_set hdr_time_scale");

    // examples for other crop
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

static void print_usage(const char* prog_name) {
    fprintf(stderr, "Usage: %s [OPTIONS] -i <input_file> -o <output_file>\n",
            prog_name);
    fprintf(stderr, "\nOptions:\n");
    // save enc file
    fprintf(stderr, "  -s <save_enc_file_flag> set save enc file flag\n");
    // input
    fprintf(stderr, "  -i <input_file>     set input file\n");
    // output
    fprintf(stderr, "  -o <output_file>    set output file\n");
    // codec name
    fprintf(stderr, "  -c <codec_name>     set codec name\n");
    // input format
    fprintf(stderr, "  -pixel_format <input_fmt>      set input format\n");

    // balance
    fprintf(stderr, "  -balance <balance> set balance flag\n");

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
    fprintf(stderr, "   -card_id <start,end> set card range (e.g., 0,1)\n");
    fprintf(stderr, "   -device_id <start,end> set device range (e.g., 0,1)\n");
    fprintf(stderr,
            "   -sessions <sessions> set number of sessions per device\n");
    fprintf(stderr,
            "   -enable_rotation <enable_rotation> set enable rotation\n");
    fprintf(stderr, "   -rotation <rotation>    set rotation\n");
    fprintf(stderr, "   -enable_crop <enable_crop> set enable crop\n");
    fprintf(stderr,
            "   -crop <top,bottom,left,right> set crop values (e.g., "
            "10,10,10,10)\n");
    fprintf(stderr, "   -enable_flip <enable_flip> set enable flip\n");
    fprintf(stderr, "   -flip_model <flip_model> set flip model\n");

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
            "-card_id 0,1 -device_id 0,1 "
            "-sessions 1 -i input.yuv -o output.h264\n",
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
            "-enable_crop 1 -crop 10,10,10,10 -i input.yuv -o output.h264\n",
            prog_name);
    fprintf(stderr, "  With flip:\n");
    fprintf(stderr,
            " DEBUG=1   %s -video_size 1920x1080 -pixel_format yuv420p -c "
            "h264_topscodec_enc "
            "-enable_flip 1 -flip_model 1 -i input.yuv -o output.h264\n",
            prog_name);
    fprintf(stderr, "  Multi-threaded encoding:\n");
    fprintf(stderr,
            " DEBUG=1   %s -video_size 1920x1080 -pixel_format yuv420p -c "
            "h264_topscodec_enc "
            "-card_id 0,1 -device_id 0,3 "
            "-sessions 2 -i input.yuv -o output.h264\n",
            prog_name);
}

static void parse_args(int argc, char** argv) {
    int i = 0;
    /* Parse command line arguments */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "-stride_align") == 0) {
            g_params.stride_align = atoi(argv[i + 1]);
            i++;
        } else if (strcmp(argv[i], "-c") == 0) {
            g_params.codec_name = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "-pixel_format") == 0) {
            g_params.input_fmt = argv[i + 1];
            i++;
        } else if (strcmp(argv[i], "-s") == 0) {
            g_save_enc_file_flag = atoi(argv[i + 1]);
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
        } else if (strcmp(argv[i], "-balance") == 0) {
            g_params.balance = atoi(argv[i + 1]);
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
        } else if (strcmp(argv[i], "-card_id") == 0) {
            char* range = argv[i + 1];
            char* comma = strchr(range, ',');
            if (comma) {
                *comma              = '\0';
                g_params.card_start = atoi(range);
                g_params.card_end   = atoi(comma + 1);
            } else {
                g_params.card_start = atoi(range);
                g_params.card_end   = g_params.card_start + 1;
            }
            i++;
        } else if (strcmp(argv[i], "-device_id") == 0) {
            char* range = argv[i + 1];
            char* comma = strchr(range, ',');
            if (comma) {
                *comma                = '\0';
                g_params.device_start = atoi(range);
                g_params.device_end   = atoi(comma + 1);
            } else {
                g_params.device_start = atoi(range);
                g_params.device_end   = g_params.device_start + 1;
            }
            i++;
        } else if (strcmp(argv[i], "-sessions") == 0) {
            g_params.sessions = atoi(argv[i + 1]);
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
        } else {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            exit(1);
        }
    }
}

static void unload_yuv_file(void) {
    if (g_yuv_buffer.data && g_yuv_buffer.data != MAP_FAILED) {
        munmap(g_yuv_buffer.data, g_yuv_buffer.size);
    }
    if (g_yuv_buffer.fd >= 0) {
        close(g_yuv_buffer.fd);
    }
    memset(&g_yuv_buffer, 0, sizeof(g_yuv_buffer));
    g_yuv_buffer.fd = -1;
}

static void mmap_buffer_noop_free(void *opaque, uint8_t *data) {
    (void)opaque;
    (void)data;
}

static int preload_yuv_file(void) {
    struct stat        st;
    enum AVPixelFormat pix_fmt;
    int                count_planes;
    AVFrame            tmp_frame = {0};
    ptrdiff_t          linesizes1[4] = {0};
    size_t             planesizes[4] = {0};
    int                ret;

    if (!g_params.input_file) {
        fprintf(stderr, "No input file specified, skipping mmap preload\n");
        return 0;
    }

    g_yuv_buffer.fd = open(g_params.input_file, O_RDONLY);
    if (g_yuv_buffer.fd < 0) {
        fprintf(stderr, "Failed to open input file: %s\n", g_params.input_file);
        return -1;
    }

    if (fstat(g_yuv_buffer.fd, &st) < 0) {
        fprintf(stderr, "fstat failed for input file\n");
        unload_yuv_file();
        return -1;
    }
    g_yuv_buffer.size = st.st_size;

    g_yuv_buffer.data = (uint8_t*)mmap(NULL, g_yuv_buffer.size, PROT_READ,
                                       MAP_SHARED, g_yuv_buffer.fd, 0);
    if (g_yuv_buffer.data == MAP_FAILED) {
        fprintf(stderr, "mmap failed for input file\n");
        g_yuv_buffer.data = NULL;
        unload_yuv_file();
        return -1;
    }

    posix_madvise(g_yuv_buffer.data, g_yuv_buffer.size, POSIX_MADV_SEQUENTIAL);

    pix_fmt = av_get_pix_fmt(g_params.input_fmt);
    if (pix_fmt == AV_PIX_FMT_NONE) {
        fprintf(stderr, "Invalid pixel format: %s\n", g_params.input_fmt);
        unload_yuv_file();
        return -1;
    }

    tmp_frame.width  = g_params.width;
    tmp_frame.height = g_params.height;
    tmp_frame.format = pix_fmt;

    ret = av_image_fill_linesizes(tmp_frame.linesize, pix_fmt, g_params.width);
    if (ret < 0) {
        fprintf(stderr, "av_image_fill_linesizes failed\n");
        unload_yuv_file();
        return -1;
    }

    for (int i = 0; i < 4; i++) {
        tmp_frame.linesize[i] = FFALIGN(tmp_frame.linesize[i],
                                        g_params.stride_align);
    }

    count_planes = av_pix_fmt_count_planes(pix_fmt);
    for (int i = 0; i < count_planes; i++) {
        linesizes1[i] = tmp_frame.linesize[i];
    }

    ret = av_image_fill_plane_sizes(planesizes, pix_fmt, g_params.height,
                                    linesizes1);
    if (ret < 0) {
        fprintf(stderr, "av_image_fill_plane_sizes failed\n");
        unload_yuv_file();
        return -1;
    }

    g_yuv_buffer.frame_size = 0;
    for (int i = 0; i < count_planes; i++) {
        g_yuv_buffer.frame_size += planesizes[i];
    }

    if (g_yuv_buffer.frame_size > 0) {
        g_yuv_buffer.frame_count = g_yuv_buffer.size / g_yuv_buffer.frame_size;
    } else {
        g_yuv_buffer.frame_count = 0;
    }

    fprintf(stderr,
            "Preloaded YUV file: %s, size: %zu, frame_size: %zu, "
            "frame_count: %ld\n",
            g_params.input_file, g_yuv_buffer.size, g_yuv_buffer.frame_size,
            g_yuv_buffer.frame_count);

    return 0;
}

static int init_encode_thread(ThreadInfo* info) {
    char               output_filename[256] = {0};
    enum AVPixelFormat pix_fmt              = AV_PIX_FMT_NONE;

    // Initialize enc_opts to NULL
    info->enc_opts = NULL;

    // Generate unique output filename for this thread
    memset(output_filename, 0, sizeof(output_filename));
    snprintf(output_filename, sizeof(output_filename),
             "card%d_dev%d_session%d_%s", info->card_id, info->device_id,
             info->session_id, info->output_file);

    info->output_file_stream = fopen(output_filename, "wb+");
    if (!info->output_file_stream) {
        av_log(info->enc_ctx, AV_LOG_ERROR,
               "[Thread %d] Failed to open output file[%s]\n", info->thread_id,
               output_filename);
        return -1;
    }

    pix_fmt = av_get_pix_fmt(g_params.input_fmt);
    if (pix_fmt == AV_PIX_FMT_NONE) {
        av_log(info->enc_ctx, AV_LOG_ERROR,
               "[Thread %d] Invalid pixel format: %s\n", info->thread_id,
               g_params.input_fmt);
        return -1;
    }
    // find encoder codec
    info->enc_codec = avcodec_find_encoder_by_name(g_params.codec_name);
    if (!info->enc_codec) {
        av_log(info->enc_ctx, AV_LOG_ERROR,
               "[Thread %d] Codec '%s' not found\n", info->thread_id,
               g_params.codec_name);
        return -1;
    }

    info->enc_ctx = avcodec_alloc_context3(info->enc_codec);
    if (!info->enc_ctx) {
        av_log(info->enc_ctx, AV_LOG_ERROR,
               "[Thread %d] Could not allocate video codec context\n",
               info->thread_id);
        return -1;
    }
    info->enc_ctx->time_base.num = 1;
    info->enc_ctx->time_base.den = g_params.enc_fps;
    info->enc_ctx->pix_fmt       = pix_fmt;  // yuv420p
    info->enc_ctx->width         = g_params.width;
    info->enc_ctx->height        = g_params.height;

    build_encoder_options(info);

    //开始计时
    info->start_time = av_gettime();

    // Serialize TOPS codec initialization to avoid race conditions
    // The TOPS runtime library might not be thread-safe during initialization
    av_log(info->enc_ctx, AV_LOG_INFO,
           "[Thread %d] Initializing TOPS codec...\n", info->thread_id);

    // Add error handling for TOPS runtime issues
    int open_result =
        avcodec_open2(info->enc_ctx, info->enc_codec, &info->enc_opts);
    if (open_result < 0) {
        av_log(info->enc_ctx, AV_LOG_ERROR,
               "[Thread %d] Could not open codec, ret(%d)\n", info->thread_id,
               open_result);
        return -1;
    }
    av_log(info->enc_ctx, AV_LOG_INFO,
           "Thread %d: TOPS codec initialized successfully\n", info->thread_id);
    av_dict_free(&info->enc_opts);

    return 0;
}

static void save_enc_file_thread(ThreadInfo* info, AVPacket* pkt) {
    int ret = 0;
    if (g_save_enc_file_flag == 0) {
        return;
    }
    if ((ret = fwrite(pkt->data, 1, pkt->size, info->output_file_stream)) < 0) {
        av_log(info->enc_ctx, AV_LOG_ERROR,
               "[Thread %d] Failed to dump raw data.\n", info->thread_id);
        return;
    }
    av_log(info->enc_ctx, AV_LOG_DEBUG, "[Thread %d] save packet to enc file\n",
           info->thread_id);
}

static int init_frame(AVFrame* frame, AVCodecContext* enc_ctx) {
    int ret = 0;
    av_frame_unref(frame);
    frame->width  = g_params.width;
    frame->height = g_params.height;
    frame->format = av_get_pix_fmt(g_params.input_fmt);

    ret = av_image_fill_linesizes(frame->linesize, frame->format, frame->width);
    if (ret < 0) {
        av_log(enc_ctx, AV_LOG_ERROR, "av_image_fill_linesizes failed.\n");
        return -1;
    }

    for (int i = 0; i < 4; i++) {
        frame->linesize[i] = FFALIGN(frame->linesize[i], g_params.stride_align);
    }

    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        av_log(enc_ctx, AV_LOG_ERROR, "av_frame_get_buffer failed.\n");
        return -1;
    }

    return 0;
}

static int encode_thread(ThreadInfo* info) {
    int                ret               = 0;
    int                eos               = 0;
    AVPacket*          packet            = NULL;
    AVFrame*           frame             = NULL;
    enum AVPixelFormat pix_fmt;
    int                count_planes;
    ptrdiff_t          linesizes1[4]     = {0};
    size_t             planesizes[4]     = {0};
    size_t             frame_size        = 0;
    int                saved_linesize[4] = {0};

    ret = init_encode_thread(info);
    if (ret < 0) {
        av_log(info->enc_ctx, AV_LOG_ERROR,
               "[Thread %d] init encode thread failed\n", info->thread_id);
        return -1;
    }

    packet = av_packet_alloc();
    frame  = av_frame_alloc();
    if (!packet || !frame) {
        av_log(info->enc_ctx, AV_LOG_ERROR,
               "[Thread %d] Failed to allocate packet and frame\n",
               info->thread_id);
        goto end;
    }

    pix_fmt      = av_get_pix_fmt(g_params.input_fmt);
    count_planes = av_pix_fmt_count_planes(pix_fmt);

    ret = av_image_fill_linesizes(saved_linesize, pix_fmt, g_params.width);
    if (ret < 0) {
        av_log(info->enc_ctx, AV_LOG_ERROR,
               "[Thread %d] av_image_fill_linesizes failed.\n",
               info->thread_id);
        goto end;
    }
    for (int i = 0; i < 4; i++) {
        saved_linesize[i] = FFALIGN(saved_linesize[i], g_params.stride_align);
    }

    for (int i = 0; i < count_planes; i++) {
        linesizes1[i] = saved_linesize[i];
        av_log(info->enc_ctx, AV_LOG_DEBUG,
               "[Thread %d] linesize[%d]:%ld\n", info->thread_id, i,
               linesizes1[i]);
    }

    ret = av_image_fill_plane_sizes(planesizes, pix_fmt, g_params.height,
                                    linesizes1);
    if (ret < 0) {
        av_log(info->enc_ctx, AV_LOG_ERROR,
               "[Thread %d] av_image_fill_plane_sizes failed.\n",
               info->thread_id);
        goto end;
    }

    for (int i = 0; i < count_planes; i++) {
        frame_size += planesizes[i];
    }

    if (g_yuv_buffer.data) {
        info->input_file_size        = g_yuv_buffer.size;
        info->guess_file_frame_count = g_yuv_buffer.frame_count;
        info->read_offset            = 0;
    } else {
        ret = init_frame(frame, info->enc_ctx);
        if (ret < 0) {
            av_log(info->enc_ctx, AV_LOG_ERROR,
                   "[Thread %d] init_frame failed.\n", info->thread_id);
            goto end;
        }
        info->input_file_size        = 0;
        info->guess_file_frame_count = 1000;
    }

    while (!eos) {
        if (info->packet_count == 1) {
            info->first_frame_time = av_gettime();
        }

        if (g_yuv_buffer.data) {
            if (info->read_offset + frame_size > g_yuv_buffer.size) {
                eos = 1;
                av_log(info->enc_ctx, AV_LOG_DEBUG,
                       "[Thread %d] mmap buffer eof.\n", info->thread_id);
                break;
            }
            av_frame_unref(frame);
            frame->width  = g_params.width;
            frame->height = g_params.height;
            frame->format = pix_fmt;
            for (int i = 0; i < 4; i++)
                frame->linesize[i] = saved_linesize[i];

            uint8_t *base  = g_yuv_buffer.data + info->read_offset;
            size_t   offset = 0;
            for (int i = 0; i < count_planes; i++) {
                frame->data[i] = base + offset;
                offset += planesizes[i];
            }
            frame->buf[0] = av_buffer_create(
                base, frame_size, mmap_buffer_noop_free, NULL,
                AV_BUFFER_FLAG_READONLY);
            info->read_offset += frame_size;
        } else {
            ret = av_frame_make_writable(frame);
            if (ret < 0) {
                av_log(info->enc_ctx, AV_LOG_ERROR,
                       "[Thread %d] av_frame_make_writable failed.\n",
                       info->thread_id);
                goto end;
            }
            if (info->frame_count > info->guess_file_frame_count) {
                eos = 1;
                av_log(info->enc_ctx, AV_LOG_DEBUG,
                       "[Thread %d] frame count > guess_file_frame_count\n",
                       info->thread_id);
                break;
            }
            for (int i = 0; i < count_planes; i++) {
                memset(frame->data[i], 0, planesizes[i]);
            }
        }

        frame->pts = info->frame_count;
        if (eos) continue;
        info->frame_count++;

        ret = avcodec_send_frame(info->enc_ctx, frame);
        if (ret < 0) {
            av_log(info->enc_ctx, AV_LOG_ERROR,
                   "[Thread %d] avcodec_send_frame failed, ret(%d)\n",
                   info->thread_id, ret);
            goto end;
        }

        while (ret >= 0) {
            ret = avcodec_receive_packet(info->enc_ctx, packet);
            if (ret == AVERROR_EOF) {
                av_log(info->enc_ctx, AV_LOG_DEBUG,
                       "[Thread %d] enc receive eos\n", info->thread_id);
                ret = 0;
                goto end;
            } else if (ret == 0) {
                info->packet_count++;
                save_enc_file_thread(info, packet);
                av_packet_unref(packet);
                av_log(info->enc_ctx, AV_LOG_DEBUG,
                       "[Thread %d] encode frame %ld success\n",
                       info->thread_id, info->packet_count);
            } else if (ret < 0 && ret != AVERROR(EAGAIN)) {
                av_log(info->enc_ctx, AV_LOG_ERROR,
                       "[Thread %d] receive frame failed\n", info->thread_id);
                goto end;
            }
        }
    }
    // flush encoder
    av_log(info->enc_ctx, AV_LOG_DEBUG, "[Thread %d] flush encoder\n",
           info->thread_id);
    ret = avcodec_send_frame(info->enc_ctx, NULL);
    if (ret < 0) {
        av_log(info->enc_ctx, AV_LOG_ERROR,
               "[Thread %d] avcodec_send_frame failed\n", info->thread_id);
        goto end;
    }

    while (ret == 0 || ret == AVERROR(EAGAIN)) {
        ret = avcodec_receive_packet(info->enc_ctx, packet);
        if (ret == AVERROR_EOF) {
            av_log(info->enc_ctx, AV_LOG_DEBUG, "[Thread %d] enc receive eos\n",
                   info->thread_id);
            ret = 0;
            goto end;
        } else if (ret == 0) {
            info->packet_count++;
            save_enc_file_thread(info, packet);
            av_packet_unref(packet);
        } else if (ret < 0 && ret != AVERROR(EAGAIN)) {
            av_log(info->enc_ctx, AV_LOG_ERROR,
                   "[Thread %d] receive frame failed\n", info->thread_id);
            goto end;
        }
    }

end:
    info->end_time = av_gettime();

    //打印endtime和first_frame_time
    av_log(info->enc_ctx, AV_LOG_INFO,
           "[Thread %d] end_time:%ld, first_frame_time:%ld, diff:%ld\n",
           info->thread_id, info->end_time, info->first_frame_time,
           info->end_time - info->first_frame_time);
    info->avg_fps = (info->frame_count - 1) * 1000000.0 /
                    (info->end_time - info->first_frame_time);
    info->first_frame_latency = info->first_frame_time - info->start_time;
    printf(
        "[Thread %d] encode success, Input file size :%ld bytes,Raw video size "
        ":%dx%d [%ld], Total frames[input]:%ld, "
        "Total packets[output]:%ld, avg "
        "fps:%.2f, first frame latency:%ld ms\n",
        info->thread_id, info->input_file_size, g_params.width, g_params.height,
        info->guess_file_frame_count, info->frame_count, info->packet_count,
        info->avg_fps, info->first_frame_latency);
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&info->enc_ctx);
    if (info->output_file_stream) fclose(info->output_file_stream);

    return ret;
}

static void* thread_worker(void* arg) {
    ThreadInfo* info = (ThreadInfo*)arg;
    info->result     = encode_thread(info);
    return NULL;
}

int main(int argc, char** argv) {
    ffmpeg_log_callback fptrLog;
    int                 log_level     = 0;
    int                 ff_log_level  = 0;
    int                 success_count = 0;
    int                 total_cards   = 0;
    int                 total_devices = 0;
    int                 thread_idx    = 0;
    int                 card          = 0;
    int                 device        = 0;
    int                 session       = 0;
    int                 i             = 0;

    char thread_name[256] = {0};

    const char* debug_env = getenv("DEBUG");

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 18, 100)
    /* register all formats and codecs */
    av_register_all();
#endif

    parse_args(argc, argv);

    /* Validate required parameters */
    // if (g_params.input_file == NULL) {
    //     fprintf(stderr, "Error: Input file (-i) is required\n");
    //     print_usage(argv[0]);
    //     return -1;
    // }
    if (g_params.output_file == NULL) {
        fprintf(stderr, "Error: Output file (-o) is required\n");
        print_usage(argv[0]);
        return -1;
    }

    // Get the DEBUG environment variable

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

    if (preload_yuv_file() < 0) {
        fprintf(stderr, "Failed to preload YUV file\n");
        return -1;
    }

    // Initialize mutex
    pthread_mutex_init(&cb_av_log_lock, NULL);

    if (g_params.card_start == 0 && g_params.card_end == 0) {
        g_params.card_start = 0;
        g_params.card_end   = 1;
    }
    if (g_params.device_start == 0 && g_params.device_end == 0) {
        g_params.device_start = 0;
        g_params.device_end   = 1;
    }
    if (g_params.sessions == 0) {
        g_params.sessions = 1;
    }

    if (g_params.card_end - g_params.card_start == 0 ||
        g_params.card_end - g_params.card_start > 8 ||
        g_params.device_end - g_params.device_start == 0 ||
        g_params.device_end - g_params.device_start > 8) {
        fprintf(stderr,
                "card_start and card_end or device_start and device_end must "
                "be different\n");
        print_usage(argv[0]);
        return -1;
    }

    // Calculate total threads needed
    total_cards     = g_params.card_end - g_params.card_start;
    total_devices   = g_params.device_end - g_params.device_start;
    g_total_threads = total_cards * total_devices * g_params.sessions;

    printf("Starting multi-threaded encoding with %d threads\n",
           g_total_threads);
    printf("Cards: %d-%d, Devices: %d-%d, Sessions per device: %d\n",
           g_params.card_start, g_params.card_end, g_params.device_start,
           g_params.device_end, g_params.sessions);

    // Allocate thread info array
    g_threads = (ThreadInfo*)av_malloc(g_total_threads * sizeof(ThreadInfo));
    if (!g_threads) {
        fprintf(stderr, "Failed to allocate memory for threads\n");
        return -1;
    }

    // Create threads
    thread_idx = 0;
    for (card = g_params.card_start; card < g_params.card_end; card++) {
        for (device = g_params.device_start; device < g_params.device_end;
             device++) {
            for (session = 0; session < g_params.sessions; session++) {
                snprintf(thread_name, sizeof(thread_name), "thread_%d_%d_%d_%d",
                         card, device, session, thread_idx);
                ThreadInfo* info  = &g_threads[thread_idx];
                info->thread_id   = thread_idx;
                info->card_id     = card;
                info->device_id   = device * 2;
                info->session_id  = session;
                info->input_file  = g_params.input_file;
                info->output_file = g_params.output_file;
                info->result      = 0;

                if (pthread_create(&info->thread, NULL, thread_worker, info) !=
                    0) {
                    fprintf(stderr, "Failed to create thread %s\n",
                            thread_name);
                    return -1;
                }
                fprintf(stderr, "Created thread %s successfully\n",
                        thread_name);

                thread_idx++;
            }
        }
    }

    // Wait for all threads to complete
    for (i = 0; i < g_total_threads; i++) {
        pthread_join(g_threads[i].thread, NULL);
        if (g_threads[i].result == 0) {
            success_count++;
        }
    }

    // Cleanup
    unload_yuv_file();
    free(g_threads);
    pthread_mutex_destroy(&cb_av_log_lock);

    printf(
        "Multi-threaded encoding finished. %d/%d threads completed "
        "successfully\n",
        success_count, g_total_threads);

    return (success_count == g_total_threads) ? 0 : -1;
}
