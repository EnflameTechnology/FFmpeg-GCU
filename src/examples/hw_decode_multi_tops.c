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

#include <inttypes.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avassert.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/time.h>
#include <libgen.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef void (*ffmpeg_log_callback)(void* ptr, int level, const char* fmt, va_list vl);

#define LOG_BUF_PREFIX_SIZE (512)
#define LOG_BUF_SIZE (1024)
#define MAX_CARD_ID (4 * 8)
#define MAX_DEV_ID (8)
#define MAX_SESSIONS (64)
#define MAX_PATH_LEN (256 * 2)
#define DEVICE_NAME "topscodec"
static char            logBufPrefix[LOG_BUF_PREFIX_SIZE] = {0};
static char            logBuffer[LOG_BUF_SIZE]           = {0};
static pthread_mutex_t cb_av_log_lock;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    int             count;
    int             released;
} pseudo_barrier_t;

static pseudo_barrier_t g_barrier_start;
static pseudo_barrier_t g_barrier_frame;
static pseudo_barrier_t g_barrier_end;

static void pseudo_barrier_init(pseudo_barrier_t* b, int count) {
    pthread_mutex_init(&b->mutex, NULL);
    pthread_cond_init(&b->cond, NULL);
    b->count    = count;
    b->released = 0;
}

static void pseudo_barrier_wait(pseudo_barrier_t* b) {
    pthread_mutex_lock(&b->mutex);
    if (--b->count == 0) {
        b->released = 1;
        pthread_cond_broadcast(&b->cond);
    } else {
        while (!b->released) {
            pthread_cond_wait(&b->cond, &b->mutex);
        }
    }
    pthread_mutex_unlock(&b->mutex);
}

static void pseudo_barrier_destroy(pseudo_barrier_t* b) {
    pthread_mutex_destroy(&b->mutex);
    pthread_cond_destroy(&b->cond);
    b->count    = 0;
    b->released = 0;
}

typedef enum Sync_type { SYNC_START, SYNC_FRAME, SYNC_END } Sync_type;

static const char* Sync_type2str(Sync_type type) {
    switch (type) {
        case SYNC_START:
            return "SYNC_START";
        case SYNC_FRAME:
            return "SYNC_FRAME";
        case SYNC_END:
            return "SYNC_END";
        default:
            return "UNKNOWN";
    }
}

static void synchoronize(Sync_type type) {
    av_log(NULL, AV_LOG_DEBUG, "synchoronize:%s\n", Sync_type2str(type));
    if (type == SYNC_START)
        pseudo_barrier_wait(&g_barrier_start);
    else if (type == SYNC_FRAME)
        pseudo_barrier_wait(&g_barrier_frame);
    else if (type == SYNC_END)
        pseudo_barrier_wait(&g_barrier_end);
}

typedef struct job_args {
    int         card_id;
    int         dev_id;
    int         session_id;
    int         out_fmt;
    int         sf;
    int         in_port_num;
    int         out_port_num;
    float       fps;
    int         frames;
    int         first_read_frames;
    int         before_eos_frames;
    uint64_t    start_time;
    uint64_t    end_time;
    uint64_t    latency;
    char        out_file[MAX_PATH_LEN];
    char        job_name[MAX_PATH_LEN];
    const char* in_file;
} job_args_t;

static int g_input_w      = 0;
static int g_input_h      = 0;
static int g_card_start   = 0;
static int g_card_end     = 1;
static int g_dev_start    = 0;
static int g_dev_end      = 1;
static int g_sessions     = 1;
static int g_dump_out     = 0;
static int g_log_level    = 2;
static int g_kill_flag    = 0;
static int g_frame_sf     = 0;
static int g_in_port_num  = 8;
static int g_out_port_num = 8;
static int g_skip_frames  = 1;
static int g_is_av1       = 0;
static int g_zero_copy    = 1;
static int g_sync         = 1;

static const char* g_in_file  = NULL;
static const char* g_out_file = NULL;

