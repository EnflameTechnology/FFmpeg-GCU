include_guard(GLOBAL)

set(ffmpeg_gcu_prefix ${CMAKE_BINARY_DIR}/_cmake_ffmpeg_gcu)
set(ffmpeg_gcu_src_dir ${FFMPEG_SRC_FOR_BUILD_PATH})

unset(configure_options)
list(APPEND configure_options -s ${ffmpeg_gcu_src_dir})
list(APPEND configure_options -a ${CMAKE_SYSTEM_PROCESSOR})

if(cache_tool)
    list(APPEND configure_options -c ${cache_tool})
endif()
if(CMAKE_C_COMPILER)
    list(APPEND configure_options -C ${CMAKE_C_COMPILER})
endif()

if(CMAKE_CXX_COMPILER)
    list(APPEND configure_options -X ${CMAKE_CXX_COMPILER})
endif()

if(PARALLEL)
    list(APPEND configure_options -j ${PARALLEL})
endif()

list(APPEND configure_options -T ${CMAKE_BUILD_TYPE})
if(SANITIZER)
    if ("${SANITIZER}" STREQUAL "address")
        list(APPEND configure_options -T asan)
    elseif ("${SANITIZER}" STREQUAL "thread")
        list(APPEND configure_options -T tsan)
    endif()
elseif(ENABLE_CODE_COVERAGE)
    list(APPEND configure_options -T Coverage)
endif()

if(CMAKE_SYSROOT)
    list(APPEND configure_options -S ${CMAKE_SYSROOT})
endif()

set(OUTPUT_DIR ${ffmpeg_gcu_prefix}/ffmpeg_gcu)

set(ffmpeg_build_outputs
    ${OUTPUT_DIR}/lib/libavcodec.so
    ${OUTPUT_DIR}/lib/libavcodec.so.58
    ${OUTPUT_DIR}/lib/libavcodec.so.58.135.100
    ${OUTPUT_DIR}/lib/libavdevice.so
    ${OUTPUT_DIR}/lib/libavdevice.so.58
    ${OUTPUT_DIR}/lib/libavdevice.so.58.14.100
    ${OUTPUT_DIR}/lib/libavfilter.so
    ${OUTPUT_DIR}/lib/libavfilter.so.7
    ${OUTPUT_DIR}/lib/libavfilter.so.7.111.100
    ${OUTPUT_DIR}/lib/libavformat.so
    ${OUTPUT_DIR}/lib/libavformat.so.58
    ${OUTPUT_DIR}/lib/libavformat.so.58.77.100
    ${OUTPUT_DIR}/lib/libavutil.so
    ${OUTPUT_DIR}/lib/libavutil.so.56
    ${OUTPUT_DIR}/lib/libavutil.so.56.71.100
    ${OUTPUT_DIR}/lib/libswresample.so
    ${OUTPUT_DIR}/lib/libswresample.so.3
    ${OUTPUT_DIR}/lib/libswresample.so.3.10.100
    ${OUTPUT_DIR}/lib/libswscale.so
    ${OUTPUT_DIR}/lib/libswscale.so.5
    ${OUTPUT_DIR}/lib/libswscale.so.5.10.100
    ${OUTPUT_DIR}/bin/ffmpeg
    ${OUTPUT_DIR}/bin/ffprobe
)

set(INCS ${INCS})
string(CONCAT INCS ${INCS} ":${FFMPEG_GCU_HEADER_PATH}")
string(CONCAT INCS ${INCS} ":${FFMPEG_GPU_HEADER_PATH}")
string(CONCAT INCS ${INCS} ":${runtime_install_opt_dir_for_run}/include")
message(${INCS})

set(PKGS ${PKGS})
string(CONCAT PKGS ${PKGS} ":${FFMPEG_GCU_LIB_PATH}/pkgconfig")
string(CONCAT PKGS ${PKGS} ":${FFMPEG_GPU_LIB_PATH}/pkgconfig")
message(${PKGS})

add_custom_command(
    OUTPUT ${ffmpeg_build_outputs}
    COMMAND ${CMAKE_COMMAND} -E env
        LIBRARY_PATH=$LIBRARY_PATH:${LDS}
        CPATH=$CPATH:${INCS}
        PKG_CONFIG_PATH=$PKG_CONFIG_PATH:${PKGS}
        ${CMAKE_CURRENT_SOURCE_DIR}/build_ffmpeg.sh ${FFMPEG_TAG} ${ffmpeg_gcu_prefix} ${configure_options}
    DEPENDS ffmpeg_gcu_header_target
    DEPENDS ffmpeg_gpu_header_target_12_1_14_0
    DEPENDS ffmpeg_src
    DEPENDS PREBUILD_TOPSCODEC_DEB_EXTRACT
    USES_TERMINAL
)

add_custom_target(ffmpeg_target ALL
    DEPENDS ${ffmpeg_build_outputs}
)

add_custom_target(ffmpeg_pkg_from_gcu
    COMMAND mkdir -p ${CMAKE_BINARY_DIR}/bin ${CMAKE_BINARY_DIR}/lib
    COMMAND find ${ffmpeg_gcu_prefix}/ffmpeg_gcu/bin -maxdepth 1 -mindepth 1 -type f -exec ln -sf {} ${CMAKE_BINARY_DIR}/bin \\\;
    COMMAND find ${ffmpeg_gcu_prefix}/ffmpeg_gcu/lib -maxdepth 1 -mindepth 1 -type f -exec ln -sf {} ${CMAKE_BINARY_DIR}/lib \\\;
    COMMAND find ${ffmpeg_gcu_src_dir} -maxdepth 1 -mindepth 1 -type d -exec ln -sf {} ${CMAKE_BINARY_DIR} \\\;
    DEPENDS ffmpeg_target
)

install(
    DIRECTORY
    ${ffmpeg_gcu_prefix}/ffmpeg_gcu/bin
    ${ffmpeg_gcu_prefix}/ffmpeg_gcu/include
    ${ffmpeg_gcu_prefix}/ffmpeg_gcu/lib
    ${ffmpeg_gcu_prefix}/ffmpeg_gcu/share
    DESTINATION . USE_SOURCE_PERMISSIONS COMPONENT ${_pkg_component})
