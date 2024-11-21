/*
 * TOPSCODEC DEC Video Acceleration API (video transcoding) transcode sample
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
 * ENFLAME TOPSCODEC-HW-Accelerated decoding example.
 *
 * @example hw_decode_tops.c
 * This example shows how to do topscodec-HW-accelerated decoding with output
 * frames from the HW video surfaces.
 */

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avassert.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <pthread.h>
#include <stdio.h>

typedef void (*ffmpeg_log_callback)(void* ptr, int level, const char* fmt, va_list vl);

#define LOG_BUF_PREFIX_SIZE 512
#define LOG_BUF_SIZE 1024
static char               logBufPrefix[LOG_BUF_PREFIX_SIZE] = {0};
static char               logBuffer[LOG_BUF_SIZE]           = {0};
static AVBufferRef*       hw_device_ctx                     = NULL;
static FILE*              output_file                       = NULL;
static int                copy_data_2_device                = 0;
static int                count                             = 0;
static enum AVPixelFormat hw_pix_fmt;
static pthread_mutex_t    cb_av_log_lock;

// static int hw_decoder_init(AVCodecContext* ctx, const enum AVHWDeviceType type,
//                            const char* dev_id) {
//     int ret = 0;

//     if ((ret = av_hwdevice_ctx_create(&hw_device_ctx, type, dev_id, NULL, 0)) <
//         0) {
//         av_log(ctx, AV_LOG_ERROR, "Failed to create specified HW device.\n");
//         return ret;
//     }
//     ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
//     return ret;
// }

// (58, 134, 100) n4.4
#if AV_VERSION_INT(LIBAVCODEC_VERSION_MAJOR, LIBAVCODEC_VERSION_MINOR, LIBAVCODEC_VERSION_MICRO) < \
    AV_VERSION_INT(58, 134, 100)
static enum AVPixelFormat get_hw_format(AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts) {
    return AV_PIX_FMT_TOPSCODEC;
}
#else
static enum AVPixelFormat get_hw_format(AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts) {
    const enum AVPixelFormat* p;

    for (p = pix_fmts; *p != -1; p++) {
        if (*p == hw_pix_fmt) return *p;
    }

    av_log(ctx, AV_LOG_ERROR, "Failed to get HW surface format.\n");
    return AV_PIX_FMT_NONE;
}
#endif
static int decode_write(AVCodecContext* avctx, AVPacket* packet, int send_eos) {
    AVFrame* frame    = NULL;
    AVFrame* sw_frame = NULL;
    uint8_t* buffer   = NULL;

    int       ret           = -1;
    int       size          = 0;
    int       linesizes[4]  = {0};
    ptrdiff_t linesizes1[4] = {0};
    size_t    planesizes[4] = {0};

    ret = avcodec_send_packet(avctx, packet);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "Error during decoding.\n");
        return ret;
    }

    while (1) {
        if (!(frame = av_frame_alloc()) || !(sw_frame = av_frame_alloc())) {
            av_log(avctx, AV_LOG_ERROR, "Can not alloc frame.\n");
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        ret = avcodec_receive_frame(avctx, frame);
        if (ret == AVERROR_EOF) {
            av_frame_free(&frame);
            av_frame_free(&sw_frame);
            return 0;
        } else if (ret == AVERROR(EAGAIN)) {
            if (send_eos)
                continue;
            else
                return 0;
        } else if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error while decoding, ret=%d\n", ret);
            goto fail;
        }

        size = av_image_get_buffer_size(frame->format, frame->width, frame->height, 1);

        /*Be sure to obtain w/h/format from the output frame.*/
        sw_frame->width  = frame->width;
        sw_frame->height = frame->height;
        sw_frame->format = frame->format;

        av_log(avctx, AV_LOG_DEBUG, "frame format:%s, w:%d, h:%d\n", av_get_pix_fmt_name(frame->format), frame->width,
               frame->height);
        ret = av_image_fill_linesizes(linesizes, sw_frame->format, sw_frame->width);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "av_image_fill_plane_sizes failed.\n");
            goto fail;
        }

        for (int i = 0; i < 4; i++) {
            linesizes1[i] = linesizes[i];
            av_log(avctx, AV_LOG_DEBUG, "ptrlinesizes[%d]:%ld\n", i, linesizes1[i]);
        }
        ret = av_image_fill_plane_sizes(planesizes, sw_frame->format, sw_frame->height, linesizes1);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "av_image_fill_plane_sizes failed.\n");
            goto fail;
        }

        if (copy_data_2_device) {  // NOT supported yet
            /* Allocate device-side memory for the sw_frame, 0 is flag.
            ffmpeg will allocate device-side memory to the user based on
            the sw_frame's width/height and format from the device
            memory pool. If you don't like this way, you can allocate
            device-side memory through topsMalloc
            */
            av_hwframe_get_buffer(avctx->hw_frames_ctx, sw_frame, 0);
        } else {  // Support
            /*Set the parameters for copying data to the host.*/
            sw_frame->hw_frames_ctx = NULL;
            /* Allocate host-side memory for the sw_frame*/
            /* 0 is align size, actual 32 Byte aligned*/
            av_frame_get_buffer(sw_frame, 0);
        }

        /*
        Copy data from the device-side memory to the host/device memory.
        If you want to copy device-side data to other places, you can use
        topsMemcpy directly.
        */
        ret = av_hwframe_transfer_data(sw_frame, frame, 0);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error transferring the data to Host memory, ret=%d\n", ret);
            goto fail;
        }

        /*Allocate a contiguous period of memory*/
        buffer = av_malloc(size);
        if (!buffer) {
            av_log(avctx, AV_LOG_ERROR, "Can not alloc buffer\n");
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        /*Copies the non-contiguous content of the three channels data */
        /*onto the contiguous buf*/
        /*data is on the host mem*/
        ret =
            av_image_copy_to_buffer(buffer, size, (const uint8_t* const*)sw_frame->data, (const int*)sw_frame->linesize,
                                    sw_frame->format, sw_frame->width, sw_frame->height, 1);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Can not copy image to buffer\n");
            goto fail;
        }

        if ((ret = fwrite(buffer, 1, size, output_file)) < 0) {
            av_log(avctx, AV_LOG_ERROR, "Failed to dump raw data.\n");
            goto fail;
        }

        count++;

    fail:
        av_frame_free(&frame);
        av_frame_free(&sw_frame);
        av_freep(&buffer);
        if (ret < 0) return ret;
    }  // while
    return 0;
}