static void print_globle_var(void) {
    printf("g_input_w:%d\n", g_input_w);
    printf("g_input_h:%d\n", g_input_h);
    printf("g_card_start:%d\n", g_card_start);
    printf("g_card_end:%d\n", g_card_end);
    printf("g_dev_start:%d\n", g_dev_start);
    printf("g_dev_end:%d\n", g_dev_end);
    printf("g_sessions:%d\n", g_sessions);
    printf("g_in_file:%s\n", g_in_file);
    printf("g_out_file:%s\n", g_out_file);
    printf("g_dump_out:%d\n", g_dump_out);
    printf("g_log_level:%d\n", g_log_level);
    printf("g_kill_flag:%d\n", g_kill_flag);
    printf("g_frame_sf:%d\n", g_frame_sf);
    printf("g_skip_frames:%d\n", g_skip_frames);
    printf("g_in_port_num:%d\n", g_in_port_num);
    printf("g_out_port_num:%d\n", g_out_port_num);
    printf("g_zero_copy:%d\n", g_zero_copy);
    printf("g_sync:%d\n", g_sync);
}

static int end_with(const char* str, const char* suffix) {
    size_t str_len    = strlen(str);
    size_t suffix_len = strlen(suffix);

    if (str_len < suffix_len) {
        return 0;
    }

    const char* str_suffix = str + (str_len - suffix_len);
    return strcmp(str_suffix, suffix) == 0;
}

#define SUICIDE()                     \
    {                                 \
        pid_t pid = getpid();         \
        printf("kill pid:%d\n", pid); \
        kill(pid, SIGINT);            \
    }

static AVCodec* create_decoder(enum AVCodecID codec_id) {
    const AVCodec* decoder = NULL;
    switch (codec_id) {
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
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 18, 100)
        case AV_CODEC_ID_AVS2:
            decoder = avcodec_find_decoder_by_name("avs2_topscodec");
            break;
        case AV_CODEC_ID_AV1:
            decoder = avcodec_find_decoder_by_name("av1_topscodec");
            break;
#endif
        default:
            decoder = avcodec_find_decoder(codec_id);
            break;
    }

    return decoder;
}

// static int hw_decoder_init(AVBufferRef** hw_device_ctx, AVCodecContext* ctx, const enum AVHWDeviceType type,
//                            const char* card_id) {
//     int ret = 0;

//     if ((ret = av_hwdevice_ctx_create(hw_device_ctx, type, card_id, NULL, 0)) < 0) {
//         av_log(ctx, AV_LOG_ERROR, "Failed to create specified HW device.\n");
//         return ret;
//     }
//     ctx->hw_device_ctx = av_buffer_ref(*hw_device_ctx);
//     return ret;
// }

static enum AVPixelFormat get_hw_format(AVCodecContext* ctx, const enum AVPixelFormat* pix_fmts) {
    const enum AVPixelFormat* p;

    for (p = pix_fmts; *p != -1; p++) {
        if (*p == AV_PIX_FMT_TOPSCODEC) return *p;
    }

    av_log(ctx, AV_LOG_ERROR, "Failed to get HW surface format.\n");
    return AV_PIX_FMT_NONE;
}

