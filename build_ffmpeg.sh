#!/bin/bash
set -eu -o pipefail
set +eu +o pipefail

FFMPEG_TAG=${FFMPEG_TAG:-"n7.1"}
FFMPEG_REPO=${FFMPEG_REPO:-"https://github.com/FFmpeg/FFmpeg.git"}
# FFMPEG_REPO=${FFMPEG_REPO:-"http://git.enflame.cn/sw/va/FFmpeg.git"} # for internal
build_path=$(dirname $(readlink -f "$0"))/build
cache_tool=""
no_cache=false
sysroot=""
c_compiler="gcc"
cxx_compiler="g++"
compiler_ar="ar"
ffmpeg_dir=""
build_only=false
parallel="-j$(nproc)"
_type="release"
_pre_c_flags="-g0 -O3 -DNDEBUG"
_c_flags="-Wl,--build-id"
_ldflags="-fuse-ld=gold -ldl -lpthread -Wl,--build-id"
_arch=""
_custom_configure_options=''
_disable_asm=false
package_format="deb"
# _ldflags="-fuse-ld=gold -m64 -ldl -lpthread"

usage="Usage: $0 [FFMPEG_TAG [build_path]] [Options]

Options:
    FFMPEG_TAG          FFMPEG git tag. (default $FFMPEG_TAG)
    build_path          Target folder to build. (default $build_path)
    -b                  build only
    -c cache_tool       ccache or sccache
    -C c-compiler       c compiler
    -X cxx-compier      cxx compiler
    -A compiler_ar      compiler ar tool
    -S sysroot          sysroot
    -s ffmpeg_dir       ffmpeg source code dir
    -j parallel         make -j parallel default is \$(nproc)
    -a arch             build with platform (x86_64, arm64, powerpc; default x86_64)
    -P package_format   package format: deb, rpm, both, none, auto. (default deb)
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

while getopts ':hc:C:X:S:bs:j:H:L:f:l:T:a:A:P:' opt; do
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
    A)
        compiler_ar="$OPTARG"
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
            _pre_c_flags="-O0 -g"
            ;;
        RelWithDebInfo)
            _pre_c_flags="-O3 -g -DNDEBUG -gsplit-dwarf"
            _ldflags+=" -Wl,--gdb-index"
            no_cache=true
            ;;
        MinSizeRel)
            _pre_c_flags="-Os -g0 -DNDEBUG"
            ;;
        [Cc]overage)
            _c_flags+=" -fprofile-arcs -ftest-coverage"
            _ldflags+=" -lgcov --coverage"
            ;;
        [Aa]san)
            _c_flags+=" -g -fsanitize=address -fno-omit-frame-pointer -fno-optimize-sibling-calls"
            _ldflags+=" -fsanitize=address -shared-libsan"
            _disable_asm=true
            ;;
        [Tt]san)
            _c_flags+=" -g -fsanitize=thread -fno-omit-frame-pointer -fno-optimize-sibling-calls"
            _ldflags+=" -fsanitize=thread"
            _disable_asm=true
            ;;
        * | [Rr]elease)
            _pre_c_flags="-O3 -g0 -DNDEBUG"
            ;;
        esac
        ;;
    a)
        case $OPTARG in
        arm64 | aarch64)
            _arch="--arch=aarch64"
            _custom_configure_options+='--enable-cross-compile '
            ;;
        arm | arm32)
            _arch="--arch=arm"
            _custom_configure_options+='--enable-cross-compile '
            ;;
        powerpc* | ppc*)
            _arch="--arch=ppc"
            _custom_configure_options+='--enable-cross-compile '
            ;;
        * | x86_64 | amd64)
            _arch="--arch=x86"
            _pre_c_flags+=" -m64"
            _ldflags+=" -m64"
            ;;
        esac
        ;;
    j)
        if [ "$OPTARG" -eq "$OPTARG" ]; then
            parallel="-j $OPTARG"
        else
            echo "$OPTARG should be a number, use '$parallel' by default"
        fi
        ;;
    P)
        package_format="$OPTARG"
        ;;
    ? | h)
        echo "$usage"
        exit 1
        ;;
    esac
done

if [ "${FFMPEG_GCU_SKIP_PACKAGING:-0}" = "1" ]; then
    package_format="none"
fi

case "$package_format" in
    deb | rpm | both | none | auto) ;;
    *)
        echo "Unsupported package format '$package_format'. Use deb, rpm, both, none, or auto."
        exit 1
        ;;
esac

detect_package_format() {
    local os_id=""
    local os_id_like=""

    if [ -r /etc/os-release ]; then
        os_id=$(awk -F= '$1 == "ID" { gsub(/\"/, "", $2); print tolower($2) }' /etc/os-release)
        os_id_like=$(awk -F= '$1 == "ID_LIKE" { gsub(/\"/, "", $2); print tolower($2) }' /etc/os-release)
    fi

    case " $os_id $os_id_like " in
        *" tlinux "* | *" rhel "* | *" centos "* | *" fedora "* | *" opencloudos "*)
            echo "rpm"
            ;;
        *" debian "* | *" ubuntu "*)
            echo "deb"
            ;;
        *)
            echo "Unable to detect package format from /etc/os-release. Use -P deb or -P rpm." >&2
            return 1
            ;;
    esac
}

