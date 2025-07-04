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
 * Enflame TOPSCODEC-HW-Accelerated decoding example.
 *
 * @example decode_tops.c
 * This example shows how to do topscodec-HW-accelerated decoding with output
 * frames from the video surfaces.
 */

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*ffmpeg_log_callback)(void* ptr, int level, const char* fmt, va_list vl);

#define LOG_BUF_PREFIX_SIZE 512
#define LOG_BUF_SIZE 1024
static char             logBufPrefix[LOG_BUF_PREFIX_SIZE] = {0};
static char             logBuffer[LOG_BUF_SIZE]           = {0};
static FILE*            fp_yuv                            = NULL;
static AVCodecContext*  g_dec_ctx                         = NULL;
static AVFormatContext* g_ifmt_ctx                        = NULL;
static int              video_stream_idx                  = 0;
static int              g_frame_count                     = 0;
static pthread_mutex_t  cb_av_log_lock;

static int init_decode(const char* in_file, const char* out_file, const char* dev_id, const char* card_id,
                       const char* out_fmt) {
    int            ret      = -1;
    AVStream*      video    = NULL;
    AVDictionary*  options  = NULL;
    AVDictionary*  dec_opts = NULL;
    AVInputFormat* fmt      = NULL;
    const AVCodec* p_codec  = NULL;
    const char*    tmp_name = NULL;

    if ((ret = avformat_network_init()) != 0) {
        av_log(g_dec_ctx, AV_LOG_INFO, "avformat_network_init failed, ret(%d)\n", ret);
        return ret;
    }
    if (!strncmp(in_file, "rtsp", 4) || !strncmp(in_file, "rtmp", 4)) {
        av_log(g_dec_ctx, AV_LOG_INFO, "decode rtsp/rtmp stream\n");
        av_dict_set(&options, "buffer_size", "1024000", 0);
        av_dict_set(&options, "max_delay", "500000", 0);
        av_dict_set(&options, "stimeout", "20000000", 0);
        av_dict_set(&options, "rtsp_transport", "tcp", 0);
    } else {
        av_log(g_dec_ctx, AV_LOG_INFO, "decode local file stream\n");
        av_dict_set(&options, "stimeout", "20000000", 0);
        av_dict_set(&options, "vsync", "0", 0);
    }

    // g_ifmt_ctx = avformat_alloc_context();

    tmp_name = &in_file[strlen(in_file) - 4];
    if (!strcmp(tmp_name, "cavs") || !strcmp(tmp_name, ".avs")) {
        fmt = av_find_input_format("cavsvideo");
    } else {
        fmt = NULL;
    }

    ret = avformat_open_input(&g_ifmt_ctx, in_file, fmt, &options);
    av_dict_free(&options);
    if (ret < 0) {
        av_log(g_dec_ctx, AV_LOG_INFO, "avformat_open_input failed[%s], ret(%d)\n", in_file, ret);
        return ret;
    }
    ret = avformat_find_stream_info(g_ifmt_ctx, NULL);
    if (ret < 0) {
        av_log(g_dec_ctx, AV_LOG_INFO, "avformat_find_stream_info failed, ret(%d)\n", ret);
        return ret;
    }
    for (size_t i = 0; i < g_ifmt_ctx->nb_streams; i++) {
        if (g_ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video            = g_ifmt_ctx->streams[i];
            video_stream_idx = i;
            break;
        }
    }
    if (NULL == video) {
        av_log(g_dec_ctx, AV_LOG_ERROR, "video stream is NULL\n");
        return -1;
    }

    av_dump_format(g_ifmt_ctx, 0, in_file, 0);

    switch (video->codecpar->codec_id) {
        case AV_CODEC_ID_H264:
            p_codec = avcodec_find_decoder_by_name("h264_topscodec");
            break;
        case AV_CODEC_ID_HEVC:
            p_codec = avcodec_find_decoder_by_name("hevc_topscodec");
            break;
        case AV_CODEC_ID_VP8:
            p_codec = avcodec_find_decoder_by_name("vp8_topscodec");
            break;
        case AV_CODEC_ID_VP9:
            p_codec = avcodec_find_decoder_by_name("vp9_topscodec");
            break;
        case AV_CODEC_ID_MJPEG:
            p_codec = avcodec_find_decoder_by_name("mjpeg_topscodec");
            break;
        case AV_CODEC_ID_H263:
            p_codec = avcodec_find_decoder_by_name("h263_topscodec");
            break;
        case AV_CODEC_ID_MPEG2VIDEO:
            p_codec = avcodec_find_decoder_by_name("mpeg2_topscodec");
            break;
        case AV_CODEC_ID_MPEG4:
            p_codec = avcodec_find_decoder_by_name("mpeg4_topscodec");
            break;
        case AV_CODEC_ID_VC1:
            p_codec = avcodec_find_decoder_by_name("vc1_topscodec");
            break;
        case AV_CODEC_ID_CAVS:
            p_codec = avcodec_find_decoder_by_name("avs_topscodec");
            break;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 18, 100)
        case AV_CODEC_ID_AVS2:
            p_codec = avcodec_find_decoder_by_name("avs2_topscodec");
            break;
        case AV_CODEC_ID_AV1:
            p_codec = avcodec_find_decoder_by_name("av1_topscodec");
            break;
#endif
        default:
            p_codec = avcodec_find_decoder(video->codecpar->codec_id);
            break;
    }
    if (p_codec == NULL) {
        av_log(g_dec_ctx, AV_LOG_INFO, "Unsupported codec! \n");
        return -1;
    }
    g_dec_ctx = avcodec_alloc_context3(p_codec);
    if (avcodec_parameters_to_context(g_dec_ctx, video->codecpar) != 0) {
        av_log(g_dec_ctx, AV_LOG_INFO, "Could not copy codec context, ret(%d)\n", ret);
        return -1;
    }

    if (card_id) av_dict_set(&dec_opts, "card_id", card_id, 0);
    if (dev_id) av_dict_set(&dec_opts, "device_id", dev_id, 0);
    if (out_fmt) /* for color space trans*/
        av_dict_set(&dec_opts, "output_pixfmt", out_fmt, 0);
    /*+++++++++++++++++++++++++++++++++++++++++++++++++++++*/
    // examples for other options:
    // 1. set the out_port_num
    // av_dict_set(&dec_opts, "out_port_num", "6", 0);
    // 2. set the zero_copy
    // av_dict_set(&dec_opts, "zero_copy", "1", 0);
    // 3. set the rotation (only support orientation,90/180/270)
    // av_dict_set(&dec_opts, "enable_rotation", "1", 0);
    // av_dict_set(&dec_opts, "rotation", "90", 0);
    // 4. set the crop
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
    // av_dict_set(&dec_opts, "enable_crop", "1", 0);
    // av_dict_set(&dec_opts, "crop_left", "20", 0);
    // av_dict_set(&dec_opts, "crop_top", "20", 0);
    // av_dict_set(&dec_opts, "crop_right", "1900", 0);
    // av_dict_set(&dec_opts, "crop_bottom", "1060", 0);
    // 5. set the resize (0-Bilinear, 1-Nearest)
    // av_dict_set(&dec_opts, "enable_resize", "1", 0);
    // av_dict_set(&dec_opts, "resize_w", "640", 0);
    // av_dict_set(&dec_opts, "resize_h", "360", 0);
    // av_dict_set(&dec_opts, "resize_m", "0", 0);
    // 6. set the idr_only (only decode the IDR frame)
    // av_dict_set(&dec_opts, "idr", "1", 0);
    // 7. set the interval (decode the frame every interval)
    // if interval is 2, x00x00x00x00x.., x is the frame, 0 is the discard frame
    // av_dict_set(&dec_opts, "sfo", "2", 0);
    // 8. set the balance rate (0-300)
    // recommend value:single core:0, multi-core:5
    // av_dict_set(&dec_opts, "sf", "5", 0);
    // 9. set the output_colorspace
    // support bt601, bt709, bt2020, bt601f, bt709f, bt2020f
    // av_dict_set(&dec_opts, "output_colorspace", "bt709", 0);
    /*+++++++++++++++++++++++++++++++++++++++++++++++++++++*/
    if (avcodec_open2(g_dec_ctx, p_codec, &dec_opts) < 0) {
        av_log(g_dec_ctx, AV_LOG_INFO, "Could not open codec, ret(%d)\n", ret);
        return -1;
    }
    av_dict_free(&dec_opts);

    fp_yuv = fopen(out_file, "wb+");

    return 0;
}