static int decode_write(job_args_t* job, FILE* outfile, AVCodecContext* avctx, AVPacket* packet, int send_eos) {
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
        av_log(avctx, AV_LOG_ERROR, "Error during avcodec_send_packet.\n");
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
            av_buffer_unref(&frame->hw_frames_ctx);
            av_frame_free(&frame);
            av_frame_free(&sw_frame);
            return 0;
        } else if (ret == AVERROR(EAGAIN)) {
            av_buffer_unref(&frame->hw_frames_ctx);
            av_frame_free(&frame);
            av_frame_free(&sw_frame);
            if (send_eos) {
                av_usleep(1);
                av_log(avctx, AV_LOG_DEBUG, "EOS EAGAIN\n");
                continue;
            } else {
                av_usleep(1);
                av_log(avctx, AV_LOG_DEBUG, "EAGAIN\n");
                return 0;
            }
        } else if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error while avcodec_receive_frame, ret=%d\n", ret);
            goto fail;
        }

        job->frames++;
        if (job->frames == g_skip_frames) {
            if (g_sync) synchoronize(SYNC_FRAME);
            job->first_read_frames = job->frames;
            job->start_time        = av_gettime();
        }

        if (g_dump_out && outfile) {
            size = av_image_get_buffer_size(frame->format, frame->width, frame->height, 1);

            /*Be sure to obtain w/h/format from the output frame.*/
            sw_frame->width  = frame->width;
            sw_frame->height = frame->height;
            sw_frame->format = frame->format;

            av_log(avctx, AV_LOG_DEBUG, "frame format:%s, w:%d, h:%d\n", av_get_pix_fmt_name(frame->format),
                   frame->width, frame->height);
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

            av_frame_get_buffer(sw_frame, 0);
            /*
            Copy data from the device-side memory to the host/device
            memory. If you want to copy device-side data to other
            places, you can use topsMemcpy directly.
            */
            ret = av_hwframe_transfer_data(sw_frame, frame, 0);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "Error transferring the data to Host memory\n");
                goto fail;
            }

            /*Allocate a contiguous period of memory*/
            buffer = av_malloc(size);
            if (!buffer) {
                av_log(avctx, AV_LOG_ERROR, "Can not alloc buffer\n");
                ret = AVERROR(ENOMEM);
                goto fail;
            }

            /*Copies the non-contiguous content of the three channels
             * data */
            /*onto the contiguous buf*/
            /*data is on the host mem*/
            ret = av_image_copy_to_buffer(buffer, size, (const uint8_t* const*)sw_frame->data,
                                          (const int*)sw_frame->linesize, sw_frame->format, sw_frame->width,
                                          sw_frame->height, 1);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "Can not copy image to buffer\n");
                goto fail;
            }

            if ((ret = fwrite(buffer, 1, size, outfile)) < 0) {
                av_log(avctx, AV_LOG_ERROR, "Failed to dump raw data.\n");
                goto fail;
            }
        }

    fail:
        av_frame_free(&frame);
        av_frame_free(&sw_frame);
        av_freep(&buffer);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "thread:%s fail, ret=%d\n", job->job_name, ret);
            return ret;
        }
        av_log(avctx, AV_LOG_DEBUG, "app capture frame:%d\n", job->frames);
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

