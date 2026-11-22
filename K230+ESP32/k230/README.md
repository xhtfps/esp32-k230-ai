# K230 视觉链路

本目录中的脚本用于 CanMV K230 V3.0 与 ESP32-S3 之间的 UART2 链路。

## 先验证 LVGL 与触控 API

在部署 AI 助手二级页前，先将 `diagnose_lvgl_touch.py` 上传到 K230
`/sdcard/` 并在 CanMV IDE 中运行。它不会改动摄像头预览，只会打印此
CanMV v1.2.2 固件实际提供的 `lvgl`、`TOUCH` API，并尝试显示
`LVGL diagnostic OK` 标签。

请把完整串口输出发回。尤其需要确认 `TOUCH` 的构造函数、读点方法和
按下/松开返回结构；在此诊断通过前，不将触控代码接入 `main.py`，以免
不同固件 API 导致摄像头主循环退出。

## 运行摄像头和 LCD

将以下文件复制到 K230 的 `/sdcard/`：

- `main.py`
- `vision_app.py`
- `vision_link.py`
- `vision_protocol.py`

在 CanMV IDE 中运行 `main.py`，或将它保存为 `/sdcard/main.py` 后复位。程序严格参考可运行的 Lissajous 工程，使用 `Sensor(width=800, height=480)`、`Sensor.RGB565`、`Display.init(Display.ST7701, width=800, height=480, to_ide=True)`、`MediaManager.init()` 和 `sensor.run()`，然后把 `sensor.snapshot()` 显示到 `800x480` LCD。

`main.py` 的默认入口是纯摄像头/LCD预览，先不加载 UART 和模型，便于确认硬件视频链路；确认画面正常后，再把 UART 和推理接回主循环。

当前入口先验证摄像头和 LCD 通路，推理回调返回空结果；接入模型后只需替换 `create_vision_app()` 中的 `capture_and_display()`。

硬件连接：

```text
K230 GPIO5 / UART2_TXD / 排针17 -> ESP32 UART_RX
K230 GPIO6 / UART2_RXD / 排针18 -> ESP32 UART_TX
K230 GND                       -> ESP32 GND
```

两块板使用 3.3 V TTL，分别供电；不要把 K230 主板从 ESP32 3.3 V 引脚供电。

默认串口为 `921600 8N1`，不稳定时两端同时改为 `460800`。脚本依赖：

- `vision_protocol.py`：帧编码、CRC16 和解码；
- `vision_link.py`：FPIOA/UART2、心跳、JSON 结果和拍照请求；
- `vision_app.py`：模型无关的推理循环和 JPEG 分块。

首个模型可将资料中的 `face_detection.py` 推理结果适配为：

```python
{"task": "face", "count": 1,
 "items": [{"x": 320, "y": 180, "w": 96, "h": 120, "score": 0.92}]}
```

ESP32 发出 `0x10` 拍照请求后，`jpeg_fn` 应返回 JPEG 字节串；K230 会以 2048 字节分块发送，最后发送总长度和 CRC32。API Key 不放在 K230 脚本中。
