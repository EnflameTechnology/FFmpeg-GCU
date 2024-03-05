#!/bin/bash
set -eu -o pipefail
set +eu +o pipefail

FFMPEG_TAG=${FFMPEG_TAG:-"n4.5-dev"}
FFMPEG_REPO=${FFMPEG_REPO:-"https://github.com/FFmpeg/FFmpeg.git"}
build_path=$(dirname $(readlink -f "$0"))/build
cache_tool=""
sysroot=""
c_compiler="gcc"
cxx_compiler="g++"
ffmpeg_dir=""
build_only=false
parallel="-j$(nproc)"
_type="release"
_pre_c_flags="-g0 -O3 -DNDEBUG"
_c_flags=""
_ldflags="-fuse-ld=gold -m64 -ldl -lpthread"

usage="Usage: $0 [FFMPEG_TAG [build_path]] [Options]

Options:
    FFMPEG_TAG          FFMPEG git tag. (default $FFMPEG_TAG)
    build_path          Target folder to build. (default $build_path)
    -b                  build only
    -c cache_tool       ccache or sccache
    -C c-compiler       c compiler
    -X cxx-compier      cxx compiler
    -S sysroot          sysroot
    -s ffmpeg_dir       ffmpeg source code dir
    -j parallel         make -j parallel default is \$(nproc)
    -h                  help message

exp:
    1. $0 $FFMPEG_TAG $build_path
    2. $0 -c ccache
"

if [ $2 ]; then
    for _ff in FFMPEG_TAG build_path; do
        if [ "${1::1}" = "-" ]; then
            break
        else
            eval $_ff="$1"
            shift
        fi
    done
fi

while getopts ':hc:C:X:S:bs:j:H:L:T:f:l:T:' opt; do
    case "$opt" in
    b)
        build_only=true
        ;;
    c)
        # space here is necessary
        cache_tool="$OPTARG "
        ;;
    C)
        c_compiler="$OPTARG"
        ;;
    X)
        cxx_compiler="$OPTARG"
        ;;
    S)
        sysroot="--sysroot=$OPTARG"
        ;;
    s)
        ffmpeg_dir=$(realpath $OPTARG)
        ;;
    f)
        _c_flags+=" $OPTARG"
        ;;
    l)
        _ldflags+=" $OPTARG"
        ;;
    T)
        case $OPTARG in
            [Dd]ebug)
                _pre_c_flags="-O0 -g" ;;
            RelWithDebInfo)
                _pre_c_flags="-O3 -g -DNDEBUG" ;;
            MinSizeRel)
                _pre_c_flags="-Os -g0 -DNDEBUG" ;;
            [Cc]overage)
                _c_flags+=" -fprofile-arcs -ftest-coverage"
                _ldflags+=" -lgcov --coverage"
                ;;
            [Aa]san)
                _c_flags+=" -g -fsanitize=address -fno-omit-frame-pointer -fno-optimize-sibling-calls"
                _ldflags+=" -fsanitize=address -shared-libsan"
                ;;
            [Tt]san)
                _c_flags+=" -g -fsanitize=thread -fno-omit-frame-pointer -fno-optimize-sibling-calls"
                _ldflags+=" -fsanitize=thread"
                ;;
            * | [Rr]elease)
                _pre_c_flags="-O3 -g0 -DNDEBUG" ;;
        esac
        ;;
    j)
        if [ "$OPTARG" -eq "$OPTARG" ]; then
            parallel="-j $OPTARG"
        else
            echo "$OPTARG should be a number, use '$parallel' by default"
        fi
        ;;
    ? | h)
        echo "$usage"
        exit 1
        ;;
    esac
done

_whole_c_flags="$_pre_c_flags $_c_flags"
echo "cflags: $_whole_c_flags"
echo "ldflags: $_ldflags"

src_path=$(dirname $(readlink -f $0))
echo "current file path: ${src_path}"
echo "build_path: ${build_path}"
echo "ffmpeg_dir: ${ffmpeg_dir}"

