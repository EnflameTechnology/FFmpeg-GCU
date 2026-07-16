#! /bin/bash

# Add the following line to the end of the file
WS='[[:space:]]*'
FILE_CAVS='cavsvideodec.c'

LINE="#define${WS}CAVS_PROFILE_JIZHUN"
CAVS="#define CAVS_PROFILE_GUANDIAN    0x48"

IF="if${WS}\(\*ptr${WS}!=${WS}CAVS_PROFILE_JIZHUN\)"
IF_REPLACE="if (*ptr != CAVS_PROFILE_JIZHUN \&\& *ptr != CAVS_PROFILE_GUANDIAN)"

 if ! grep -Fxq "$CAVS" $FILE_CAVS
 then
    sed -E -i "/${LINE}/a ${CAVS}" ${FILE_CAVS}
    sed -E -i "s/${IF}/${IF_REPLACE}/g" ${FILE_CAVS}
fi


# add input_frame_stride_align to rawvideo decoders
RAWVIDEO_DECODERS="rawvideodec.c"
STRIDE_ALIGN="int input_frame_stride_align;"
if grep -Fxq "} RawVideoDemuxerContext;" $RAWVIDEO_DECODERS
then
    sed -E -i "/} RawVideoDemuxerContext;/i ${STRIDE_ALIGN}" ${RAWVIDEO_DECODERS}
fi

PARAM_LINE='static const AVOption rawvideo_options[] = {'
ESCAPED_PARAM_LINE=$(echo "$PARAM_LINE" | sed 's/\[/\\[/g; s/\]/\\]/g')
RAW_REPLACE='    {"stride_align", "input frame stride align", OFFSET(input_frame_stride_align), AV_OPT_TYPE_INT, {.i64 = 1}, 1, INT_MAX, AV_OPT_FLAG_DECODING_PARAM|AV_OPT_FLAG_VIDEO_PARAM},'
PARAM_REPLACE=$(echo "$RAW_REPLACE" | sed 's/"/\\"/g')
if grep -Fqx "$PARAM_LINE" "$RAWVIDEO_DECODERS"; then
  sed -i "/${ESCAPED_PARAM_LINE}/a ${PARAM_REPLACE}" "$RAWVIDEO_DECODERS"
fi

# n7.1
OLD_PART="av_image_get_buffer_size(pix_fmt, s->width, s->height, 1)"
NEW_PART="av_image_get_buffer_size(pix_fmt, s->width, s->height, s->input_frame_stride_align)"
if grep -qF "$OLD_PART" "$RAWVIDEO_DECODERS"; then
  sed -i "s|$OLD_PART|$NEW_PART|g" "$RAWVIDEO_DECODERS"
fi

# n3.2
OLD_PART_N32="av_image_get_buffer_size(st->codecpar->format, s->width, s->height, 1)"
NEW_PART_N32="av_image_get_buffer_size(st->codecpar->format, s->width, s->height, s->input_frame_stride_align)"
if grep -qF "$OLD_PART_N32" "$RAWVIDEO_DECODERS"; then
  sed -i "s|$OLD_PART_N32|$NEW_PART_N32|g" "$RAWVIDEO_DECODERS"
fi
