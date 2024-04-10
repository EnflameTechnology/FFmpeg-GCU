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
 * @example hw_decode_multi_tops.c
 * This example shows how to do topscodec-HW-accelerated decoding with output
 * frames from the HW video surfaces.
 */

#include <stdio.h>
#include <unistd.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/avassert.h>
#include <libavutil/imgutils.h>
#include <pthread.h>

typedef void (*ffmpeg_log_callback)(void *ptr, int level, const char *fmt, 
                va_list vl);

#define LOG_BUF_PREFIX_SIZE (512)
#define LOG_BUF_SIZE        (1024)
#define MAX_CARD_ID         (4*8)
#define MAX_DEV_ID          (4*8*8)
#define MAX_SESSIONS        (128)
#define DEVICE_NAME        "topscodec"
static char            logBufPrefix[LOG_BUF_PREFIX_SIZE] = {0};
static char            logBuffer[LOG_BUF_SIZE]           = {0};
static pthread_mutex_t cb_av_log_lock;

typedef struct job_args {
    int card_id;
    int dev_id;
    int session_id;
    int out_fmt;
    const char *in_file;
    const char *out_file;
    AVInputFormat *fmt;
} job_args_t;

static int g_card_start = 0;
static int g_card_end   = 1;
static int g_dev_start  = 0;
static int g_dev_end    = 1;
static int g_sessions   = 1;
static int g_dump_out   = 0;

static const char *g_in_file  = NULL;
static const char *g_out_file = NULL;

static void print_globle_var(void) {
    printf("g_card_start:%d\n", g_card_start);
    printf("g_card_end:%d\n", g_card_end);
    printf("g_dev_start:%d\n", g_dev_start);
    printf("g_dev_end:%d\n", g_dev_end);
    printf("g_sessions:%d\n", g_sessions);
    printf("g_in_file:%s\n", g_in_file);
    printf("g_out_file:%s\n", g_out_file);
    printf("g_dump_out:%d\n", g_dump_out);
}

static AVCodec *create_decoder(enum AVCodecID codec_id) {
    AVCodec *decoder = NULL;
    switch(codec_id) {
    case AV_CODEC_ID_H264:
        decoder = avcodec_find_decoder_by_name("h264_topscodec");     break;
    case AV_CODEC_ID_HEVC:
        decoder = avcodec_find_decoder_by_name("hevc_topscodec");     break;
    case AV_CODEC_ID_VP8:
        decoder = avcodec_find_decoder_by_name("vp8_topscodec");      break;
    case AV_CODEC_ID_VP9:
        decoder = avcodec_find_decoder_by_name("vp9_topscodec");      break;
    case AV_CODEC_ID_MJPEG:
        decoder = avcodec_find_decoder_by_name("mjpeg_topscodec");    break;
    case AV_CODEC_ID_H263:
        decoder = avcodec_find_decoder_by_name("h263_topscodec");     break;
    case AV_CODEC_ID_MPEG2VIDEO:
        decoder = avcodec_find_decoder_by_name("mpeg2_topscodec");    break;
    case AV_CODEC_ID_MPEG4:
        decoder = avcodec_find_decoder_by_name("mpeg4_topscodec");    break;
    case AV_CODEC_ID_VC1:
        decoder = avcodec_find_decoder_by_name("vc1_topscodec");      break;
    case AV_CODEC_ID_CAVS:
        decoder = avcodec_find_decoder_by_name("avs_topscodec");      break;
    case AV_CODEC_ID_AVS2:
        decoder = avcodec_find_decoder_by_name("avs2_topscodec");     break;
    case AV_CODEC_ID_AV1:
        decoder = avcodec_find_decoder_by_name("av1_topscodec");      break;
    default:
        decoder = avcodec_find_decoder(codec_id);                     break;
    }

    return decoder;
}

static int hw_decoder_init(
    AVBufferRef *hw_device_ctx, AVCodecContext *ctx, 
    const enum AVHWDeviceType type, const char *dev_id) {
    int ret = 0;

    if ((ret = av_hwdevice_ctx_create(&hw_device_ctx, type,
                                      dev_id, NULL, 0)) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Failed to create specified HW device.\n");
        return ret;
    }
    ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
    return ret;
}

static enum AVPixelFormat get_hw_format(AVCodecContext *ctx,
                                        const enum AVPixelFormat *pix_fmts)
{
    const enum AVPixelFormat *p;

    for (p = pix_fmts; *p != -1; p++) {
        if (*p == AV_PIX_FMT_EFCCODEC)
            return *p;
    }

    av_log(ctx, AV_LOG_ERROR,  "Failed to get HW surface format.\n");
    return AV_PIX_FMT_NONE;
}

