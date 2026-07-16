# FFmpeg-GCU Release Notes

## v3.1.9

### 新增功能

- 新增支持编码功能，支持 H.264/AVC、H.265/HEVC 硬件编码
- 解码器新增异步解码模式，相比同步解码模式可获得更高解码性能
- 新增编解码器负载均衡模式，可通过 `balance` 参数设置

### TopsPlatform 兼容性

| FFmpeg-GCU 版本 | TopsPlatform 版本 | 备注 |
| ------------- | ------------ | ---- |
| v3.1.9        | 1.9.24       |  |

## v1.5.1

### 新增功能


| Feature                                               | n3.2 | n4.4 及以上 |
| ----------------------------------------------------- | ---- | ---- |
| 环境变量指定设备（`TOPSCODEC_CARD_ID` / `TOPSCODEC_DEVICE_ID`） | ✅    | ✅    |
| VP8/VP9/AV1 解码                                                | ❌    | ✅    |
| AVS/AVS2 解码                                         | ❌    | ✅    |
| H.263/MJPEG/MPEG2/MPEG4/VC-1 解码            | ❌    | ✅    |
| FFmpeg `receive_frame` 新 API 适配                       | ❌    | ✅    |
| 输出像素格式 gray10le/p010le_lsb/rgb24p/bgr24p            | ❌    | ✅    |


### Bug 修复

- 修复多处内存泄漏（decoder flush / buffer 管理路径）
- 修复多路并发场景下的 coredump 问题
- 修复 deb 包构建异常
- 移除 ASAN 残留编译选项

### TopsPlatform 兼容性


| FFmpeg-GCU 版本 | TopsPlatform 版本 |    备注     |
| ------------- | ------------ | ------ |
| v1.5.1        | 1.5.1.x    |        |