if [ "$package_format" = "auto" ]; then
    package_format=$(detect_package_format) || exit 1
fi

get_deb_arch() {
    case "$_arch" in
        *aarch64*) echo "arm64" ;;
        *arm*)     echo "armhf" ;;
        *ppc*)     echo "ppc64el" ;;
        *)         echo "amd64" ;;
    esac
}

get_rpm_arch() {
    case "$_arch" in
        *aarch64*) echo "aarch64" ;;
        *arm*)     echo "armv7hl" ;;
        *ppc*)     echo "ppc64le" ;;
        *)         echo "x86_64" ;;
    esac
}

clean_package_artifacts() {
    local package_output_dir="${build_path}/packages"
    local legacy_package_output_dir="${build_path}/ffmpeg_gcu"

    rm -f "${package_output_dir}"/*.deb "${package_output_dir}"/*.rpm
    rm -f "${legacy_package_output_dir}"/*.deb "${legacy_package_output_dir}"/*.rpm
}

make_deb_package() {
    local package_name="$1"
    local package_version="$2"
    local package_description="$3"
    local package_arch
    local deb_dir="${build_path}/ffmpeg_gcu"
    local package_output_dir="${build_path}/packages"

    if ! command -v dpkg-deb >/dev/null 2>&1; then
        echo "dpkg-deb is required to create Debian packages."
        return 1
    fi

    package_arch=$(get_deb_arch)

    echo "Creating Debian package"
    mkdir -p "${deb_dir}/DEBIAN"
    mkdir -p "${package_output_dir}"

    cat <<EOF > "${deb_dir}/DEBIAN/control"
Package: ${package_name}
Version: ${package_version}
Section: base
Priority: optional
Architecture: ${package_arch}
Maintainer: Enflame-Tech
Description: ${package_description}
EOF

    if ! dpkg-deb --build "${deb_dir}" \
        "${package_output_dir}/${package_name}_${FFMPEG_TAG}_${package_arch}.deb"; then
        echo "dpkg-deb failed"
        return 1
    fi
    echo "Debian package created"
}

make_rpm_package() {
    local package_name="$1"
    local package_version="$2"
    local package_description="$3"
    local package_arch
    local rpm_topdir="${build_path}/rpmbuild"
    local package_root="${build_path}/ffmpeg_gcu"
    local package_output_dir="${build_path}/packages"
    local tar_name="${package_name}-${package_version}"
    local spec_file="${rpm_topdir}/SPECS/${package_name}.spec"

    if ! command -v rpmbuild >/dev/null 2>&1; then
        echo "rpmbuild is required to create RPM packages."
        return 1
    fi

    package_arch=$(get_rpm_arch)

    echo "Creating RPM package"
    rm -rf "${rpm_topdir}"
    mkdir -p "${rpm_topdir}"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
    mkdir -p "${package_output_dir}"
    tar -C "${package_root}" --exclude=DEBIAN --exclude="*.deb" --exclude="*.rpm" -czf \
        "${rpm_topdir}/SOURCES/${tar_name}.tar.gz" .

    cat <<EOF > "${spec_file}"
Name: ${package_name}
Version: ${package_version}
Release: 1
Summary: ${package_description}
License: LGPLv2.1+
Vendor: Enflame-Tech
BuildArch: ${package_arch}
Source0: %{name}-%{version}.tar.gz

%description
${package_description}

%prep
%setup -q -c

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}
cp -a . %{buildroot}/
find %{buildroot} \( -type f -o -type l \) -printf '/%%P\n' > %{_builddir}/%{name}.files

%files -f %{_builddir}/%{name}.files
%defattr(-,root,root,-)
EOF

    if ! rpmbuild --define "_topdir ${rpm_topdir}" --target "${package_arch}" -bb "${spec_file}"; then
        echo "rpmbuild failed"
        return 1
    fi
    if ! cp "${rpm_topdir}/RPMS/${package_arch}/${package_name}-${package_version}-1.${package_arch}.rpm" \
        "${package_output_dir}/${package_name}-${FFMPEG_TAG}-1.${package_arch}.rpm"; then
        echo "copy RPM package failed"
        return 1
    fi
    echo "RPM package created"
}

$no_cache && cache_tool=''

_whole_c_flags="$_pre_c_flags $_c_flags $(pkg-config --cflags --libs fftopscodec)"
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

gcc_basever=$($c_compiler -dumpversion)
gcc_major=$(echo $gcc_basever | cut -d. -f1)

if [ "$gcc_major" -lt 5 ]; then
    compat_header_dir="$src_path/src/compat/gcc4"
    if [ -d "$compat_header_dir" ]; then
        _whole_c_flags="$_whole_c_flags -isystem $compat_header_dir"
    fi
fi

_whole_c_flags="$_whole_c_flags -I${src_path}/include"

if [ "$FFMPEG_TAG" != "n3.2" ]; then
    _custom_configure_options+='--enable-decoder=av1 '
    _custom_configure_options+='--enable-decoder=av1_topscodec '
    _custom_configure_options+='--enable-decoder=avs_topscodec '
    _custom_configure_options+='--enable-decoder=avs2_topscodec '
    if $_disable_asm; then
        _custom_configure_options+='--disable-asm '
        _custom_configure_options+='--disable-x86asm '
    fi
fi

if [ "$FFMPEG_TAG" == "n3.2" ]; then
    _custom_configure_options+='--disable-asm '
    _custom_configure_options+='--disable-yasm '
fi

if echo '#include "tops/tops_codec.h"
topscodecEncCaps_t x;' | $c_compiler -fsyntax-only -x c - $_whole_c_flags >/dev/null 2>&1; then
    echo "topscodec encode types available, enabling topscodec encode support"
    _whole_c_flags="$_whole_c_flags -DTOPSCODEC_HAS_ENCODE=1"
else
    echo "topscodec encode types not available, disabling topscodec encoders and encode examples"
    _custom_configure_options+='--disable-encoder=h264_topscodec_enc '
    _custom_configure_options+='--disable-encoder=hevc_topscodec_enc '
    _custom_configure_options+='--disable-encoder=vp8_topscodec_enc '
    _custom_configure_options+='--disable-encoder=vp9_topscodec_enc '
    _custom_configure_options+='--disable-encoder=mjpeg_topscodec_enc '
    _custom_configure_options+='--disable-encode_tops_example '
    _custom_configure_options+='--disable-multi_encode_tops_example '
    _custom_configure_options+='--disable-hw_encode_tops_example '
fi

cd $build_path

echo "configure FFmpeg"  #--toolchain=gcc-asan \
${ffmpeg_dir}/configure \
    --prefix=${build_path}/ffmpeg_gcu \
    --cc="${cache_tool}$c_compiler" \
    --cxx="${cache_tool}$cxx_compiler" \
    --ar="$compiler_ar" \
    $sysroot \
    $_arch \
    --extra-cflags="$_whole_c_flags" \
    --extra-ldflags="$_ldflags" \
    --disable-stripping \
    --disable-optimizations \
    --enable-pic \
    --enable-swscale \
    --enable-topscodec \
    --enable-decoder=vc1_topscodec \
    --enable-decoder=h264_topscodec \
    --enable-decoder=hevc_topscodec \
    --enable-decoder=vp8_topscodec \
    --enable-decoder=vp9_topscodec \
    --enable-decoder=mpeg4_topscodec \
    --enable-decoder=mpeg2_topscodec \
    --enable-decoder=mjpeg_topscodec \
    --enable-decoder=h263_topscodec \
    --enable-static \
    --enable-shared \
    $_custom_configure_options

if [ $? -ne 0 ]; then
    echo "configure failed"
    exit 1
fi

make clean

echo "make"
if command -v bear >/dev/null 2>&1; then
    bear make $parallel
else
    make $parallel
fi

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
cp ${build_path}/doc/examples/hw_decode_tops ${build_path}/ffmpeg_gcu/bin

echo "copy decode_tops"
cp ${build_path}/doc/examples/decode_tops ${build_path}/ffmpeg_gcu/bin

echo "copy hw_decode_multi_tops"
cp ${build_path}/doc/examples/hw_decode_multi_tops ${build_path}/ffmpeg_gcu/bin

echo "copy encode_tops"
cp ${build_path}/doc/examples/encode_tops ${build_path}/ffmpeg_gcu/bin
cp ${build_path}/doc/examples/encode_tops_g ${build_path}/ffmpeg_gcu/bin

echo "copy hw_encode_tops"
cp ${build_path}/doc/examples/hw_encode_tops ${build_path}/ffmpeg_gcu/bin
cp ${build_path}/doc/examples/hw_encode_tops_g ${build_path}/ffmpeg_gcu/bin

echo "copy multi_encode_tops"
cp ${build_path}/doc/examples/multi_encode_tops ${build_path}/ffmpeg_gcu/bin
cp ${build_path}/doc/examples/multi_encode_tops_g ${build_path}/ffmpeg_gcu/bin

PACKAGE_NAME="ffmpeg-gcu"
PACKAGE_VERSION="${FFMPEG_TAG#n}"
PACKAGE_DESCRIPTION="FFmpeg with GCU support"

if [ "$package_format" != "none" ]; then
    clean_package_artifacts
fi

case "$package_format" in
    none)
        echo "Package creation skipped"
        ;;
    deb)
        make_deb_package "$PACKAGE_NAME" "$PACKAGE_VERSION" "$PACKAGE_DESCRIPTION" || exit 1
        ;;
    rpm)
        make_rpm_package "$PACKAGE_NAME" "$PACKAGE_VERSION" "$PACKAGE_DESCRIPTION" || exit 1
        ;;
    both)
        make_deb_package "$PACKAGE_NAME" "$PACKAGE_VERSION" "$PACKAGE_DESCRIPTION" || exit 1
        make_rpm_package "$PACKAGE_NAME" "$PACKAGE_VERSION" "$PACKAGE_DESCRIPTION" || exit 1
        ;;
esac

echo "build ffmpeg gcu done"
