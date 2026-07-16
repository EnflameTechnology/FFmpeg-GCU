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
    PD_END2='\[AV_PIX_FMT_RGB24\] = {'
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
     PD_TYPE2_P010LE_LSB=$(cat << EOF
        [AV_PIX_FMT_P010LE_LSB] = {\n \
        .name = "p010le_lsb",\n \
        .nb_components = 3,\n \
        .log2_chroma_w = 1,\n \
        .log2_chroma_h = 1,\n \
        .comp = {\n \
            { 0, 2, 0, 0, 10 },        /* Y */\n \
            { 1, 4, 0, 0, 10 },        /* U */\n \
            { 1, 4, 2, 0, 10 },        /* V */\n \
        }, \n \
        .flags = AV_PIX_FMT_FLAG_PLANAR,\n \
    },
EOF
    )

else
    PD_END2='\[AV_PIX_FMT_RGB24\] = {'
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

    PD_TYPE2_P010LE_LSB=$(cat << EOF
        [AV_PIX_FMT_P010LE_LSB] = {\n \
        .name = "p010le_lsb",\n \
        .nb_components = 3,\n \
        .log2_chroma_w = 1,\n \
        .log2_chroma_h = 1,\n \
        .comp = {\n \
            { 0, 2, 0, 0, 10 },        /* Y */\n \
            { 1, 4, 0, 0, 10 },        /* U */\n \
            { 1, 4, 2, 0, 10 },        /* V */\n \
        }, \n \
        .flags = AV_PIX_FMT_FLAG_PLANAR,\n \
    },
EOF
    )

fi

sed -i "/${PD_END2}/i \
${PD_TYPE2_RGB24P} ${PD_TYPE2_BGR24P} ${PD_TYPE2_P010LE_LSB}" ${PIXDESC}

#pixfmt.h
PIX_END="AV_PIX_FMT_NB"
RGB24P='\\tAV_PIX_FMT_RGB24P,     ///< planar RGB 8:8:8, 24bpp, RRR...GGG...BBB...\n'
BGR24P='\tAV_PIX_FMT_BGR24P,     ///< planar BGR 8:8:8, 24bpp, BBB...GGG...RRR...\n'
P010LE_LSB='\tAV_PIX_FMT_P010LE_LSB,     ///< Semi-planar P010LE,LSB, 10bit\n'
EFCODEC='\tAV_PIX_FMT_TOPSCODEC,\n'

PIX_FILE='pixfmt.h'

#在AV_PIX_FMT_NB前插入
sed -i "/${PIX_END}/i \
${RGB24P}\
${BGR24P}\
${P010LE_LSB}\
${EFCODEC}" ${PIX_FILE}

# stride_align
# # FFmpeg 7.0 imgutils.c 路径（请确保路径正确，若不同请修改）
# FILE="imgutils.c"
# TAG_LINE="ret = av_image_fill_plane_sizes(sizes, pix_fmt, height, aligned_linesize);"

# # 要插入的新对齐逻辑（保持 FFmpeg 4空格缩进风格）
# AV_IMAGE_GET_BUFFER_SIZE=$(cat << 'EOF'
#     // 核心修正：按像素格式的抽样比例，基于 Y 平面对齐所有平面
#     int valid_planes = av_pix_fmt_count_planes(pix_fmt);
#     if (valid_planes > 0) {
#         // 步骤 1:先对齐 Y 平面（第 0 平面）
#         aligned_linesize[0] = FFALIGN(linesize[0], align);
#         if (aligned_linesize[0] < linesize[0])
#             return AVERROR(EINVAL);

#         // 步骤 2:根据像素格式的色度抽样比例，对齐 UV 平面
#         // 关键:获取色度宽度缩放比例(log2_chroma_w)→ 2^log2_chroma_w = 缩放分母
#         int log2_chroma_w = desc->log2_chroma_w;
#         int chroma_w_scale = 1 << log2_chroma_w; // 如 YUV420P 的 log2_chroma_w=1 → scale=2

#         // 遍历 UV 平面(i=1,2,仅处理有效平面)
#         for (i = 1; i < valid_planes; i++) {
#             // 核心逻辑:UV linesize = 对齐后的 Y linesize / 缩放比例（保持抽样比例）
#             aligned_linesize[i] = aligned_linesize[0] / chroma_w_scale;

#             // 安全校验 1:UV linesize 不能小于原始 linesize(避免数据截断)
#             if (aligned_linesize[i] < linesize[i])
#                 return AVERROR(EINVAL);

#             // 安全校验 2:UV linesize 需是「单分量字节数」的整数倍(避免像素错位)
#             // 单分量字节数 = 每个像素分量的存储字节数(如 8bit=1 字节,10bit=2 字节)
#             int comp_bits = desc->comp[i].depth;
#             int comp_bytes = (comp_bits + 7) / 8; // 向上取整（如 10bit→2 字节）
#             if (aligned_linesize[i] % comp_bytes != 0) {
#                 // 若不满足，向上取整到最近的 comp_bytes 整数倍
#                 aligned_linesize[i] = FFALIGN(aligned_linesize[i], comp_bytes);
#             }
#         }
#     }
# EOF
# )

# if grep -qF "$TAG_LINE" "$FILE"; then
#    # Insert ADD_LINE before the matching line using temporary file
#    TMP_FILE=$(mktemp)
#    echo "$AV_IMAGE_GET_BUFFER_SIZE" > "$TMP_FILE"
#    awk -v tmpfile="$TMP_FILE" '/ret = av_image_fill_plane_sizes\(sizes, pix_fmt, height, aligned_linesize\);/ {while ((getline line < tmpfile) > 0) print line; close(tmpfile)} 1' ${FILE} > ${FILE}.tmp && mv ${FILE}.tmp ${FILE}
#    rm -f "$TMP_FILE"
# fi