#! /bin/bash

WS="[[:space:]]*"
CODEC="AVCodec"
FILE_CODEC='allcodecs.c'

if grep -Fq "topscodec" $FILE_CODEC; then
  echo "find topscodec exit"
  exit 0
fi

if grep -Fq "FFCodec" $FILE_CODEC; then
  CODEC="FFCodec"
  echo "Codec Version is :$FFCodec"
fi

# Add the following line to the end of the file
# allcodecs.c insert

if grep -Fq "REGISTER_DECODER" $FILE_CODEC; then
  #3.x
  END_3X="REGISTER_DECODER\(AASC"
  # AVS2_3X='REGISTER_DECODER(AVS2_TOPSCODEC, avs2_topscodec);\n'
  # AVS_3X='REGISTER_DECODER(AVS_TOPSCODEC, avs_topscodec);\n'
  # AV1_3X='REGISTER_DECODER(AV1_TOPSCODEC, av1_topscodec);\n'
  H263_3X='REGISTER_DECODER(H263_TOPSCODEC, h263_topscodec);\n'
  H264_3X='REGISTER_DECODER(H264_TOPSCODEC, h264_topscodec);\n'
  HEVC_3X='REGISTER_DECODER(HEVC_TOPSCODEC, hevc_topscodec);\n'
  MJPEG_3X='REGISTER_DECODER(MJPEG_TOPSCODEC, mjpeg_topscodec);\n'
  MPEG4_3X='REGISTER_DECODER(MPEG4_TOPSCODEC, mpeg4_topscodec);\n'
  MPEG2_3X='REGISTER_DECODER(MPEG2_TOPSCODEC, mpeg2_topscodec);\n'
  VC1_3X='REGISTER_DECODER(VC1_TOPSCODEC, vc1_topscodec);\n'
  VP8_3X='REGISTER_DECODER(VP8_TOPSCODEC, vp8_topscodec);\n'
  VP9_3X='REGISTER_DECODER(VP9_TOPSCODEC, vp9_topscodec);\n'
  H264_ENC_3X='REGISTER_ENCODER(H264_TOPSCODEC_ENC, h264_topscodec_enc);\n'
  HEVC_ENC_3X='REGISTER_ENCODER(HEVC_TOPSCODEC_ENC, hevc_topscodec_enc);\n'
  VP9_ENC_3X='REGISTER_ENCODER(VP9_TOPSCODEC_ENC, vp9_topscodec_enc);\n'
  VP8_ENC_3X='REGISTER_ENCODER(VP8_TOPSCODEC_ENC, vp8_topscodec_enc);\n'
  MJPEG_ENC_3X='REGISTER_ENCODER(MJPEG_TOPSCODEC_ENC, mjpeg_topscodec_enc);\n'
  # HWACCEL 3.x
  # S_AVS2_3X='REGISTER_HWACCEL(AVS2_TOPSCODEC, avs2_topscodec);\n'
  # S_AVS_3X='REGISTER_HWACCEL(AVS_TOPSCODEC, avs_topscodec);\n'
  # S_AV1_3X='REGISTER_HWACCEL(AV1_TOPSCODEC, av1_topscodec);\n'
  S_H263_3X='REGISTER_HWACCEL(H263_TOPSCODEC, h263_topscodec);\n'
  S_H264_3X='REGISTER_HWACCEL(H264_TOPSCODEC, h264_topscodec);\n'
  S_HEVC_3X='REGISTER_HWACCEL(HEVC_TOPSCODEC, hevc_topscodec);\n'
  S_MJPEG_3X='REGISTER_HWACCEL(MJPEG_TOPSCODEC, mjpeg_topscodec);\n'
  S_MPEG4_3X='REGISTER_HWACCEL(MPEG4_TOPSCODEC, mpeg4_topscodec);\n'
  S_MPEG2_3X='REGISTER_HWACCEL(MPEG2_TOPSCODEC, mpeg2_topscodec);\n'
  S_VC1_3X='REGISTER_HWACCEL(VC1_TOPSCODEC, vc1_topscodec);\n'
  S_VP8_3X='REGISTER_HWACCEL(VP8_TOPSCODEC, vp8_topscodec);\n'
  S_VP9_3X='REGISTER_HWACCEL(VP9_TOPSCODEC, vp9_topscodec);\n'
  echo "Codec Version is 3.x"
  sed -E -i "/${END_3X}/a \
  ${H263_3X}\
  ${H264_3X}\
  ${HEVC_3X}\
  ${MJPEG_3X}\
  ${MPEG4_3X}\
  ${MPEG2_3X}\
  ${VC1_3X}\
  ${VP8_3X}\
  ${VP9_3X}\
  ${H264_ENC_3X}\
  ${HEVC_ENC_3X}\
  ${VP9_ENC_3X}\
  ${VP8_ENC_3X}\
  ${MJPEG_ENC_3X} \
  ${S_H263_3X}\
  ${S_H264_3X}\
  ${S_HEVC_3X}\
  ${S_MJPEG_3X}\
  ${S_MPEG4_3X}\
  ${S_MPEG2_3X}\
  ${S_VC1_3X}\
  ${S_VP8_3X}\
  ${S_VP9_3X}" ${FILE_CODEC}