if [ -z $ffmpeg_dir ]; then
    echo "delete old FFmpeg files"
    rm -rf ${build_path}/FFmpeg-${FFMPEG_TAG}
    rm -rf ${build_path}/ffmpeg_gcu

    echo "download FFmpeg-${FFMPEG_TAG}"
    git clone -b ${FFMPEG_TAG} $FFMPEG_REPO ${build_path}/FFmpeg-${FFMPEG_TAG}
    ffmpeg_dir=${build_path}/FFmpeg-${FFMPEG_TAG}
elif ! [ -d $ffmpeg_dir ]; then
    echo "FFmpeg need to be cloned to $ffmpeg_dir"
    exit 1
fi

echo "copy FFmpeg GCU Plugin files info FFmpeg source tree"
cd $ffmpeg_dir
cp ${src_path}/src/configure ${ffmpeg_dir}/
cp ${src_path}/src/libavformat/* ${ffmpeg_dir}/libavformat/
cp ${src_path}/src/libavcodec/* ${ffmpeg_dir}/libavcodec/
cp ${src_path}/src/libavutil/* ${ffmpeg_dir}/libavutil/
cp ${src_path}/src/examples/* ${ffmpeg_dir}/doc/examples/

echo "configure FFmpeg"
./configure \
    --prefix=${build_path}/ffmpeg_gcu \
    --cc="${cache_tool}$c_compiler" \
    --cxx="${cache_tool}$cxx_compiler" \
    $sysroot \
    --extra-cflags="$_whole_c_flags" \
    --extra-ldflags="$_ldflags" \
    --disable-stripping \
    --disable-x86asm \
    --disable-decoders \
    --disable-optimizations \
    --enable-pic \
    --enable-swscale \
    --enable-nvdec \
    --enable-cuvid \
    --enable-topscodec \
    --enable-decoder=vc1 \
    --enable-decoder=vc1_topscodec \
    --enable-decoder=vc1_cuvid \
    --enable-decoder=av1 \
    --enable-decoder=av1_topscodec \
    --enable-decoder=av1_cuvid \
    --enable-decoder=h264 \
    --enable-decoder=h264_topscodec \
    --enable-decoder=h264_cuvid \
    --enable-decoder=hevc \
    --enable-decoder=hevc_topscodec \
    --enable-decoder=hevc_cuvid \
    --enable-decoder=vp8 \
    --enable-decoder=vp8_topscodec \
    --enable-decoder=vp8_cuvid \
    --enable-decoder=vp9 \
    --enable-decoder=vp9_topscodec \
    --enable-decoder=vp9_cuvid \
    --enable-decoder=mpeg4 \
    --enable-decoder=mpeg4_topscodec \
    --enable-decoder=mpeg4_cuvid \
    --enable-decoder=mpeg2video \
    --enable-decoder=mpeg2_topscodec \
    --enable-decoder=mpeg2_cuvid \
    --enable-decoder=mjpeg \
    --enable-decoder=mjpeg_topscodec \
    --enable-decoder=mjpeg_cuvid \
    --enable-decoder=h263_topscodec \
    --enable-decoder=avs_topscodec \
    --enable-decoder=avs2_topscodec \
    --enable-decoder=mpeg1_cuvid \
    --enable-static \
    --enable-shared

if [ $? -ne 0 ]; then
    echo "configure failed"
    exit 1
fi

make clean

echo "make"
make $parallel
if [ $? -ne 0 ]; then
    echo "make failed"
    exit 1
fi

echo "make examples"
make examples -j
if [ $? -ne 0 ]; then
    echo "make examples failed"
    exit 1
fi

if $build_only; then
    exit 0
fi

echo "make install"
make install
if [ $? -ne 0 ]; then
    echo "make install failed"
    exit 1
fi

echo "copy hw_decode_tops"
cp ${ffmpeg_dir}/doc/examples/hw_decode_tops ${build_path}/ffmpeg_gcu/bin

echo "copy decode_tops"
cp ${ffmpeg_dir}/doc/examples/decode_tops ${build_path}/ffmpeg_gcu/bin

echo "build ffmpeg gcu done"