static void* job_thread(void* arg) {
    int              ret           = 0;
    AVFormatContext* input_ctx     = NULL;
    AVInputFormat*   fmt           = NULL;
    AVStream*        video         = NULL;
    AVCodecContext*  avctx         = NULL;
    AVCodec*         decoder       = NULL;
    AVDictionary*    dec_opts      = NULL;
    AVBufferRef*     hw_device_ctx = NULL;

    FILE*    output_file = NULL;
    int64_t  count       = 0;
    uint64_t start_time  = 0;
    uint64_t end_time    = 0;
    uint64_t elapsed     = 0;

    int video_stream = 0;
    int skip         = 0;

    const char* dev_type = NULL;
    const char* tmp_name = NULL;
    char        tmp[64]  = {0};

    AVPacket            packet;
    enum AVHWDeviceType type;

    job_args_t* job = (job_args_t*)arg;

    dev_type = DEVICE_NAME;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 18, 100)
    type = av_hwdevice_find_type_by_name(dev_type);
    if (type == AV_HWDEVICE_TYPE_NONE) {
        fprintf(stderr, "Device type %s is not supported.\n", dev_type);
        fprintf(stderr, "Available device types:");
        while ((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE)
            fprintf(stderr, " %s", av_hwdevice_get_type_name(type));
        fprintf(stderr, "\n");
        return NULL;
    }
#endif
    tmp_name = &job->in_file[strlen(job->in_file) - 4];
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
            video        = input_ctx->streams[i];
            video_stream = i;
            break;
        }
    }

    if (NULL == video) {
        fprintf(stderr, "video stream is NULL\n");
        return NULL;
    }

    decoder = create_decoder(video->codecpar->codec_id);

    if (decoder == NULL) {
        fprintf(stderr, "Unsupported codec! \n");
        return NULL;
    }

    if (!(avctx = avcodec_alloc_context3(decoder))) return NULL;

    if (avcodec_parameters_to_context(avctx, video->codecpar) < 0) return NULL;

    avctx->get_format = get_hw_format;

    memset(tmp, 0, sizeof(tmp));
    snprintf(tmp, sizeof(tmp), "%d", job->card_id);
    // if (hw_decoder_init(&hw_device_ctx, avctx, type, tmp) < 0) return NULL;

    memset(tmp, 0, sizeof(tmp));
    snprintf(tmp, sizeof(tmp), "%d", job->card_id);
    av_dict_set(&dec_opts, "card_id", tmp, 0);

    memset(tmp, 0, sizeof(tmp));
    snprintf(tmp, sizeof(tmp), "%d", job->dev_id);
    av_dict_set(&dec_opts, "device_id", tmp, 0);

    memset(tmp, 0, sizeof(tmp));
    snprintf(tmp, sizeof(tmp), "%d", job->sf);
    av_dict_set(&dec_opts, "sf", tmp, 0);

    memset(tmp, 0, sizeof(tmp));
    snprintf(tmp, sizeof(tmp), "%d", job->in_port_num);
    av_dict_set(&dec_opts, "in_port_num", tmp, 0);

    memset(tmp, 0, sizeof(tmp));
    snprintf(tmp, sizeof(tmp), "%d", job->out_port_num);
    av_dict_set(&dec_opts, "out_port_num", tmp, 0);

    memset(tmp, 0, sizeof(tmp));
    snprintf(tmp, sizeof(tmp), "%d", g_zero_copy);
    av_dict_set(&dec_opts, "zero_copy", tmp, 0);

    // case some video format can't detect w/h by avformat_find_stream_info
    // so we need to set the video w/h by user
    // expecially for the avs2
    if (g_input_h > 0 && g_input_w > 0) {
        memset(tmp, 0, sizeof(tmp));
        snprintf(tmp, sizeof(tmp), "%d", g_input_w);
        av_dict_set(&dec_opts, "in_w", tmp, 0);

        memset(tmp, 0, sizeof(tmp));
        snprintf(tmp, sizeof(tmp), "%d", g_input_h);
        av_dict_set(&dec_opts, "in_h", tmp, 0);
        av_log(avctx, AV_LOG_DEBUG, "set in w/h:%d/%d\n", g_input_w, g_input_h);
    }

    if ((ret = avcodec_open2(avctx, decoder, &dec_opts)) < 0) {
        fprintf(stderr, "Failed to open codec for stream #%d\n", video_stream);
        return NULL;
    }
    av_dict_free(&dec_opts);

    if (!avctx->hw_frames_ctx) {
        av_log(avctx, AV_LOG_ERROR, "avctx hw_frames_ctx is NULL.\n");
        ret = AVERROR(ENOMEM);
        return NULL;
    }

    /* open the file to dump raw data */
    if (g_dump_out == 1) {
        output_file = fopen(job->out_file, "w+");
        if (!output_file) {
            fprintf(stderr, "Could not open destination file %s\n", job->out_file);
            return NULL;
        }
        av_log(avctx, AV_LOG_DEBUG, "open output file %s\n", job->out_file);
    }
    // sychoronize sessions
    if (g_sync) synchoronize(SYNC_START);

    job->frames            = 0;
    job->first_read_frames = 0;
    job->start_time        = 0;
    start_time             = av_gettime();
    while (ret >= 0) {
        if ((ret = av_read_frame(input_ctx, &packet)) < 0) break;

        if (video_stream != packet.stream_index) {
            continue;
        }
        ret = decode_write(job, output_file, avctx, &packet, 0);
        if (ret) break;

        av_packet_unref(&packet);
    }
    job->end_time          = av_gettime();
    job->before_eos_frames = job->frames;
    /* flush the decoder */
    av_log(avctx, AV_LOG_DEBUG, "flush video-->\n");
    packet.data = NULL;
    packet.size = 0;
    ret         = decode_write(job, output_file, avctx, &packet, 1);
    if (ret < 0) {
        fprintf(stderr, "Error while flushing the decoder\n");
    }
    av_packet_unref(&packet);
    end_time = av_gettime();

    if (job->start_time == 0) {
        job->start_time = start_time;
    }

    if (job->before_eos_frames == 0) {
        job->before_eos_frames = job->frames;
        job->end_time          = end_time;
    }
    count = job->before_eos_frames;

    if (count > 0 && (end_time - job->start_time) > 0) {
        skip         = job->first_read_frames;
        elapsed      = job->end_time - job->start_time;
        job->fps     = (1000000.f * (count - skip)) / elapsed;
        job->latency = job->start_time - start_time;
    }

    if (g_dump_out && output_file) {
        fclose(output_file);
    }
    if (g_kill_flag) {
        SUICIDE()
    }
    if (g_sync) synchoronize(SYNC_END);
    av_log(avctx, AV_LOG_INFO, "decode finish, frames:%d\n", job->frames);

    // av_buffer_unref(&hw_device_ctx);
    avcodec_free_context(&avctx);
    avformat_close_input(&input_ctx);

    return NULL;
}