static int decode_write(FILE *outfile, AVCodecContext *avctx, AVPacket *packet, 
                            int send_eos, int64_t *count)
{
    AVFrame *frame          = NULL;
    AVFrame *sw_frame       = NULL;
    uint8_t *buffer         = NULL;

    int ret                 = -1;
    int size                = 0;
    int linesizes[4]        = {0};
    ptrdiff_t linesizes1[4] = {0};
    size_t planesizes[4]    = {0};

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
        } else if(ret == AVERROR(EAGAIN)){
            if(send_eos)
                continue;
            else    
                return 0;

        } else if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error while decoding, ret=%d\n", ret);
            goto fail;
        }

        size = av_image_get_buffer_size(frame->format, frame->width,
                                            frame->height, 1);
        
        /*Be sure to obtain w/h/format from the output frame.*/
        sw_frame->width  = frame->width;
        sw_frame->height = frame->height;
        sw_frame->format = frame->format;

        av_log(avctx, AV_LOG_DEBUG, "frame format:%s, w:%d, h:%d\n",
                av_get_pix_fmt_name(frame->format),
                frame->width, frame->height);
        ret =  av_image_fill_linesizes(linesizes, sw_frame->format,
                                        sw_frame->width);
        if (ret < 0){
            av_log(avctx, AV_LOG_ERROR,"av_image_fill_plane_sizes failed.\n");
            goto fail;
        }

        for (int i = 0; i < 4; i++){
            linesizes1[i] = linesizes[i];
            av_log(avctx, AV_LOG_DEBUG, "ptrlinesizes[%d]:%ld\n",
                        i, linesizes1[i]);
        }   
        ret = av_image_fill_plane_sizes(planesizes, sw_frame->format,
                                        sw_frame->height, linesizes1);
        if (ret < 0){
            av_log(avctx, AV_LOG_ERROR,"av_image_fill_plane_sizes failed.\n");
            goto fail;
        }

        av_frame_get_buffer(sw_frame, 0);

        /*
        Copy data from the device-side memory to the host/device memory.
        If you want to copy device-side data to other places, you can use 
        topsMemcpy directly.
        */
        ret = av_hwframe_transfer_data(sw_frame, frame, 0);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR,
                    "Error transferring the data to Host memory\n");
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
        ret = av_image_copy_to_buffer(buffer, size,
                                      (const uint8_t * const *)sw_frame->data,
                                      (const int *)sw_frame->linesize, 
                                      sw_frame->format,
                                      sw_frame->width, sw_frame->height, 1);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Can not copy image to buffer\n");
            goto fail;
        }
        

        if (g_dump_out && outfile) {
            if ((ret = fwrite(buffer, 1, size, outfile)) < 0) {
                av_log(avctx, AV_LOG_ERROR, "Failed to dump raw data.\n");
                goto fail;
            }
        }

        (*count)++;

    fail:
        av_frame_free(&frame);
        av_frame_free(&sw_frame);
        av_freep(&buffer);
        if (ret < 0)
            return ret;
    } //while
    return 0;
}

static void log_callback_null(void *ptr, int level, const char *fmt, 
                                va_list vl) {
    pthread_mutex_lock(&cb_av_log_lock);
    snprintf(logBufPrefix, LOG_BUF_PREFIX_SIZE, "%s", fmt);
    vsnprintf(logBuffer, LOG_BUF_SIZE, logBufPrefix, vl);
    printf("%s", logBuffer);
    pthread_mutex_unlock(&cb_av_log_lock);
}

