#! /bin/bash


# Add the following line to the end of the file
WS='[[:space:]]*'

HW_FILE_H='hwcontext.h'
 if grep  -Fq "topscodec" $HW_FILE_H;then
    echo "find topscodec exit"
    exit 0
 fi

# 3.0 3.2 does not have hwcontext_internal.h
FILE='hwcontext_internal.h'
if [ -f $FILE ]; then
    END="extern${WS}const${WS}HWContextType${WS}ff_hwcontext_type_cuda;"
    TOPSCODEC='extern const HWContextType ff_hwcontext_type_topscodec;/*enflame*/'
    sed -E -i "/${END}/a \
    ${TOPSCODEC} " ${FILE}
fi

#hwcontext.h
HW_END_H='AV_HWDEVICE_TYPE_CUDA,'
HW_TYPE_H='\\tAV_HWDEVICE_TYPE_TOPSCODEC,'
HW_FILE_H='hwcontext.h'
sed -i "/${HW_END_H}/a \
${HW_TYPE_H} " ${HW_FILE_H}

# 3.0 3.2 does not have hwcontext.c
HW_FILE_C='hwcontext.c'
if [ -f $FILE ]; then
    HW_END_C="hw_type_names\[\]${WS}=${WS}\{"
    HW_TYPE_C="\\\t[AV_HWDEVICE_TYPE_TOPSCODEC] = \"topscodec\","
    echo $HW_TYPE_C
    sed -E -i "/${HW_END_C}/a \
    ${HW_TYPE_C} " ${HW_FILE_C}
fi

HW_END_C2="hw_table\[\]${WS}=${WS}\{"
HW_TYPE_C2='#if CONFIG_TOPSCODEC\n\t&ff_hwcontext_type_topscodec,\n#endif'

sed -E -i "/${HW_END_C2}/a \
${HW_TYPE_C2} " ${HW_FILE_C}

#Makefile
M_END_AVTUILS="xtea.h" 
M_HEANDER_AVTUILS='\\t  hwcontext_topscodec.h \\'
M_AVTUILS='Makefile'

sed -E -i "/${M_END_AVTUILS}/a \
${M_HEANDER_AVTUILS}" ${M_AVTUILS}

#Makefile 2
M_END_AVTUILS2="OBJS-\\$\(CONFIG_CUDA\)${WS}\+=${WS}hwcontext_cuda.o"
M_OBJ_AVTUILS2='OBJS-$(CONFIG_TOPSCODEC)                += hwcontext_topscodec.o'

sed -E -i "/${M_END_AVTUILS2}/a \
${M_OBJ_AVTUILS2}" ${M_AVTUILS}

#Makefile 3
M_END_AVTUILS3="SKIPHEADERS-\\$\(HAVE_CUDA_H\)${WS}\+=${WS}hwcontext_cuda.h"
M_OBJ_AVTUILS3='SKIPHEADERS-$(CONFIG_TOPSCODEC)        += hwcontext_topscodec.h'

sed -E -i "/${M_END_AVTUILS3}/i \
${M_OBJ_AVTUILS3} " ${M_AVTUILS}

#pixdesc.c
PD_END="\[AV_PIX_FMT_CUDA\]${WS}=${WS}\{"
PD_TYPE='\\t\[AV_PIX_FMT_TOPSCODEC\] = { \n\t\t\.name = "topscodec", \n\t\t\.flags = AV_PIX_FMT_FLAG_HWACCEL, \n\t},'
PIXDESC='pixdesc.c'

sed -E  -i "/${PD_END}/i \
${PD_TYPE}" ${PIXDESC}

# for 3.2
grep "av_image_fill_plane_sizes" imgutils.h
if [ $? -ne 0 ]; then
    FUN_END='#endif'
    FUN_CON=$(cat << EOF
    int av_image_fill_plane_sizes(size_t size[4], enum AVPixelFormat pix_fmt,\n \
                              int height, const ptrdiff_t linesizes[4]);        
EOF
    )

sed -i "/${FUN_END}/i \
${FUN_CON}" imgutils.h
 
