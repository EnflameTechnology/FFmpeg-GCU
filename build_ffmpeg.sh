#!/bin/bash
set -eu -o pipefail
set +eu +o pipefail

FFMPEG_TAG="n4.4"
# FFMPEG_REPO=${FFMPEG_REPO:-"https://github.com/FFmpeg/FFmpeg.git"}
FFMPEG_REPO=${FFMPEG_REPO:-"http://git.enflame.cn/sw/va/FFmpeg.git"} #for debug

ffmpeg_dir=""
parallel="-j$(nproc)"
_type="release"
_pre_c_flags="-g0 -O3 -DNDEBUG"
_c_flags="-Wl,--build-id"
_ldflags="-fuse-ld=gold -ldl -lpthread -Wl,--build-id"
_arch=""

usage="Usage: $0 FFMPEG_TAG/n3.2/n4.4/n5.0 [Options]

Options:
    FFMPEG_TAG          FFMPEG git tag. (default $FFMPEG_TAG)
    -s ffmpeg_dir       ffmpeg source code dir,
    -f _c_flags          _c_flags
    -l _ldflags         _ldflags
    -j parallel         make -j parallel default is \$(nproc)
    -a arch             build with platform (x86_64, arm64, powerpc; default x86_64)
    -h                  help message

exp:
    1. $0 $FFMPEG_TAG -s /path/to/FFmpeg
"

if [ $# -lt 1 ]; then
    echo "$usage"
    exit 1
fi

if [ $1 ]; then
    FFMPEG_TAG=$1
fi


OPTIONS=$(getopt -o hc:C:X:S:bs:j:H:L:T:f:l:T:a:A:  -- "$@")
if [ $? -ne 0 ]; then
    echo "$usage"
    exit 1
fi

eval set -- "$OPTIONS"
while true; do
    case "$1" in
    -s)
        ffmpeg_dir=$(realpath $2)
        shift 2
        ;;
    -f)
        _c_flags+=" $2"
        shift 2
        ;;
    -l)
        _ldflags+=" $2"
        shift 2
        ;;
    -a)
        case $2 in
            arm64|aarch64)
                _arch="--arch=aarch64" ;;
            arm|arm32)
                _arch="--arch=arm" ;;
            powerpc*|ppc*)
                _arch="--arch=ppc" ;;
            *|x86_64|amd64)
                _arch="--arch=x86"
                _pre_c_flags+=" -m64"
                _ldflags+=" -m64"
                ;;
        esac
        shift 2
        ;;
    -j)
        if [ "$2" -eq "$2" ]; then
            parallel="-j $2"
        else
            echo "$2 should be a number, use '$parallel' by default"
        fi
        shift 2
        ;;
    -h)
    echo "--help"
        echo "$usage"
        exit 1
        ;;
    --)
        shift
        break
        ;;
    esac
done

_whole_c_flags="$_pre_c_flags $_c_flags"
echo "cflags: $_whole_c_flags"
echo "ldflags: $_ldflags"

build_path=$(dirname $(readlink -f "$0"))/build_${FFMPEG_TAG}
src_path=$(dirname $(readlink -f $0))
echo "current file path: ${src_path}"
echo "build_path: ${build_path}"

if [ ! -d ${build_path} ]; then
    echo "mkdir -p ${build_path}"
    mkdir -p ${build_path}
fi

echo "ffmpeg_dir: ${ffmpeg_dir}"
echo "FFMPEG_TAG: ${FFMPEG_TAG}"
echo "parallel: ${parallel}"

if [ -z $ffmpeg_dir ]; then
    ffmpeg_dir=${build_path}/FFmpeg-${FFMPEG_TAG}
    find ${build_path} -mindepth 1 -maxdepth 1 ! -name "FFmpeg-${FFMPEG_TAG}" -exec rm -rf {} +
    if [ ! -d $ffmpeg_dir ]; then
        echo "download ffmpeg_dir"
        # git clone -b ${FFMPEG_TAG} $FFMPEG_REPO ${ffmpeg_dir}
        git clone  $FFMPEG_REPO ${ffmpeg_dir}
        pushd ${ffmpeg_dir}
        echo "checkout ${FFMPEG_TAG}"
        git checkout ${FFMPEG_TAG}
        popd
    fi
else
    rm -rf ${build_path}
    echo "rm -rf ffmpeg_dir: ${build_path}"
fi

current_date=$(date +%Y%m%d%H%M%S)
build_ffmpeg=${build_path}/FFmpeg-${FFMPEG_TAG}_with_gcu_patch_${current_date}
mkdir -p ${build_ffmpeg}
echo "build_ffmpeg: ${build_ffmpeg}"

