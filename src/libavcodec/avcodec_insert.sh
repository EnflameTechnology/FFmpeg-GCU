#! /bin/bash

# Add the following line to the end of the file
END='extern AVCodec ff_zmbv_decoder;'
AVS2='extern AVCodec ff_avs2_topscodec_decoder;\n'
AVS='extern AVCodec ff_avs_topscodec_decoder;\n'
AV1='extern AVCodec ff_av1_topscodec_decoder;\n'
H263='extern AVCodec ff_h263_topscodec_decoder;\n'
H264='extern AVCodec ff_h264_topscodec_decoder;\n'
HEVC='extern AVCodec ff_hevc_topscodec_decoder;\n'
MJPEG='extern AVCodec ff_mjpeg_topscodec_decoder;\n'
MPEG4='extern AVCodec ff_mpeg4_topscodec_decoder;\n'
MPEG2='extern AVCodec ff_mpeg2_topscodec_decoder;\n'
VC1='extern AVCodec ff_vc1_topscodec_decoder;\n'
VP8='extern AVCodec ff_vp8_topscodec_decoder;\n'
VP9='extern AVCodec ff_vp9_topscodec_decoder;\n'
FILE_CODEC='allcodecs.c'

M_END='OBJS-$(CONFIG_ZMBV_ENCODER)            += zmbvenc.o'
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

M_END_SUB='OBJS-$(CONFIG_WMV2DSP)                 += wmv2dsp.o'
M_BUF='OBJS-$(CONFIG_TOPSCODEC)               += ff_topscodec_buffers.o\n'
M_FILE='Makefile'

sed -i "/${END}/a \
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

sed -i "/${M_END_SUB}/a \
${M_BUF} " ${M_FILE}