static enum AVCodecID find_codec_id(const char* file) {
    enum AVCodecID   ret       = AV_CODEC_ID_NONE;
    AVFormatContext* input_ctx = NULL;
    AVStream*        video     = NULL;

    if (avformat_open_input(&input_ctx, file, NULL, NULL) != 0) {
        fprintf(stderr, "Cannot open input file '%s'\n", file);
        return ret;
    }

    if (avformat_find_stream_info(input_ctx, NULL) < 0) {
        fprintf(stderr, "Cannot find input stream information.\n");
        return ret;
    }

    for (size_t i = 0; i < input_ctx->nb_streams; i++) {
        if (input_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video = input_ctx->streams[i];
            break;
        }
    }

    if (NULL == video) {
        fprintf(stderr, "video stream is NULL\n");
        return ret;
    }

    ret = video->codecpar->codec_id;
    avformat_close_input(&input_ctx);
    return ret;
}

static int cal_card_dev_session() {
    int total = 0;
    for (int i = g_card_start; i < g_card_end; i++) {
        for (int j = g_dev_start; j < g_dev_end; j++) {
            if (g_is_av1) {
                if (j % 2 == 0) {
                    av_log(NULL, AV_LOG_INFO, "skip dev_id:%d\n", j);
                    continue;
                }
            }
            for (int k = 0; k < g_sessions; k++) {
                total++;
            }
        }
    }
    return total;
}

