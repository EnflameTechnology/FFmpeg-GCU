#! /bin/bash

# Add the following line to the end of the file
END='HWACCEL_AUTODETECT_LIBRARY_LIST="'
END1='--disable-audiotoolbox*'
END2='HWACCEL_LIBRARY_LIST='
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

sed -i "/${END2}/a \
${HW_DECODE_TOPS}" ${C_FILE}

#configure 2
END2='vc1_cuvid_decoder_deps='
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

# configure 4
END4='avio_dir_cmd_deps=\"avformat avutil\"'
HW_DECODE_TOPS_EXAMPLE='hw_decode_tops_example_deps="avcodec avformat avutil"\n'
DECODE_TOPS_EXAMPLE='decode_tops_example_deps="avcodec avformat avutil"\n'
HW_DECODE_MULTI_TOPS_EXAMPLE='hw_decode_multi_tops_example_deps="avcodec avformat avutil"\n'

sed -i "/${END4}/a \
${HW_DECODE_TOPS_EXAMPLE}\
${DECODE_TOPS_EXAMPLE}\
${HW_DECODE_MULTI_TOPS_EXAMPLE}" ${C_FILE}

# configure 5 for n3.2
END5='vc1_cuvid_hwaccel_deps='
H263='h263_topscodec_hwaccel_deps="topscodec"\n'
AV1='av1_topscodec_hwaccel_deps="topscodec"\n'
AVS='avs_topscodec_hwaccel_deps="topscodec"\n'
AVS2='avs2_topscodec_hwaccel_deps="topscodec"\n'
JPEG='jpeg_topscodec_hwaccel_deps="topscodec"\n'
H264='h264_topscodec_hwaccel_deps="topscodec"\n'
HEVC='hevc_topscodec_hwaccel_deps="topscodec"\n'
VP9='vp9_topscodec_hwaccel_deps="topscodec"\n'
VP8='vp8_topscodec_hwaccel_deps="topscodec"\n'
VC1='vc1_topscodec_hwaccel_deps="topscodec"\n'
MPEG4='mpeg4_topscodec_hwaccel_deps="topscodec"\n'
MPEG2='mpeg2_topscodec_hwaccel_deps="topscodec"\n'
MPEG1='mpeg1_topscodec_hwaccel_deps="topscodec"\n'
MJPEG='mjpeg_topscodec_hwaccel_deps="topscodec"\n'

S_H263='h263_topscodec_decoder_select="h263_topscodec_hwaccel"\n'
S_AV1='av1_topscodec_decoder_select="av1_topscodec_hwaccel"\n'
S_AVS='avs_topscodec_decoder_select="avs_topscodec_hwaccel"\n'
S_AVS2='avs2_topscodec_decoder_select="avs2_topscodec_hwaccel"\n'
S_JPEG='jpeg_topscodec_decoder_select="jpeg_topscodec_hwaccel"\n'
S_H264='h264_topscodec_decoder_select="h264_topscodec_hwaccel"\n'
S_HEVC='hevc_topscodec_decoder_select="hevc_topscodec_hwaccel"\n'
S_VP9='vp9_topscodec_decoder_select="vp9_topscodec_hwaccel"\n'
S_VP8='vp8_topscodec_decoder_select="vp8_topscodec_hwaccel"\n'
S_VC1='vc1_topscodec_decoder_select="vc1_topscodec_hwaccel"\n'
S_MPEG4='mpeg4_topscodec_decoder_select="mpeg4_topscodec_hwaccel"\n'
S_MPEG2='mpeg2_topscodec_decoder_select="mpeg2_topscodec_hwaccel"\n'
S_MPEG1='mpeg1_topscodec_decoder_select="mpeg1_topscodec_hwaccel"\n'
S_MJPEG='mjpeg_topscodec_decoder_select="mjpeg_topscodec_hwaccel"\n'

sed -i "/${END5}/a \
${H263}\
${AV1}\
${AVS}\
${AVS2}\
${JPEG}\
${H264}\
${HEVC}\
${VP9}\
${VP8}\
${VC1}\
${MPEG4}\
${MPEG2}\
${MPEG1}\
${MJPEG}\
${S_H263}\
${S_AV1}\
${S_AVS}\
${S_AVS2}\
${S_JPEG}\
${S_H264}\
${S_HEVC}\
${S_VP9}\
${S_VP8}\
${S_VC1}\
${S_MPEG4}\
${S_MPEG2}\
${S_MPEG1}\
${S_MJPEG}" ${C_FILE}
