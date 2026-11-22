# OV2640 摄像头 BSP 设计

## 范围

为 ESP32-S3 的 OV2640 摄像头增加独立模块，实现传感器初始化、运行时调整
JPEG 画质和在内存中连续获取图像帧。本模块不使用 SD 卡、文件系统、Wi-Fi
或 Web 服务器。

## 硬件配置

板级连线与 `E:\ESP32\esp32-s3-cam` 一致：

- SCCB 数据线 SIOD：GPIO4；SCCB 时钟线 SIOC：GPIO5
- XCLK：GPIO15；PCLK：GPIO13；VSYNC：GPIO6；HREF：GPIO7
- D0 至 D7 依次为 GPIO11、GPIO9、GPIO8、GPIO10、GPIO12、GPIO18、GPIO17、
  GPIO16
- PWDN 和 RESET 未连接。

## 架构

`components/BSP/CAMERA` 独占 `esp32-camera` 驱动及图像帧缓冲区的生命周期，
对外提供 `camera_init`、`camera_set_quality`、`camera_get_frame`、
`camera_return_frame` 和 `camera_deinit`。

`main.c` 负责应用初始化顺序：初始化已有板级驱动，初始化 CAMERA，再启动一个
FreeRTOS 图像帧消费任务。该任务获取一帧图像，记录帧序号、字节数和计算得到的
帧率；无论处理结果如何，均在请求下一帧前归还该帧缓冲区。

## 默认参数与错误处理

摄像头默认使用 JPEG/QVGA、10 MHz XCLK、两个 PSRAM 帧缓冲区和
`CAMERA_GRAB_WHEN_EMPTY` 取帧模式。JPEG 画质可设为 0--63：数值越小，画质
越高，图像也越大。无效画质参数、摄像头初始化失败、未取得传感器控制对象和
取帧失败均以 ESP-IDF 错误码和日志报告。由 `camera_get_frame` 获取的帧必须且
只能通过 `camera_return_frame` 归还一次。

## 验证

工程必须能解析 `espressif/esp32-camera` 依赖，并通过 ESP-IDF 工具链构建。
运行时应观察到 OV2640 探测成功，随后持续输出图像帧日志且不发生帧缓冲耗尽。
