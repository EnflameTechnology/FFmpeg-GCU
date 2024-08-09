#! /bin/bash

# Add the following line to the end of the file
END='HWACCEL_AUTODETECT_LIBRARY_LIST="'
END1='--disable-cuvid*'
HW_DECODE_TOPS='topscodec'
DISABLE='  --disable-topscodec        disable topscodec support [autodetect]'

C_FILE='configure'

 if grep -m 1 -Fq  "topscodec" $C_FILE;then
    echo "find topscodec exit"
    exit 0
 fi

#configure 1
sed -i "/${END}/a \
${HW_DECODE_TOPS}" ${C_FILE}

sed -i "/${END1}/a \
${DISABLE}" ${C_FILE}

#configure 2
END2='av1_qsv_decoder_select="qsvdec"'
H263='h263_topscodec_decoder_deps="topscodec"\n'
AV1='av1_topscodec_decoder_deps="topscodec"\n'
AVS='avs_topscodec_decoder_deps="topscodec"\n'
AVS2='avs2_topscodec_decoder_deps="topscodec"\n'
JPEG='jpeg_topscodec_decoder_deps="topscodec"\n'
H264='h264_topscodec_decoder_deps="topscodec"\n'
H264_BSF='h264_topscodec_decoder_select="h264_mp4toannexb_bsf"\n'
HEVC='hevc_topscodec_decoder_deps="topscodec"\n'
HEVC_BSF='hevc_topscodec_decoder_select="hevc_mp4toannexb_bsf"\n'
VP9='vp9_topscodec_decoder_deps="topscodec"\n'
VP8='vp8_topscodec_decoder_deps="topscodec"\n'
VC1='vc1_topscodec_decoder_deps="topscodec"\n'
MPEG4='mpeg4_topscodec_decoder_deps="topscodec"\n'
MPEG2='mpeg2_topscodec_decoder_deps="topscodec"\n'
MPEG1='mpeg1_topscodec_decoder_deps="topscodec"\n'
MJPEG='mjpeg_topscodec_decoder_deps="topscodec"\n'

sed -i "/${END2}/a \
${H263}\
${AV1}\
${AVS}\
${AVS2}\
${JPEG}\
${H264}\
${H264_BSF}\
${HEVC}\
${HEVC_BSF}\
${VP9}\
${VP8}\
${VC1}\
${MPEG4}\
${MPEG2}\
${MPEG1}\
${MJPEG}" ${C_FILE}

#configure 3
END3='EXAMPLE_LIST="'
HW_DECODE_TOPS_EXAMPLE='hw_decode_tops_example\n'
DECODE_TOPS_EXAMPLE='decode_tops_example\n'
HW_DECODE_MULTI_TOPS_EXAMPLE='hw_decode_multi_tops_example'

sed -i "/${END3}/a \
${HW_DECODE_TOPS_EXAMPLE}\
${DECODE_TOPS_EXAMPLE}\
${HW_DECODE_MULTI_TOPS_EXAMPLE}" ${C_FILE}