else
  # 5.x 4.x
  echo "Codec Version is 5.x/4.x"
  END_5X="extern${WS}(const)?${WS}${CODEC}${WS}ff_zmbv_decoder;"
  AVS2="extern const ${CODEC} ff_avs2_topscodec_decoder;\n"
  AVS="extern const ${CODEC} ff_avs_topscodec_decoder;\n"
  AV1="extern const ${CODEC} ff_av1_topscodec_decoder;\n"
  H263="extern const ${CODEC} ff_h263_topscodec_decoder;\n"
  H264="extern const ${CODEC} ff_h264_topscodec_decoder;\n"
  HEVC="extern const ${CODEC} ff_hevc_topscodec_decoder;\n"
  MJPEG="extern const ${CODEC} ff_mjpeg_topscodec_decoder;\n"
  MPEG4="extern const ${CODEC} ff_mpeg4_topscodec_decoder;\n"
  MPEG2="extern const ${CODEC} ff_mpeg2_topscodec_decoder;\n"
  VC1="extern const ${CODEC} ff_vc1_topscodec_decoder;\n"
  VP8="extern const ${CODEC} ff_vp8_topscodec_decoder;\n"
  VP9="extern const ${CODEC} ff_vp9_topscodec_decoder;\n"
  H264_ENC="extern const ${CODEC} ff_h264_topscodec_enc_encoder;\n"
  HEVC_ENC="extern const ${CODEC} ff_hevc_topscodec_enc_encoder;\n"
  VP9_ENC="extern const ${CODEC} ff_vp9_topscodec_enc_encoder;\n"
  VP8_ENC="extern const ${CODEC} ff_vp8_topscodec_enc_encoder;\n"
  MJPEG_ENC="extern const ${CODEC} ff_mjpeg_topscodec_enc_encoder;\n"

  sed -E --in-place="bak" "/${END_5X}/a \
  ${AVS2}\
  ${AVS}\
  ${AV1}\
  ${H263}\
  ${H264}\
  ${HEVC}\
  ${MJPEG}\
  ${MPEG4}\
  ${MPEG2}\
  ${VC1}\
  ${VP8}\
  ${VP9}\
  ${H264_ENC}\
  ${HEVC_ENC}\
  ${VP9_ENC}\
  ${VP8_ENC}\
  ${MJPEG_ENC}" ${FILE_CODEC}
fi