FUN_END='void av_image_fill_max_pixsteps(int max_pixsteps'
FUN_CON=$(cat << EOF
int av_image_fill_plane_sizes(size_t sizes[4], enum AVPixelFormat pix_fmt,\n \
                              int height, const ptrdiff_t linesizes[4])\n \
{\n  \
    int i, has_plane[4] = { 0 };\n \
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(pix_fmt);\n \
    memset(sizes    , 0, sizeof(sizes[0])*4);\n\
    if (!desc || desc->flags & AV_PIX_FMT_FLAG_HWACCEL)\n \
        return AVERROR(EINVAL);\n\
    if (linesizes[0] > SIZE_MAX / height)\n\
        return AVERROR(EINVAL);\n\
    sizes[0] = linesizes[0] * (size_t)height;\n\
    if (desc->flags & AV_PIX_FMT_FLAG_PAL ||\n\
        desc->flags & 0) {\n\
        sizes[1] = 256 * 4; /* palette is stored here as 256 32 bits words */\n\
        return 0;\n\
    }\n\
    for (i = 0; i < 4; i++)\n\
        has_plane[desc->comp[i].plane] = 1;\n\
    for (i = 1; i < 4 && has_plane[i]; i++) {\n\
        int h, s = (i == 1 || i == 2) ? desc->log2_chroma_h : 0;\n\
        h = (height + (1 << s) - 1) >> s;\n\
        if (linesizes[i] > SIZE_MAX / h)\n\
            return AVERROR(EINVAL);\n\
        sizes[i] = (size_t)h * linesizes[i];\n\
    } \n\
    return 0; \n\
} 
EOF
    )

sed -i "/${FUN_END}/i \
${FUN_CON}" imgutils.c

fi


#pixdesc.c 2
echo "$file" | grep "attribute_deprecated${WS}int${WS}step_minus1;"
if [ $? -eq 0 ]; then
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
            [AV_PIX_FMT_P010LE_EF] = {\n \
            .name = "p010le_ef",\n \
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
else
    PD_END2='\[AV_PIX_FMT_X2RGB10LE\] = {'
    PD_TYPE2_RGB24P=$(cat << EOF
            [AV_PIX_FMT_RGB24P] = {\n \
            .name = "rgb24p",\n \
            .nb_components = 3,\n \
            .log2_chroma_w = 0,\n \
            .log2_chroma_h = 0,\n \
            .comp = {\n \
                { 0, 1, 0, 0, 8},        /* R */\n \
                { 1, 1, 0, 0, 8},        /* G */\n \
                { 2, 1, 0, 0, 8},        /* B */\n \
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
                { 0, 1, 0, 0, 8},        /* B */\n \
                { 1, 1, 0, 0, 8},        /* G */\n \
                { 2, 1, 0, 0, 8},        /* R */\n \
            },\n \
            .flags = AV_PIX_FMT_FLAG_PLANAR | AV_PIX_FMT_FLAG_RGB,\n \
        },
EOF
    )

    PD_TYPE2_P010LE=$(cat << EOF
            [AV_PIX_FMT_P010LE_EF] = {\n \
            .name = "p010le_ef",\n \
            .nb_components = 3,\n \
            .log2_chroma_w = 1,\n \
            .log2_chroma_h = 1,\n \
            .comp = {\n \
                { 0, 2, 0, 6, 10},        /* Y */\n \
                { 1, 4, 0, 6, 10},        /* U */\n \
                { 1, 4, 2, 6, 10},        /* V */\n \
            },\n \
            .flags = AV_PIX_FMT_FLAG_PLANAR,\n \
        },
EOF
    )
fi

sed -i "/${PD_END2}/i \
${PD_TYPE2_RGB24P} ${PD_TYPE2_BGR24P} ${PD_TYPE2_P010LE}" ${PIXDESC}

#pixfmt.h
PIX_END="AV_PIX_FMT_NB"
RGB24P='\\tAV_PIX_FMT_RGB24P,     ///< planar RGB 8:8:8, 24bpp, RRR...GGG...BBB...\n'
BGR24P='\tAV_PIX_FMT_BGR24P,     ///< planar BGR 8:8:8, 24bpp, BBB...GGG...RRR...\n'
EFCODEC='\tAV_PIX_FMT_TOPSCODEC,\n'
P010LE='\tAV_PIX_FMT_P010LE_EF, ///< like NV12, with 10bpp per component, little-endian\n'

PIX_FILE='pixfmt.h'

#在AV_PIX_FMT_NB前插入
sed -i "/${PIX_END}/i \
${RGB24P}\
${BGR24P}\
${EFCODEC}\
${P010LE} " ${PIX_FILE}