static void save_yuv_file(AVCodecContext* dec_ctx, AVFrame* frame) {
    int      size   = 0;
    int      ret    = 0;
    uint8_t* buffer = NULL;

    size = av_image_get_buffer_size(frame->format, frame->width, frame->height, 1);

    buffer = av_malloc(size);
    if (!buffer) {
        av_log(dec_ctx, AV_LOG_ERROR, "Can not alloc buffer\n");
        return;
    }

    ret = av_image_copy_to_buffer(buffer, size, (const uint8_t* const*)frame->data, (const int*)frame->linesize,
                                  frame->format, frame->width, frame->height, 1);
    if (ret < 0) {
        av_log(dec_ctx, AV_LOG_ERROR, "Can not copy image to buffer\n");
        return;
    }

    if ((ret = fwrite(buffer, 1, size, fp_yuv)) < 0) {
        av_log(dec_ctx, AV_LOG_ERROR, "Failed to dump raw data.\n");
        return;
    }
    av_log(dec_ctx, AV_LOG_DEBUG, "save frame to yuv file[%d]\n", g_frame_count);
    av_free(buffer);
}

static int decode(AVCodecContext* dec_ctx) {
    int      ret;
    AVPacket packet;
    AVFrame* p_frame;
    int      eos = 0;

    p_frame = av_frame_alloc();

    while (1) {
        ret = av_read_frame(g_ifmt_ctx, &packet);
        if (ret == AVERROR_EOF) {
            av_log(g_dec_ctx, AV_LOG_INFO, "av_read_frame got eof\n");
            eos = 1;
        } else if (ret < 0) {
            av_log(g_dec_ctx, AV_LOG_ERROR, "av_read_frame failed, ret(%d)\n", ret);
            goto fail;
        }

        if (packet.stream_index != video_stream_idx) {
            av_packet_unref(&packet);
            continue;
        }
        packet.dts = 0;
        av_log(g_dec_ctx, AV_LOG_DEBUG, "packet pts[%ld], dts:[%ld]\n", packet.pts, packet.dts);
        ret = avcodec_send_packet(dec_ctx, &packet);
        if (ret < 0) {
            av_log(dec_ctx, AV_LOG_ERROR, "send pkt failed, ret(%d), %s, %d\n", ret, __FILE__, __LINE__);
            goto fail;
        }

        while (ret >= 0 || eos) {
            printf("decode frame=====\n");
            ret = avcodec_receive_frame(dec_ctx, p_frame);
            if (ret == AVERROR_EOF) {
                av_log(g_dec_ctx, AV_LOG_INFO, "dec receive eos\n");
                av_frame_unref(p_frame);
                av_frame_free(&p_frame);
                return 0;
            } else if (ret == 0) {
                g_frame_count++;
                save_yuv_file(dec_ctx, p_frame);
                av_frame_unref(p_frame);
            } else if (ret < 0 && ret != AVERROR(EAGAIN)) {
                av_log(dec_ctx, AV_LOG_ERROR, "receive frame failed\n");
                goto fail;
            }
        }
        av_packet_unref(&packet);
    }

fail:
    av_frame_free(&p_frame);
    return -1;
}