# Makefile insert
M_FILE="Makefile"
M_END="OBJS\-\\\$\(CONFIG_ZMBV_ENCODER\)"
M_AVS2="OBJS-\$(CONFIG_AVS2_TOPSCODEC_DECODER)  += ff_topscodec_dec.o\n"
M_AVS="OBJS-\$(CONFIG_AVS_TOPSCODEC_DECODER)   += ff_topscodec_dec.o\n"
M_AV1="OBJS-\$(CONFIG_AV1_TOPSCODEC_DECODER)   += ff_topscodec_dec.o\n"
M_H263="OBJS-\$(CONFIG_H263_TOPSCODEC_DECODER)  += ff_topscodec_dec.o\n"
M_H264="OBJS-\$(CONFIG_H264_TOPSCODEC_DECODER)  += ff_topscodec_dec.o\n"
M_HEVC="OBJS-\$(CONFIG_HEVC_TOPSCODEC_DECODER)  += ff_topscodec_dec.o\n"
M_MJPEG="OBJS-\$(CONFIG_MJPEG_TOPSCODEC_DECODER) += ff_topscodec_dec.o\n"
M_MJPEG2="OBJS-\$(CONFIG_MPEG2_TOPSCODEC_DECODER) += ff_topscodec_dec.o\n"
M_MPEG4="OBJS-\$(CONFIG_MPEG4_TOPSCODEC_DECODER) += ff_topscodec_dec.o\n"
M_VC1="OBJS-\$(CONFIG_VC1_TOPSCODEC_DECODER)   += ff_topscodec_dec.o\n"
M_VP8="OBJS-\$(CONFIG_VP8_TOPSCODEC_DECODER)   += ff_topscodec_dec.o\n"
M_VP9="OBJS-\$(CONFIG_VP9_TOPSCODEC_DECODER)   += ff_topscodec_dec.o\n"
M_H264_ENC="OBJS-\$(CONFIG_H264_TOPSCODEC_ENC_ENCODER) += ff_topscodec_enc.o\n"
M_HEVC_ENC="OBJS-\$(CONFIG_HEVC_TOPSCODEC_ENC_ENCODER) += ff_topscodec_enc.o\n"
M_VP9_ENC="OBJS-\$(CONFIG_VP9_TOPSCODEC_ENC_ENCODER) += ff_topscodec_enc.o\n"
M_VP8_ENC="OBJS-\$(CONFIG_VP8_TOPSCODEC_ENC_ENCODER) += ff_topscodec_enc.o\n"
M_MJPEG_ENC="OBJS-\$(CONFIG_MJPEG_TOPSCODEC_ENC_ENCODER) += ff_topscodec_enc.o\n"

echo "Makefile insert:${M_END}"
sed -E -i "/${M_END}/a \
${M_AVS2}\
${M_AVS}\
${M_AV1}\
${M_H263}\
${M_H264}\
${M_HEVC}\
${M_MJPEG}\
${M_MJPEG2}\
${M_MPEG4}\
${M_VC1}\
${M_VP8}\
${M_VP9}\
${M_H264_ENC}\
${M_HEVC_ENC}\
${M_VP9_ENC}\
${M_VP8_ENC}\
${M_MJPEG_ENC} " ${M_FILE}

M_END_SUB="OBJS\-\\\$\(CONFIG_WMV2DSP\)"
M_BUF="OBJS-\$(CONFIG_TOPSCODEC)               += ff_topscodec_buffers.o\n"
M_UTILS="OBJS-\$(CONFIG_TOPSCODEC)               += ff_topscodec_utils.o\n"
#makefile insert
sed -E -i "/${M_END_SUB}/a \
${M_BUF}\
${M_UTILS} " ${M_FILE}

#add rawdec.c
RAWDEC="rawdec.c"
STRIDE_ALIGN="int input_frame_stride_align;"
if grep -Fxq "} RawVideoContext;" $RAWDEC
then
    sed -E -i "/} RawVideoContext;/i ${STRIDE_ALIGN}" ${RAWDEC}
fi