static void log_callback_null(void* ptr, int level, const char* fmt, va_list vl) {
    pthread_mutex_lock(&cb_av_log_lock);
    snprintf(logBufPrefix, LOG_BUF_PREFIX_SIZE, "%s", fmt);
    vsnprintf(logBuffer, LOG_BUF_SIZE, logBufPrefix, vl);
    printf("%s", logBuffer);
    pthread_mutex_unlock(&cb_av_log_lock);
}

int main(int argc, char* argv[]) {
    AVFormatContext* input_ctx = NULL;
    AVStream*        video     = NULL;
    AVCodecContext*  avctx     = NULL;
    const AVCodec*   decoder   = NULL;
    AVDictionary*    dec_opts  = NULL;

    int            i            = 0;
    int            video_stream = 0;
    int            ret          = 0;
    const char*    dev_type     = NULL;
    const char*    dev_id       = NULL;
    const char*    out_fmt      = NULL;
    const char*    card_id      = NULL;
    const char*    in_file      = NULL;
    const char*    out_file     = NULL;
    const char*    tmp_name     = NULL;
    AVInputFormat* fmt          = NULL;

    AVPacket            packet;
    enum AVHWDeviceType type;
    ffmpeg_log_callback fptrLog;

#if AV_VERSION_INT(LIBAVCODEC_VERSION_MAJOR, LIBAVCODEC_VERSION_MINOR, LIBAVCODEC_VERSION_MICRO) <= \
    AV_VERSION_INT(57, 64, 100)
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

    dev_type = "topscodec";
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

    fptrLog = log_callback_null;
    av_log_set_level(AV_LOG_DEBUG);
    av_log_set_callback(fptrLog);
// (58, 134, 100) n4.4
#if AV_VERSION_INT(LIBAVCODEC_VERSION_MAJOR, LIBAVCODEC_VERSION_MINOR, LIBAVCODEC_VERSION_MICRO) >= \
    AV_VERSION_INT(58, 134, 100)
    type = av_hwdevice_find_type_by_name(dev_type);
    if (type == AV_HWDEVICE_TYPE_NONE) {
        fprintf(stderr, "Device type %s is not supported.\n", dev_type);
        fprintf(stderr, "Available device types:");
        while ((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE)
            fprintf(stderr, " %s", av_hwdevice_get_type_name(type));
        fprintf(stderr, "\n");
        return -1;
    }
#endif
    tmp_name = &in_file[strlen(in_file) - 4];
    if (!strcmp(tmp_name, "cavs") || !strcmp(tmp_name, ".avs")) {
        fmt = av_find_input_format("cavsvideo");
    } else {
        fmt = NULL;
    }

    if (avformat_open_input(&input_ctx, in_file, fmt, NULL) < 0) {
        fprintf(stderr, "Cannot open input file '%s'\n", in_file);
        return -1;
    }

    if (avformat_find_stream_info(input_ctx, NULL) < 0) {
        fprintf(stderr, "Cannot find input stream information.\n");
        return -1;
    }

    for (size_t i = 0; i < input_ctx->nb_streams; i++) {
        if (input_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video        = input_ctx->streams[i];
            video_stream = i;
            break;
        }
    }

    if (NULL == video) {
        fprintf(stderr, "video stream is NULL\n");
        return -1;
    }

    switch (video->codecpar->codec_id) {
        case AV_CODEC_ID_H264:
            decoder = avcodec_find_decoder_by_name("h264_topscodec");
            break;
        case AV_CODEC_ID_HEVC:
            decoder = avcodec_find_decoder_by_name("hevc_topscodec");
            break;
        case AV_CODEC_ID_VP8:
            decoder = avcodec_find_decoder_by_name("vp8_topscodec");
            break;
        case AV_CODEC_ID_VP9:
            decoder = avcodec_find_decoder_by_name("vp9_topscodec");
            break;
        case AV_CODEC_ID_MJPEG:
            decoder = avcodec_find_decoder_by_name("mjpeg_topscodec");
            break;
        case AV_CODEC_ID_H263:
            decoder = avcodec_find_decoder_by_name("h263_topscodec");
            break;
        case AV_CODEC_ID_MPEG2VIDEO:
            decoder = avcodec_find_decoder_by_name("mpeg2_topscodec");
            break;
        case AV_CODEC_ID_MPEG4:
            decoder = avcodec_find_decoder_by_name("mpeg4_topscodec");
            break;
        case AV_CODEC_ID_VC1:
            decoder = avcodec_find_decoder_by_name("vc1_topscodec");
            break;
        case AV_CODEC_ID_CAVS:
            decoder = avcodec_find_decoder_by_name("avs_topscodec");
            break;
// (58, 134, 100) n4.4
#if AV_VERSION_INT(LIBAVCODEC_VERSION_MAJOR, LIBAVCODEC_VERSION_MINOR, LIBAVCODEC_VERSION_MICRO) >= \
    AV_VERSION_INT(58, 134, 100)
        case AV_CODEC_ID_AVS2:
            decoder = avcodec_find_decoder_by_name("avs2_topscodec");
            break;
        case AV_CODEC_ID_AV1:
            decoder = avcodec_find_decoder_by_name("av1_topscodec");
            break;
#endif
        default:
            decoder = avcodec_find_decoder(video->codecpar->codec_id);
            break;
    }

    if (decoder == NULL) {
        fprintf(stderr, "Unsupported codec! \n");
        return -1;
    }
// (58, 134, 100) n4.4
#if AV_VERSION_INT(LIBAVCODEC_VERSION_MAJOR, LIBAVCODEC_VERSION_MINOR, LIBAVCODEC_VERSION_MICRO) >= \
    AV_VERSION_INT(58, 134, 100)
    for (i = 0;; i++) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(decoder, i);
        if (!config) {
            fprintf(stderr, "Decoder %s does not support device type %s.\n", decoder->name,
                    av_hwdevice_get_type_name(type));
            return -1;
        }

        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX && config->device_type == type) {
            hw_pix_fmt = config->pix_fmt;
            break;
        }
    }