static void log_callback_null(void* ptr, int level, const char* fmt, va_list vl) {
    pthread_mutex_lock(&cb_av_log_lock);
    snprintf(logBufPrefix, LOG_BUF_PREFIX_SIZE, "%s", fmt);
    vsnprintf(logBuffer, LOG_BUF_SIZE, logBufPrefix, vl);
    printf("%s", logBuffer);
    pthread_mutex_unlock(&cb_av_log_lock);
}

/*
 * one topscodec card, has 64 cores.
 */
int main(int argc, char** argv) {
    int                 ret = -1;
    const char *        in_file, *out_file, *dev_id, *out_fmt, *card_id;
    ffmpeg_log_callback fptrLog;

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 18, 100)
    /* register all formats and codecs */
    av_register_all();
#endif

    if (argc < 5) {
        fprintf(stderr,
                "Usage:%s <input file> <output file> <card id> <dev id> "
                "<out fmt>"
                "\n card_id 0~7"
                "\n dev_id 0~7"
                "\n out formt:"
                "\n yuv420p"
                "\n rgb24"
                "\n bgr24"
                "\n rgb24p"
                "\n bgr24p"
                "\n yuv444p"
                "\n gray8"
                "\n nv12"
                "\n nv21"
                "\n yuv444ple"
                "\n p010(topscodec p010)"
                "\n p010le_ef(topscodec p010le)"
                "\n gray10 "
                "\n",
                argv[0]);
        return -1;
    }

    in_file  = argv[1];
    out_file = argv[2];

    if (argv[3]) {
        card_id = argv[3];
    } else {
        card_id = NULL;
    }

    if (argv[4]) {
        dev_id = argv[4];
    } else {
        dev_id = NULL;
    }

    if (argv[5]) {
        out_fmt = argv[5];
    } else {
        out_fmt = NULL;
    }

    // Get the DEBUG environment variable
    const char* debug_env    = getenv("DEBUG");
    int         log_level    = 0;
    int         ff_log_level = 0;

    printf("infile : %s\n", in_file);
    printf("outfile: %s\n", out_file);
    printf("card_id: %s\n", card_id);
    printf("dev_id : %s\n", dev_id);
    printf("out_fmt: %s\n", out_fmt);

    if (debug_env != NULL) {
        log_level    = atoi(debug_env);
        ff_log_level = log_level == 0 ? AV_LOG_PANIC : AV_LOG_DEBUG;
        fptrLog      = log_callback_null;
        av_log_set_level(ff_log_level);
        av_log_set_callback(fptrLog);
        printf("DEBUG level: %d\n", log_level);
    }

    ret = init_decode(in_file, out_file, dev_id, card_id, out_fmt);
    if (ret < 0) {
        av_log(g_dec_ctx, AV_LOG_INFO, "init decode failed\n");
        return -1;
    }

    ret = decode(g_dec_ctx);
    if (ret < 0) {
        av_log(g_dec_ctx, AV_LOG_INFO, "decode failed\n");
    }

    printf("decode_tops test finish, all frames:%d\n", g_frame_count);
    fclose(fp_yuv);
    avformat_close_input(&g_ifmt_ctx);
    avcodec_free_context(&g_dec_ctx);

    return 0;
}
