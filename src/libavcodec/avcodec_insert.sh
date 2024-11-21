#! /bin/bash

# Add the following line to the end of the file

# 5.x
END_5X='extern \(const \)\?FFCodec ff_zmbv_decoder;'
FF_AVS2='extern const FFCodec ff_avs2_topscodec_decoder;\n'
FF_AVS='extern const FFCodec ff_avs_topscodec_decoder;\n'
FF_AV1='extern const FFCodec ff_av1_topscodec_decoder;\n'
FF_H263='extern const FFCodec ff_h263_topscodec_decoder;\n'
FF_H264='extern const FFCodec ff_h264_topscodec_decoder;\n'
FF_HEVC='extern const FFCodec ff_hevc_topscodec_decoder;\n'
FF_MJPEG='extern const FFCodec ff_mjpeg_topscodec_decoder;\n'
FF_MPEG4='extern const FFCodec ff_mpeg4_topscodec_decoder;\n'
FF_MPEG2='extern const FFCodec ff_mpeg2_topscodec_decoder;\n'
FF_VC1='extern const FFCodec ff_vc1_topscodec_decoder;\n'
FF_VP8='extern const FFCodec ff_vp8_topscodec_decoder;\n'
FF_VP9='extern const FFCodec ff_vp9_topscodec_decoder;\n'

#4.x
END_4X='extern \(const \)\?AVCodec ff_zmbv_decoder;'
AVS2='extern const AVCodec ff_avs2_topscodec_decoder;\n'
AVS='extern const AVCodec ff_avs_topscodec_decoder;\n'
AV1='extern const AVCodec ff_av1_topscodec_decoder;\n'
H263='extern const AVCodec ff_h263_topscodec_decoder;\n'
H264='extern const AVCodec ff_h264_topscodec_decoder;\n'
HEVC='extern const AVCodec ff_hevc_topscodec_decoder;\n'
MJPEG='extern const AVCodec ff_mjpeg_topscodec_decoder;\n'
MPEG4='extern const AVCodec ff_mpeg4_topscodec_decoder;\n'
MPEG2='extern const AVCodec ff_mpeg2_topscodec_decoder;\n'
VC1='extern const AVCodec ff_vc1_topscodec_decoder;\n'
VP8='extern const AVCodec ff_vp8_topscodec_decoder;\n'
VP9='extern const AVCodec ff_vp9_topscodec_decoder;\n'

#3.x
END_3X='REGISTER_DECODER(AASC*'
# AVS2_3X='REGISTER_DECODER(AVS2_TOPSCODEC, avs2_topscodec);\n'
AVS_3X='REGISTER_DECODER(AVS_TOPSCODEC, avs_topscodec);\n'
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
# HWACCEL 3.x
# S_AVS2_3X='REGISTER_HWACCEL(AVS2_TOPSCODEC, avs2_topscodec);\n'
S_AVS_3X='REGISTER_HWACCEL(AVS_TOPSCODEC, avs_topscodec);\n'
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

M_END='OBJS-$(CONFIG_ZMBV_ENCODER)*'
M_AVS2='OBJS-$(CONFIG_AVS2_TOPSCODEC_DECODER)  += ff_topscodec_dec.o\n'
M_AVS='OBJS-$(CONFIG_AVS_TOPSCODEC_DECODER)   += ff_topscodec_dec.o\n'
M_AV1='OBJS-$(CONFIG_AV1_TOPSCODEC_DECODER)   += ff_topscodec_dec.o\n'
M_H263='OBJS-$(CONFIG_H263_TOPSCODEC_DECODER)  += ff_topscodec_dec.o\n'
M_H264='OBJS-$(CONFIG_H264_TOPSCODEC_DECODER)  += ff_topscodec_dec.o\n'
M_HEVC='OBJS-$(CONFIG_HEVC_TOPSCODEC_DECODER)  += ff_topscodec_dec.o\n'
M_MJPEG='OBJS-$(CONFIG_MJPEG_TOPSCODEC_DECODER) += ff_topscodec_dec.o\n'
M_MJPEG2='OBJS-$(CONFIG_MPEG2_TOPSCODEC_DECODER) += ff_topscodec_dec.o\n'
M_MPEG4='OBJS-$(CONFIG_MPEG4_TOPSCODEC_DECODER) += ff_topscodec_dec.o\n'
M_VC1='OBJS-$(CONFIG_VC1_TOPSCODEC_DECODER)   += ff_topscodec_dec.o\n'
M_VP8='OBJS-$(CONFIG_VP8_TOPSCODEC_DECODER)   += ff_topscodec_dec.o\n'
M_VP9='OBJS-$(CONFIG_VP9_TOPSCODEC_DECODER)   += ff_topscodec_dec.o\n'

FILE_CODEC='allcodecs.c'

M_END_SUB='OBJS-$(CONFIG_WMV2DSP)*'
M_BUF='OBJS-$(CONFIG_TOPSCODEC)               += ff_topscodec_buffers.o\n'
M_FILE='Makefile'

 if grep -Fq "topscodec" $FILE_CODEC;then
    echo "find topscodec exit"
    exit 0
 fi

#allcodecs.c insert
 if grep -Fq "FFCodec" $FILE_CODEC;then
   echo "Codec Version is 5.x"
   sed -i "/${END_5X}/a \
   ${FF_AVS2}\
   ${FF_AVS}\
   ${FF_AV1}\
   ${FF_H263}\
   ${FF_H264}\
   ${FF_HEVC}\
   ${FF_MJPEG}\
   ${FF_MPEG4}\
   ${FF_MPEG2}\
   ${FF_VC1}\
   ${FF_VP8}\
   ${FF_VP9} " ${FILE_CODEC}
  elif  grep -Fq "AVCodec" $FILE_CODEC && grep -Fq "REGISTER_DECODER" $FILE_CODEC;then
   echo "Codec Version is 3.x"
   sed -i "/${END_3X}/a \
   ${AVS_3X}\
   ${H263_3X}\
   ${H264_3X}\
   ${HEVC_3X}\
   ${MJPEG_3X}\
   ${MPEG4_3X}\
   ${MPEG2_3X}\
   ${VC1_3X}\
   ${VP8_3X}\
   ${VP9_3X} \
   ${S_AVS_3X}\
   ${S_H263_3X}\
   ${S_H264_3X}\
   ${S_HEVC_3X}\
   ${S_MJPEG_3X}\
   ${S_MPEG4_3X}\
   ${S_MPEG2_3X}\
   ${S_VC1_3X}\
   ${S_VP8_3X}\
   ${S_VP9_3X}" ${FILE_CODEC}
 elif  grep -Fq "AVCodec" $FILE_CODEC;then
   echo "Codec Version is 4.x"
   sed -i "/${END_4X}/a \
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
   ${VP9} " ${FILE_CODEC}
fi

#makefile insert
sed -i "/${M_END}/a \
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
${M_VP9} " ${M_FILE}

#makefile insert
sed -i "/${M_END_SUB}/a \
${M_BUF} " ${M_FILE}