#endif

    if (!(avctx = avcodec_alloc_context3(decoder))) return AVERROR(ENOMEM);

    if (avcodec_parameters_to_context(avctx, video->codecpar) < 0) return -1;

    avctx->get_format = get_hw_format;

    // if (hw_decoder_init(avctx, type, dev_id) < 0) return -1;

    if (card_id) av_dict_set(&dec_opts, "card_id", card_id, 0);
    if (dev_id) av_dict_set(&dec_opts, "device_id", dev_id, 0);
    if (out_fmt) /* for color space trans*/
        av_dict_set(&dec_opts, "output_pixfmt", out_fmt, 0);

    if ((ret = avcodec_open2(avctx, decoder, &dec_opts)) < 0) {
        fprintf(stderr, "Failed to open codec for stream #%d\n", video_stream);
        return -1;
    }
    av_dict_free(&dec_opts);

    if (!avctx->hw_frames_ctx) {
        av_log(avctx, AV_LOG_ERROR, "avctx hw_frames_ctx is NULL.\n");
        ret = AVERROR(ENOMEM);
        return -1;
    }

    /* open the file to dump raw data */
    output_file = fopen(out_file, "w+");

    while (ret >= 0) {
        if ((ret = av_read_frame(input_ctx, &packet)) < 0) break;

        if (video_stream != packet.stream_index) {
            continue;
        }
        ret = decode_write(avctx, &packet, 0);
        if (ret) break;

        av_packet_unref(&packet);
    }

    /* flush the decoder */
    av_log(avctx, AV_LOG_INFO, "flush video-->\n");
    packet.data = NULL;
    packet.size = 0;
    ret         = decode_write(avctx, &packet, 1);
    av_packet_unref(&packet);

    if (output_file) {
        fclose(output_file);
    }
    av_log(avctx, AV_LOG_INFO, "decode_EFC test finish, frames:%d\n", count);
    avcodec_free_context(&avctx);
    avformat_close_input(&input_ctx);
    av_buffer_unref(&hw_device_ctx);

    return 0;
}
