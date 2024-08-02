#! /bin/bash

# Add the following line to the end of the file
#hwcontext_internal.h
END='extern const HWContextType ff_hwcontext_type_cuda;'
TOPSCODEC='extern const HWContextType ff_hwcontext_type_topscodec;/*enflame*/'
FILE='hwcontext_internal.h'

sed -i "/${END}/a \
${TOPSCODEC} " ${FILE}

#hwcontext.h
HW_END_H='AV_HWDEVICE_TYPE_CUDA,'
HW_TYPE_H='\\tAV_HWDEVICE_TYPE_TOPSCODEC,'
HW_FILE_H='hwcontext.h'

sed -i "/${HW_END_H}/a \
${HW_TYPE_H} " ${HW_FILE_H}

#hwcontext.c
HW_END_C='static const char \*const hw_type_names\[\] = {'
HW_TYPE_C='\\t[AV_HWDEVICE_TYPE_TOPSCODEC] = "topscodec",'
HW_FILE_C='hwcontext.c'

sed -i "/${HW_END_C}/a \
${HW_TYPE_C} " ${HW_FILE_C}

#hwcontext.c 2
HW_END_C2='static const HWContextType \* const hw_table\[\] = {'
HW_TYPE_C2='#if CONFIG_TOPSCODEC\n\t&ff_hwcontext_type_topscodec,\n#endif'

sed -i "/${HW_END_C2}/a \
${HW_TYPE_C2} " ${HW_FILE_C}

#Makefile
M_END_AVTUILS='film_grain_params.h'
M_HEANDER_AVTUILS='\\t   hwcontext_topscodec.h'
M_AVTUILS='Makefile'

sed -i "/${M_END_AVTUILS}/a \
${M_HEANDER_AVTUILS} " ${M_AVTUILS}

#Makefile 2
M_END_AVTUILS2='OBJS-$(CONFIG_CUDA)                     += hwcontext_cuda.o'
M_OBJ_AVTUILS2='OBJS-$(CONFIG_TOPSCODEC)                += hwcontext_topscodec.o'

sed -i "/${M_END_AVTUILS2}/a \
${M_OBJ_AVTUILS2} " ${M_AVTUILS}

#Makefile 3
M_END_AVTUILS3='SKIPHEADERS-$(HAVE_CUDA_H)             += hwcontext_cuda.h'
M_OBJ_AVTUILS3='SKIPHEADERS-$(CONFIG_TOPSCODEC)        += hwcontext_topscodec.h'

sed -i "/${M_END_AVTUILS3}/i \
${M_OBJ_AVTUILS3} " ${M_AVTUILS}

#pixdesc.c
PD_END='\[AV_PIX_FMT_CUDA\] = {'
PD_TYPE='\\t\[AV_PIX_FMT_EFCCODEC\] = { \n\t\t\.name = "topscodec", \n\t\t\.flags = AV_PIX_FMT_FLAG_HWACCEL, \n\t},'
PIXDESC='pixdesc.c'

sed -i "/${PD_END}/i \
${PD_TYPE}" ${PIXDESC}

#pixdesc.c 2
PD_END2='\[AV_PIX_FMT_X2RGB10LE\] = {'
PD_TYPE2_RGB24P=$(cat << EOF
        [AV_PIX_FMT_RGB24P] = {\n \
        .name = "rgb24p",\n \
        .nb_components = 3,\n \
        .log2_chroma_w = 0,\n \
        .log2_chroma_h = 0,\n \
        .comp = {\n \
            { 0, 1, 0, 0, 8, 0, 7, 1 },        /* R */\n \
            { 1, 1, 0, 0, 8, 0, 7, 1 },        /* G */\n \
            { 2, 1, 0, 0, 8, 0, 7, 1 },        /* B */\n \
        },\n \
        .flags = AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_RGB,\n \
    },
EOF
)
PD_TYPE2_BGR24P=$(cat << EOF
        [AV_PIX_FMT_BGR24P] = {\n \
        .name = "bgr24p",\n \
        .nb_components = 3,\n \
        .log2_chroma_w = 0,\n \
        .log2_chroma_h = 0,\n \
        .comp = {\n \
            { 0, 1, 0, 0, 8, 0, 7, 1 },        /* B */\n \
            { 1, 1, 0, 0, 8, 0, 7, 1 },        /* G */\n \
            { 2, 1, 0, 0, 8, 0, 7, 1 },        /* R */\n \
        },\n \
        .flags = AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_RGB,\n \
    },
EOF
)

PD_TYPE2_P010LE=$(cat << EOF
        [AV_PIX_FMT_P010LE] = {\n \
        .name = "p010le",\n \
        .nb_components = 3,\n \
        .log2_chroma_w = 1,\n \
        .log2_chroma_h = 1,\n \
        .comp = {\n \
            { 0, 2, 0, 6, 10, 1, 9, 1 },        /* Y */\n \
            { 1, 4, 0, 6, 10, 3, 9, 1 },        /* U */\n \
            { 1, 4, 2, 6, 10, 3, 9, 3 },        /* V */\n \
        },\n \
        .flags = AV_PIX_FMT_FLAG_PLANAR,\n \
    },
EOF
)

sed -i "/${PD_END2}/i \
${PD_TYPE2_RGB24P} ${PD_TYPE2_BGR24P} ${PD_TYPE2_P010LE}" ${PIXDESC}

#pixfmt.h
RGB24P='\\tAV_PIX_FMT_RGB24P,     ///< planar RGB 8:8:8, 24bpp, RRR...GGG...BBB...\n'
BGR24P='\tAV_PIX_FMT_BGR24P,     ///< planar BGR 8:8:8, 24bpp, BBB...GGG...RRR...\n'
EFCODEC='\tAV_PIX_FMT_EFCCODEC,\n'
P010LE='\tAV_PIX_FMT_P010LE_EF, ///< like NV12, with 10bpp per component, little-endian\n'

PIX_END='AV_PIX_FMT_NB'
PIX_FILE='pixfmt.h'

sed -i "/${PIX_END}/i \
${RGB24P}\
${BGR24P}\
${EFCODEC}\
${P010LE} " ${PIX_FILE}