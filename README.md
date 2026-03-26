# FFmpeg-GCU

FFmpeg-GCU 是基于 [燧原](https://www.enflame-tech.com/) GCU 硬件加速的 FFmpeg 视频解码插件，通过 TopsCodec SDK 实现高性能视频解码。

## 1 特性

- 支持 GCU300 及以上硬件
- 支持 H.264、H.265/HEVC、VP8、VP9、AV1、AVS、AVS2、MJPEG、MPEG2、MPEG4、VC-1、H.263 等格式硬件解码
- 支持 Online CSC（颜色空间转换）、Resize、Crop、Rotation 等后处理
- 兼容 FFmpeg 命令行、C API、OpenCV、PyAV、imageio 等多种调用方式
- 支持 FFmpeg n3.2 / n4.4 / n5.0 多版本构建

## 2 编译与运行

### 2.1 前置依赖


| 依赖项              | 说明                                              |
| ---------------- | ----------------------------------------------- |
| 燧原 GCU 硬件        | GCU300 及以上                                      |
| TopsPlatform 软件栈 | 包含 FFmpeg-GCU 依赖的 TopsCodec 和 TopsRuntime，需提前安装 |

如需获取 TopsPlatform 对应版本，请联系燧原科技商务对接人员。

具体安装步骤请参见 [TopsPlatform 安装使用手册](https://support.enflame-tech.com/onlinedoc_dev_3.6/_static/topsplatform_html/1-install/quick_started/content/source/index.html)。

#### 已验证的编译环境

| OS           | GCC    | glibc |
| ------------ | ------ | ----- |
| Ubuntu 16.04 | 4.8.5  | 2.23  |
| Ubuntu 18.04 | 7.5.0  | 2.27  |
| Ubuntu 20.04 | 9.4.0  | 2.31  |
| Ubuntu 24.04 | 13.3.0 | 2.39  |

### 2.2 编译出带燧原硬件加速的 FFmpeg 可执行程序

FFmpeg-GCU 编译依赖 TopsPlatform 的头文件，需指定头文件所在的路径（默认安装路径为 /opt/tops/include）。

```bash
# 基本用法（默认 n4.4），TopsPlatform 默认安装的头文件路径在 /opt/tops/include
./build_ffmpeg.sh n4.4 -f "-I/opt/tops/include"

# 为了避免从 github 上拉取 FFmpeg 源代码缓慢，可以指定本地 FFmpeg 源码路径（提前下载所需版本 FFmpeg 源码）
./build_ffmpeg.sh n4.4 -f "-I/opt/tops/include" -s /path/to/FFmpeg

# 查看完整编译选项
./build_ffmpeg.sh -h
```

支持的 FFmpeg 版本标签：`n3.2`、`n4.4`、`n5.0`。

如果希望支持更多的 FFmpeg 版本，请联系燧原科技商务对接人员。

编译完成后，在 `build_<tag>/ffmpeg_gcu/` 目录下生成 `.deb` 安装包。

### 2.3 安装

```bash
sudo dpkg -x ffmpeg-gcu_<version>_<tag>_amd64.deb /usr/local/
```

安装后，可在 `/usr/local/lib`、`/usr/local/bin`、`/usr/local/include` 下找到对应的库文件、可执行文件和头文件。

### 2.4 添加环境变量

```bash
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
```

### 2.5 运行

成功安装带有燧原硬件加速的 FFmpeg 后，可通过以下命令行进行验证。

#### FFmpeg 命令行举例

```bash
# 将输入文件 test.264 解码，并输出到 out.yuv 文件，YUV文件格式指定为 YUV420P
ffmpeg -c:v h264_topscodec -output_pixfmt yuv420p -i test.264 -y out.yuv

# 将输入文件 test.265 解码，并输出到 out.yuv 文件，YUV文件格式指定为 NV12
ffmpeg -c:v hevc_topscodec -output_pixfmt nv12 -i test.265 -y out.yuv
```

## 3 文档

详细的使用说明、API 参考、命令行示例及 OpenCV / PyAV 集成指南，请参阅在线文档：

**[FFmpeg-GCU 用户文档](https://support.enflame-tech.com/onlinedoc_dev_3.6/5-program/topscv/FFmpeg_GCU/content/source/index.html)**

## 4 Release Notes

详见 [RELEASE_NOTES.md](RELEASE_NOTES.md)。