if [ -d $ffmpeg_dir ]; then
    echo "copy raw ffmpeg to FFmpeg-${FFMPEG_TAG}"
    cp -r /$ffmpeg_dir/* ${build_ffmpeg}/
else
    echo "FFmpeg need to be cloned to $ffmpeg_dir"
    exit 1
fi

# rename FFmpeg source code dir
ffmpeg_dir=$build_ffmpeg
echo "copy FFmpeg GCU Plugin files info FFmpeg source tree"
cd $ffmpeg_dir

cp ${src_path}/src/configure* ${ffmpeg_dir}/
pushd ${ffmpeg_dir}/
echo "add tops hwaccel to configure"
${ffmpeg_dir}/configure_insert.sh # add hwaccel to configure
popd

#replace cavsvideodec.c
cp ${src_path}/src/libavformat/* ${ffmpeg_dir}/libavformat/
pushd ${ffmpeg_dir}/libavformat/
echo "add CAVS_PROFILE_GUANDIAN to cavsvideodec.c"
${ffmpeg_dir}/libavformat/cavsvideodec_insert.sh # add CAVS_PROFILE_GUANDIAN to cavsvideodec.c
popd

cp ${src_path}/src/libavcodec/* ${ffmpeg_dir}/libavcodec/
pushd ${ffmpeg_dir}/libavcodec
echo "add codec to allcodecs.c"
${ffmpeg_dir}/libavcodec/avcodec_insert.sh # add codec to allcodecs.c
popd

cp ${src_path}/src/libavutil/* ${ffmpeg_dir}/libavutil/
pushd ${ffmpeg_dir}/libavutil
echo "add pixfmt to pixdesc.c"
${ffmpeg_dir}/libavutil/avutil_insert.sh # add pixfmt to pixdesc.c
popd

# for 4.x 5.0
cp ${src_path}/src/examples/* ${ffmpeg_dir}/doc/examples/
pushd ${ffmpeg_dir}/doc/examples/
echo "add hw_decode_tops to examples"
${ffmpeg_dir}/doc/examples/example_insert.sh # add hw_decode_tops to examples
popd

# for 3.x
cp ${src_path}/src/examples/* ${ffmpeg_dir}/doc/
pushd ${ffmpeg_dir}/doc/
echo "add hw_decode_tops to examples"
${ffmpeg_dir}/doc/example_insert.sh # add hw_decode_tops to examples
popd

echo "configure FFmpeg"  #--toolchain=gcc-asan \   
./configure \
    --prefix=${build_path}/ffmpeg_gcu \
    --extra-cflags="$_whole_c_flags" \
    --extra-ldflags="$_ldflags" \
    --disable-stripping \
    --disable-optimizations \
    --disable-x86asm \
    --enable-pic \
    --enable-swscale \
    --enable-topscodec \
    --enable-decoder=vc1_topscodec \
    --enable-decoder=av1_topscodec \
    --enable-decoder=h264_topscodec \
    --enable-decoder=hevc_topscodec \
    --enable-decoder=vp8_topscodec \
    --enable-decoder=vp9_topscodec \
    --enable-decoder=mpeg4_topscodec \
    --enable-decoder=mpeg2_topscodec \
    --enable-decoder=mjpeg_topscodec \
    --enable-decoder=h263_topscodec \
    --enable-decoder=avs_topscodec \
    --enable-decoder=avs2_topscodec \
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

pushd ${ffmpeg_dir}/doc/examples/
echo "add hw_decode_tops to examples"
${ffmpeg_dir}/doc/examples/example_insert.sh # add hw_decode_tops to examples
popd

echo "make examples"
example_path=${ffmpeg_dir}/doc/examples
make examples
if [ $? -ne 0 ]; then
    echo "make examples failed"
    exit 1
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

echo "copy hw_decode_multi_tops"
cp ${ffmpeg_dir}/doc/examples/hw_decode_multi_tops ${build_path}/ffmpeg_gcu/bin

echo "build ffmpeg gcu done"

# Create Debian package
echo "Creating Debian package"

# 定义需要检查的包列表
PACKAGES=("build-essential" "devscripts" "debhelper" "dh-make")
# 检查并安装包
sudo apt update
for pkg in "${PACKAGES[@]}"; do
    # 使用 dpkg-query 检查包是否已安装
    if dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q "ok installed"; then
        echo "$pkg 已经安装，跳过安装步骤。"
    else
        echo "$pkg 未安装，正在安装..."
        sudo apt install -y "$pkg"
    fi
done

# sudo apt update
# echo y | sudo apt install build-essential devscripts debhelper dh-make

# Define package variables
PACKAGE_NAME="ffmpeg-gcu"
PACKAGE_VERSION=1.0
PACKAGE_ARCH="amd64"
PACKAGE_DESCRIPTION="FFmpeg with GCU support"
PACKAGE_MAINTAINER="zhencheng.cai@enflame-tech.com"

# Create directory structure
DEB_DIR="${build_path}/ffmpeg_gcu"
mkdir -p ${DEB_DIR}/DEBIAN

# Create control file
cat <<EOF > ${DEB_DIR}/DEBIAN/control
Package: ${PACKAGE_NAME}
Version: ${PACKAGE_VERSION}
Section: base
Priority: optional
Architecture: ${PACKAGE_ARCH}
Maintainer: ${PACKAGE_MAINTAINER}
Description: ${PACKAGE_DESCRIPTION}
EOF

# Build the package
dpkg-deb --build ${DEB_DIR} ${DEB_DIR}/${PACKAGE_NAME}_${PACKAGE_VERSION}_tag_${FFMPEG_TAG}_${PACKAGE_ARCH}.deb

echo "all done"