PARAM_LINE='static const AVOption options[]={'
ESCAPED_PARAM_LINE=$(echo "$PARAM_LINE" | sed 's/\[/\\[/g; s/\]/\\]/g')
RAW_REPLACE='    {"stride_align", "input frame stride align", offsetof(RawVideoContext,input_frame_stride_align), AV_OPT_TYPE_INT, {.i64 = 1}, 1, INT_MAX, AV_OPT_FLAG_DECODING_PARAM|AV_OPT_FLAG_VIDEO_PARAM},'
PARAM_REPLACE=$(echo "$RAW_REPLACE" | sed 's/"/\\"/g')
if grep -Fqx "$PARAM_LINE" "$RAWDEC"; then
  sed -i "/${ESCAPED_PARAM_LINE}/a ${PARAM_REPLACE}" "$RAWDEC"
fi

OLD_PART="avctx->height, 1);"
NEW_PART="avctx->height, context->input_frame_stride_align);"
if grep -qF "$OLD_PART" "$RAWDEC"; then
  sed -i "s|$OLD_PART|$NEW_PART|g" "$RAWDEC"
fi

OLD_PART2="avctx->width, avctx->height, 1"
NEW_PART2="avctx->width, avctx->height, context->input_frame_stride_align"
if grep -qF "$OLD_PART2" "$RAWDEC"; then
  sed -i "s|$OLD_PART2|$NEW_PART2|g" "$RAWDEC"
fi

#add rawenc.c
RAWENC="rawenc.c"
HEADER_FILE=$(cat << EOF
#include "libavutil/opt.h"
typedef struct RawencContext {
    const AVClass *class;
    int input_frame_stride_align;
} RawencContext;

#define OFFSET(x) offsetof(RawencContext, x)
EOF
)

# n7.1
RAWENC_CONTEXT=$(cat << EOF
static const AVOption options[] = {
    {"stride_align", "input frame stride align", OFFSET(input_frame_stride_align), AV_OPT_TYPE_INT, {.i64 = 1}, 1, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM|AV_OPT_FLAG_VIDEO_PARAM},
    { NULL }
};
static const AVClass rawenc_class = {
    .class_name = "rawvideo_encoder",
    .item_name = av_default_item_name,
    .option = options,
    .version = LIBAVUTIL_VERSION_INT,
    .log_level_offset_offset = 0,
    .parent_log_context_offset = 0,
    .category = 0,
    .get_category = NULL,
    .query_ranges = NULL,
    .child_next = NULL,
    .child_class_iterate = NULL,
};
EOF
)

PRIVATE_CONTEXT=$(cat << EOF
    .p.priv_class = &rawenc_class,
    .priv_data_size = sizeof(RawencContext),
EOF
)

# n3.2
RAWENC_CONTEXT_LEGACY=$(cat << EOF
static const AVOption options[] = {
    {"stride_align", "input frame stride align", OFFSET(input_frame_stride_align), AV_OPT_TYPE_INT, {.i64 = 1}, 1, INT_MAX, AV_OPT_FLAG_ENCODING_PARAM|AV_OPT_FLAG_VIDEO_PARAM},
    { NULL }
};
static const AVClass rawenc_class = {
    .class_name = "rawvideo_encoder",
    .item_name = av_default_item_name,
    .option = options,
    .version = LIBAVUTIL_VERSION_INT,
};
EOF
)

PRIVATE_CONTEXT_LEGACY=$(cat << EOF
    .priv_class = &rawenc_class,
    .priv_data_size = sizeof(RawencContext),
EOF
)

if grep -Fxq "const FFCodec ff_rawvideo_encoder = {" $RAWENC
then
    echo "找到: const FFCodec ff_rawvideo_encoder = {"
    # Insert RAWENC_CONTEXT before the matching line using temporary file
    TMP_FILE=$(mktemp)
    echo "$RAWENC_CONTEXT" > "$TMP_FILE"
    awk -v tmpfile="$TMP_FILE" '/const FFCodec ff_rawvideo_encoder = \{/ {while ((getline line < tmpfile) > 0) print line; close(tmpfile)} 1' ${RAWENC} > ${RAWENC}.tmp && mv ${RAWENC}.tmp ${RAWENC}
    rm -f "$TMP_FILE"
    
    # Insert PRIVATE_CONTEXT after the matching line
    TMP_FILE=$(mktemp)
    echo "$PRIVATE_CONTEXT" > "$TMP_FILE"
    awk -v tmpfile="$TMP_FILE" '/const FFCodec ff_rawvideo_encoder = \{/ {print; while ((getline line < tmpfile) > 0) print line; close(tmpfile); next} 1' ${RAWENC} > ${RAWENC}.tmp && mv ${RAWENC}.tmp ${RAWENC}
    rm -f "$TMP_FILE"