static int parse_opt(int argc, char** argv) {
    int result;

    while ((result = getopt(argc, argv, "a:e:c:n:d:m:s:i:o:y:l:k:f:b:p:z:w:h:")) != -1) {
        switch (result) {
            case 'a':
                printf("option=h, optopt=%c, optarg=%s\n", optopt, optarg);
                g_zero_copy = atoi(optarg);
                printf("g_zero_copy:%d\n", g_card_start);
                break;
            case 'e':
                printf("option=h, optopt=%c, optarg=%s\n", optopt, optarg);
                g_sync = atoi(optarg);
                printf("g_sync:%d\n", g_card_start);
                break;
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
            case 'l':
                printf("option=y, optopt=%c, optarg=%s\n", optopt, optarg);
                g_log_level = atoi(optarg);
                printf("g_log_level:%d\n", g_log_level);
                break;
            case 'k':
                printf("option=y, optopt=%c, optarg=%s\n", optopt, optarg);
                g_kill_flag = atoi(optarg);
                printf("g_kill_flag:%d\n", g_kill_flag);
                break;
            case 'f':
                printf("option=y, optopt=%c, optarg=%s\n", optopt, optarg);
                g_frame_sf = atoi(optarg);
                printf("g_frame_sf:%d\n", g_frame_sf);
                break;
            case 'b':
                printf("option=y, optopt=%c, optarg=%s\n", optopt, optarg);
                g_in_port_num = atoi(optarg);
                printf("g_in_port_num:%d\n", g_in_port_num);
                break;
            case 'p':
                printf("option=y, optopt=%c, optarg=%s\n", optopt, optarg);
                g_out_port_num = atoi(optarg);
                printf("g_out_port_num:%d\n", g_out_port_num);
                break;
            case 'z':
                printf("option=y, optopt=%c, optarg=%s\n", optopt, optarg);
                g_skip_frames = atoi(optarg);
                printf("g_skip_frames:%d\n", g_skip_frames);
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
            case 'w':
                printf("option=h, optopt=%c, optarg=%s\n", optopt, optarg);
                g_input_w = atoi(optarg);
                printf("g_input_w:%d\n", g_card_start);
                break;
            case 'h':
                printf("option=h, optopt=%c, optarg=%s\n", optopt, optarg);
                g_input_h = atoi(optarg);
                printf("g_input_h:%d\n", g_card_start);
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
    }  // while
    return 0;
}

int main(int argc, char* argv[]) {
    int            ret      = 0;
    enum AVCodecID codec_id = AV_CODEC_ID_NONE;

    ffmpeg_log_callback fptrLog;
    char                name[MAX_PATH_LEN]                             = {0};
    job_args_t*         jobs[MAX_CARD_ID][MAX_DEV_ID][MAX_SESSIONS]    = {0};
    pthread_t*          threads[MAX_CARD_ID][MAX_DEV_ID][MAX_SESSIONS] = {0};

    char *path, *file;
    char  g_out_file_copy1[MAX_PATH_LEN];
    char  g_out_file_copy2[MAX_PATH_LEN];
    char  env_str[MAX_PATH_LEN];

    uint64_t sum_frames         = 0;
    uint64_t sum_skip_frames    = 0;
    uint64_t mean_skip_frames   = 0;
    uint64_t sum_latency        = 0;
    uint64_t mean_latency       = 0;
    float    sum_fps            = 0.0;
    float    mean_fps           = 0.0;
    float    max_fps            = 0.0;
    float    min_fps            = 0.0;
    float    diff               = 0.0;
    float    variance           = 0.0;
    float    standard_deviation = 0.0;

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 18, 100)
    /* register all formats and codecs */
    av_register_all();
#endif

    parse_opt(argc, argv);
    if (g_in_file == NULL || g_out_file == NULL) {
        printf(
            "Usage: %s [-a zero_copy 1/0] "
            "[-e sync 1/0] "
            "[-k kill_self 0/1] "
            "[-l loglevel0/1/2] "
            "[-f switch_frame] "
            "[-z skip_frames] "
            "[-b in_port_num] "
            "[-p out_port_num] "
            "[-c start_card_id] "
            "[-n end_card_id] "
            "[-d start_dev_id] "
            "[-m end_dev_id] "
            "[-s sessions] "
            "[-b input_buf_num] "
            "[-p out_buf_num] "
            "[-w width] "
            "[-h height] "
            "[-y write_out_file 0/1] "
            "-i <input file> -o <output file>\n",
            argv[0]);
        printf(
            "Example: %s -k 0 -l 2 -c 0 -n 4 -d 0 -m 8 -s 32 -y 0 "
            "-i input.h264 -o output.yuv\n",
            argv[0]);
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

    // checkout if the input file is exist
    if (access(g_in_file, F_OK) != 0) {
        fprintf(stderr, "input file %s is not exist\n", g_in_file);
        return -1;
    }

    codec_id = find_codec_id(g_in_file);

    if (codec_id == AV_CODEC_ID_NONE) {
        fprintf(stderr, "unknow codec id !!!\n");
        return -1;
    }

    g_is_av1 = 0;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(58, 18, 100)
    // if (end_with(g_in_file, ".av1") || end_with(g_in_file, ".AV1")) {
    if (codec_id == AV_CODEC_ID_AV1) {
        g_is_av1 = 1;
        printf("file[%s] end with AV1\n", g_in_file);
    }
#endif
    pthread_mutex_init(&cb_av_log_lock, NULL);

    fptrLog = log_callback_null;
    if (g_log_level) {
        av_log_set_level(AV_LOG_DEBUG);
        av_log_set_callback(fptrLog);
    }

    // Set environment variable
    memset(env_str, 0, sizeof(env_str));
    snprintf(env_str, sizeof(env_str), "TOPS_VISIBLE_DEVICE=%d", g_card_start);
    for (int i = g_card_start + 1; i < g_card_end; i++) {
        snprintf(env_str + strlen(env_str), sizeof(env_str), ",%d", i);
    }
    printf("env_str:%s\n", env_str);

    if (setenv("TOPS_VISIBLE_DEVICE", env_str, 1) != 0) {
        fprintf(stderr, "Failed to set TOPS_VISIBLE_DEVICE\n");
        return -1;
    }
    printf("TOPS_VISIBLE_DEVICE:%s\n", getenv("TOPS_VISIBLE_DEVICE"));
    int all_session = cal_card_dev_session();
    pseudo_barrier_init(&g_barrier_start, all_session);
    pseudo_barrier_init(&g_barrier_end, all_session);
    pseudo_barrier_init(&g_barrier_frame, all_session);
    for (int i = g_card_start; i < g_card_end; i++) {
        for (int j = g_dev_start; j < g_dev_end; j++) {
            if (g_is_av1) {
                if (j % 2 == 0) {
                    av_log(NULL, AV_LOG_INFO, "skip dev_id:%d\n", j);
                    continue;
                }
            }
            for (int k = 0; k < g_sessions; k++) {
                jobs[i][j][k]               = (job_args_t*)malloc(sizeof(job_args_t));
                jobs[i][j][k]->card_id      = i;
                jobs[i][j][k]->dev_id       = j;
                jobs[i][j][k]->session_id   = k;
                jobs[i][j][k]->in_file      = g_in_file;
                jobs[i][j][k]->sf           = g_frame_sf;
                jobs[i][j][k]->in_port_num  = g_in_port_num;
                jobs[i][j][k]->out_port_num = g_out_port_num;
                threads[i][j][k]            = (pthread_t*)malloc(sizeof(pthread_t));
                memset(name, 0, sizeof(name));
                memset(g_out_file_copy1, 0, sizeof(g_out_file_copy1));
                memset(g_out_file_copy2, 0, sizeof(g_out_file_copy2));
                strncpy(g_out_file_copy1, g_out_file, sizeof(g_out_file_copy1));
                path = dirname(g_out_file_copy1);
                strncpy(g_out_file_copy2, g_out_file, sizeof(g_out_file_copy2));
                file = basename(g_out_file_copy2);
                snprintf(name, sizeof(name), "%s/card%d_dev%d_session%d_%s", path, i, j, k, file);
                // rename outfile's name
                memset(jobs[i][j][k]->out_file, 0, sizeof(jobs[i][j][k]->out_file));
                memcpy(jobs[i][j][k]->out_file, name, strlen(name));
                av_log(NULL, AV_LOG_INFO, "out file name: %s\n", name);
                ret = pthread_create(threads[i][j][k], NULL, job_thread, jobs[i][j][k]);
                if (ret != 0) {
                    fprintf(stderr, "pthread_create failed, ret=%d\n", ret);
                    return -1;
                }
                memset(name, 0, sizeof(name));
                snprintf(name, sizeof(name), "card%d_dev%d_session%d", i, j, k);
                // pthread_setname_np(*threads[i][j][k], name);
                memset(jobs[i][j][k]->job_name, 0, sizeof(jobs[i][j][k]->job_name));
                memcpy(jobs[i][j][k]->job_name, name, strlen(name));
                av_log(NULL, AV_LOG_INFO, "create thread %s success.\n", name);
            }
        }
    }

    av_log(NULL, AV_LOG_INFO, "main thread wait for all threads to finish\n");
    for (int i = g_card_start; i < g_card_end; i++) {
        for (int j = g_dev_start; j < g_dev_end; j++) {
            if (g_is_av1) {
                if (j % 2 == 0) {
                    continue;
                }
            }
            for (int k = 0; k < g_sessions; k++) {
                pthread_join(*threads[i][j][k], NULL);
                if (threads[i][j][k]) free(threads[i][j][k]);
                av_log(NULL, AV_LOG_INFO, "thread join [%s] success\n", jobs[i][j][k]->job_name);
            }
        }
    }

    /*print result msg*/
    for (int i = g_card_start; i < g_card_end; i++) {
        for (int j = g_dev_start; j < g_dev_end; j++) {
            if (g_is_av1) {
                if (j % 2 == 0) {
                    continue;
                }
            }
            sum_frames       = 0;
            sum_fps          = 0.0;
            mean_fps         = 0.0;
            max_fps          = jobs[i][j][0]->fps;
            min_fps          = jobs[i][j][0]->fps;
            sum_skip_frames  = 0;
            mean_skip_frames = 0;
            for (int k = 0; k < g_sessions; k++) {
                if (jobs[i][j][k]->fps > max_fps) {
                    max_fps = jobs[i][j][k]->fps;
                }
                if (jobs[i][j][k]->fps < min_fps) {
                    min_fps = jobs[i][j][k]->fps;
                }
                sum_frames += jobs[i][j][k]->frames;
                sum_fps += jobs[i][j][k]->fps;
                sum_skip_frames += jobs[i][j][k]->first_read_frames;
                av_log(NULL, AV_LOG_INFO,
                       "thread card:%2d, "
                       "dev:%2d, "
                       "session:%2d, "
                       "frames:%5d, "
                       "skip_frames:%5lu, "
                       "fps:%5.2f, "
                       "latency:%lu\n",
                       i, j, k, jobs[i][j][k]->frames, jobs[i][j][k]->first_read_frames, jobs[i][j][k]->fps,
                       jobs[i][j][k]->latency);
            }
            mean_fps         = sum_fps / g_sessions;
            mean_skip_frames = sum_skip_frames / g_sessions;
            /*cal standard_deviation*/
            variance = 0.0;

            standard_deviation = 0.0;
            diff               = 0.0;
            for (int k = 0; k < g_sessions; k++) {
                diff = jobs[i][j][k]->fps - mean_fps;
                variance += diff * diff;
                sum_latency += jobs[i][j][k]->latency;
                if (jobs[i][j][k]) free(jobs[i][j][k]);
            }
            standard_deviation = sqrt(variance / g_sessions);
            mean_latency       = sum_latency / g_sessions;
            printf(
                "card:%2d, "
                "dev:%2d, "
                "nsession:%2d, "
                "sf:%2d,  "
                "skip_frames:%5lu, "
                "standard deviation:%8.2f, "
                "mean_latency:%5lu, "
                "max_fps:%8.2f, "
                "min_fps:%8.2f, "
                "mean_fps:%8.2f\n",
                i, j, g_sessions, g_frame_sf, mean_skip_frames, standard_deviation, mean_latency, max_fps, min_fps,
                mean_fps);
        }
    }
    av_log(NULL, AV_LOG_INFO, "main thread finish\n");
    pthread_mutex_destroy(&cb_av_log_lock);
    pseudo_barrier_destroy(&g_barrier_start);
    pseudo_barrier_destroy(&g_barrier_end);
    pseudo_barrier_destroy(&g_barrier_frame);
    return 0;
}