static void *job_thread(void *arg) {
    int              ret       = 0;
    AVFormatContext *input_ctx = NULL;
    AVInputFormat   *fmt       = NULL;
    AVStream        *video     = NULL;
    AVCodecContext  *avctx     = NULL;
    AVCodec         *decoder   = NULL;
    AVDictionary    *dec_opts  = NULL;
    AVBufferRef *hw_device_ctx = NULL;

    FILE       *output_file    = NULL;
    int64_t     count          = 0;

    int        video_stream    = 0;
    
    const char *dev_type       = NULL;
    const char *tmp_name       = NULL;
    char tmp[64]               = {0};

    AVPacket packet;
    enum AVHWDeviceType type;
    
    job_args_t *job = (job_args_t *)arg;

    dev_type = DEVICE_NAME;
    
    type = av_hwdevice_find_type_by_name(dev_type);
    if (type == AV_HWDEVICE_TYPE_NONE) {
        fprintf(stderr, "Device type %s is not supported.\n", dev_type);
        fprintf(stderr, "Available device types:");
        while((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE)
            fprintf(stderr, " %s", av_hwdevice_get_type_name(type));
        fprintf(stderr, "\n");
        return NULL;
    }

    tmp_name = &job->in_file[strlen(job->in_file)-4];
    if (!strcmp(tmp_name, "cavs") || !strcmp(tmp_name, ".avs")) {
        fmt = av_find_input_format("cavsvideo");
    } else {
        fmt = NULL;
    }

    if (avformat_open_input(&input_ctx, job->in_file, fmt, NULL) != 0) {
        fprintf(stderr, "Cannot open input file '%s'\n", job->in_file);
        return NULL;
    }

    if (avformat_find_stream_info(input_ctx, NULL) < 0) {
        fprintf(stderr, "Cannot find input stream information.\n");
        return NULL;
    }

    for (size_t i = 0; i < input_ctx->nb_streams; i++) {
        if (input_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video = input_ctx->streams[i];
            video_stream = i;
            break;
        }
    }

    if (NULL == video) {
        fprintf(stderr, "video stream is NULL\n");
        return NULL;
    }

    decoder = create_decoder(video->codecpar->codec_id);

    if(decoder == NULL) {
        fprintf(stderr, "Unsupported codec! \n");
        return NULL;
    }

    if (!(avctx = avcodec_alloc_context3(decoder)))
        return NULL;

    if (avcodec_parameters_to_context(avctx, video->codecpar) < 0)
        return NULL;

    avctx->get_format  = get_hw_format;

    memset(tmp, 0, sizeof(tmp));
    snprintf(tmp, sizeof(tmp), "%d", job->card_id);
    if (hw_decoder_init(hw_device_ctx, avctx, type, tmp) < 0)
        return NULL;

    memset(tmp, 0, sizeof(tmp));
    snprintf(tmp, sizeof(tmp), "%d", job->card_id);
    av_dict_set(&dec_opts, "card_id", tmp, 0);

    memset(tmp, 0, sizeof(tmp));
    snprintf(tmp, sizeof(tmp), "%d", job->dev_id);
    av_dict_set(&dec_opts, "device_id", tmp, 0);

    if ((ret = avcodec_open2(avctx, decoder, &dec_opts)) < 0) {
        fprintf(stderr, "Failed to open codec for stream #%d\n", video_stream);
        return NULL;
    }
    av_dict_free(&dec_opts);

    if (!avctx->hw_frames_ctx){
        av_log(avctx, AV_LOG_ERROR, "avctx hw_frames_ctx is NULL.\n");
        ret = AVERROR(ENOMEM);
        return NULL;
    }

    /* open the file to dump raw data */
    if (g_dump_out == 0) {
        output_file = fopen(job->out_file, "w+");
        if (!output_file) {
            fprintf(stderr, "Could not open destination file %s\n", job->out_file);
            return NULL;
        }
    }

    while (ret >= 0) {
        if ((ret = av_read_frame(input_ctx, &packet)) < 0)
            break;

        if (video_stream != packet.stream_index) {
            continue;
        }
        ret = decode_write(output_file, avctx, &packet, 0, &count);
        if (ret)
            break;

        av_packet_unref(&packet);
    }

    /* flush the decoder */
    av_log(avctx, AV_LOG_INFO, "flush video-->\n");
    packet.data = NULL;
    packet.size = 0;
    ret = decode_write(output_file, avctx, &packet, 1, &count);
    av_packet_unref(&packet);

    if (g_dump_out && output_file) {
        fclose(output_file);
    }
    av_log(avctx, AV_LOG_INFO, "decode_EFC test finish, frames:%ld\n", count);
    avcodec_free_context(&avctx);
    avformat_close_input(&input_ctx);
    av_buffer_unref(&hw_device_ctx);

    return NULL;
}


static int parse_opt(int argc, char **argv) {
  int result;

  while ((result = getopt(argc, argv, "c:n:d:m:s:i:o:y:")) != -1) {
    switch (result) {
      case 'c':
        printf("option=h, optopt=%c, optarg=%s\n", optopt, optarg);
        g_card_start = atoi(optarg);
        printf("g_card_start:%d\n", g_card_start);
        break;
      case 'n':
        printf("option=n, optopt=%c, optarg=%s\n", optopt, optarg);
        g_card_end = atoi(optarg);
        printf("g_card_end:%d\n", g_card_end);
        break;
      case 'd':
        printf("option=i, optopt=%c, optarg=%s\n", optopt, optarg);
        g_dev_start = atoi(optarg);
        printf("g_dev_start:%d\n", g_dev_start);
        break;
      case 'm':
        printf("option=w, optopt=%c, optarg=%s\n", optopt, optarg);
        g_dev_end = atoi(optarg);
        printf("g_dev_end:%d\n", g_dev_end);
        break;
        case 's':
        printf("option=w, optopt=%c, optarg=%s\n", optopt, optarg);
        g_sessions = atoi(optarg);
        printf("g_sessions:%d\n", g_sessions);
        break;
      case 'y':
        printf("option=y, optopt=%c, optarg=%s\n", optopt, optarg);
        g_dump_out = atoi(optarg);
        printf("g_dump_out:%d\n", g_dump_out);
        break;
      case 'i':
        printf("option=h, optopt=%c, optarg=%s\n", optopt, optarg);
        g_in_file = optarg;
        printf("g_in_file:%s\n", g_in_file);
        break;
      case 'o':
        printf("option=o, optopt=%c, optarg=%s\n", optopt, optarg);
        g_out_file = optarg;
        printf("g_out_file:%s\n", g_out_file);
        break;
      case '?':
        printf("result=?, optopt=%c, optarg=%s\n", optopt, optarg);
        printf("unknown option:%c\n", optopt);
        break;
      default:
        printf("default, result=%c\n", result);
        break;
    }
    printf("argv[%d]=%s\n", optind, argv[optind]);
  }//while
  return 0;
}

int main(int argc, char *argv[])
{
    int ret = 0;
    ffmpeg_log_callback fptrLog;
    job_args_t *jobs[MAX_CARD_ID][MAX_DEV_ID][MAX_SESSIONS]   = {0};
    pthread_t *threads[MAX_CARD_ID][MAX_DEV_ID][MAX_SESSIONS] = {0};
    char name[1024] = {0};

    parse_opt(argc, argv);
    if (g_in_file == NULL || g_out_file == NULL) {
        printf("Usage: %s [-c start_card_id] [-n end_card_id] [-d start_dev_id] [-m end_dev_id] [-s sessions] [-y write_out_file] -i <input file> -o <output file>\n", argv[0]);
        printf("Example: %s -c 0 -n 4 -d 0 -m 8 -s 32 -y 0 -i input.h264 -o output.yuv\n", argv[0]);
        return -1;
    }
    print_globle_var();

    if (g_card_start < 0 || g_card_start > MAX_CARD_ID) {
        fprintf(stderr, "g_card_start is invalid\n");
        return -1;
    }

    if (g_card_end < 0 || g_card_end > MAX_CARD_ID) {
        fprintf(stderr, "g_card_end is invalid\n");
        return -1;
    }

    if (g_dev_start < 0 || g_dev_start > MAX_DEV_ID) {
        fprintf(stderr, "g_dev_start is invalid\n");
        return -1;
    }

    if (g_dev_end < 0 || g_dev_end > MAX_DEV_ID) {
        fprintf(stderr, "g_dev_end is invalid\n");
        return -1;
    }

    if (g_sessions < 0 || g_sessions > MAX_SESSIONS) {
        fprintf(stderr, "g_sessions is invalid\n");
        return -1;
    }

    //checkout if the input file is exist
    if (access(g_in_file, F_OK) != 0) {
        fprintf(stderr, "input file %s is not exist\n", g_in_file);
        return -1;
    }

    fptrLog = log_callback_null;
    av_log_set_level(AV_LOG_DEBUG);
    av_log_set_callback(fptrLog);

    for (int i = g_card_start; i < g_card_end; i++) {
        for (int j = g_dev_start; j < g_dev_end; j++) {
            for (int k = 0; k < g_sessions; k++) {
                jobs[i][j][k] = (job_args_t *)malloc(sizeof(job_args_t));
                jobs[i][j][k]->card_id = i;
                jobs[i][j][k]->dev_id = j;
                jobs[i][j][k]->session_id = k;
                jobs[i][j][k]->in_file = g_in_file;
                jobs[i][j][k]->out_file = g_out_file;
                jobs[i][j][k]->fmt = NULL;
                threads[i][j][k] = (pthread_t *)malloc(sizeof(pthread_t));
                memset(name, 0, sizeof(name));
                snprintf(name, sizeof(name), "%s-card%d_dev%d_session%d.bin", g_out_file, i, j, k);
                //rename outfile's name
                jobs[i][j][k]->out_file = name;
                ret = pthread_create(threads[i][j][k], NULL, job_thread, jobs[i][j][k]);
                if (ret != 0) {
                    fprintf(stderr, "pthread_create failed, ret=%d\n", ret);
                    return -1;
                }
                memset(name, 0, sizeof(name));
                snprintf(name, sizeof(name), "card%d_dev%d_session%d", i, j, k);
                // pthread_setname_np(*threads[i][j][k], name);
                av_log(NULL, AV_LOG_INFO, "create thread %s success.\n", name);
            }
        }
    }

    av_log(NULL, AV_LOG_INFO, "main thread wait for all threads to finish\n");
    for (int i = g_card_start; i < g_card_end; i++) {
        for (int j = g_dev_start; j < g_dev_end; j++) {
            for (int k = 0; k < g_sessions; k++) {
                pthread_join(*threads[i][j][k], NULL);
                if (jobs[i][j][k]) free(jobs[i][j][k]);
                if (threads[i][j][k]) free(threads[i][j][k]);
            }
        }
    }
    av_log(NULL, AV_LOG_INFO, "main thread finish\n");
    return 0;
}