elif grep -Fxq "AVCodec ff_rawvideo_encoder = {" $RAWENC
then
    echo "找到: AVCodec ff_rawvideo_encoder = {"
    TMP_FILE=$(mktemp)
    echo "$RAWENC_CONTEXT_LEGACY" > "$TMP_FILE"
    awk -v tmpfile="$TMP_FILE" '/AVCodec ff_rawvideo_encoder = \{/ {while ((getline line < tmpfile) > 0) print line; close(tmpfile)} 1' ${RAWENC} > ${RAWENC}.tmp && mv ${RAWENC}.tmp ${RAWENC}
    rm -f "$TMP_FILE"

    TMP_FILE=$(mktemp)
    echo "$PRIVATE_CONTEXT_LEGACY" > "$TMP_FILE"
    awk -v tmpfile="$TMP_FILE" '/AVCodec ff_rawvideo_encoder = \{/ {print; while ((getline line < tmpfile) > 0) print line; close(tmpfile); next} 1' ${RAWENC} > ${RAWENC}.tmp && mv ${RAWENC}.tmp ${RAWENC}
    rm -f "$TMP_FILE"
else
    echo "未找到 rawvideo_encoder 定义"
fi

INIT_LINE='int ret = av_image_get_buffer_size(frame->format,'
ADD_LINE=$(cat << EOF
     RawencContext*    ctx          = NULL;
      ctx        = avctx->priv_data;
EOF
)

if grep -qF "$INIT_LINE" "$RAWENC"; then
   # Insert ADD_LINE before the matching line using temporary file
   TMP_FILE=$(mktemp)
   echo "$ADD_LINE" > "$TMP_FILE"
   awk -v tmpfile="$TMP_FILE" '/int ret = av_image_get_buffer_size\(frame->format,/ {while ((getline line < tmpfile) > 0) print line; close(tmpfile)} 1' ${RAWENC} > ${RAWENC}.tmp && mv ${RAWENC}.tmp ${RAWENC}
   rm -f "$TMP_FILE"
fi

if grep -qF " frame->width, frame->height, 1);" "$RAWENC"; then
   sed -E -i "s|frame->width, frame->height, 1\)|frame->width, frame->height, ctx->input_frame_stride_align)|" ${RAWENC}
fi

if grep -qF "static av_cold int raw_encode_init(AVCodecContext" "$RAWENC"; then
   # Insert HEADER_FILE before the matching line using temporary file
   TMP_FILE=$(mktemp)
   echo "$HEADER_FILE" > "$TMP_FILE"
   awk -v tmpfile="$TMP_FILE" '/static av_cold int raw_encode_init\(AVCodecContext/ {while ((getline line < tmpfile) > 0) print line; close(tmpfile)} 1' ${RAWENC} > ${RAWENC}.tmp && mv ${RAWENC}.tmp ${RAWENC}
   rm -f "$TMP_FILE"
fi

# Add memset before av_image_copy_to_buffer in rawenc.c
MEMSET_LINE="    memset(pkt->data, 0, pkt->size);"
COPY_BUFFER_LINE="if ((ret = av_image_copy_to_buffer(pkt->data, pkt->size,"
if grep -qF "$COPY_BUFFER_LINE" "$RAWENC"; then
   sed -i "/${COPY_BUFFER_LINE}/i ${MEMSET_LINE}" "$RAWENC"
fi

exit 0
