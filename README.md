# ESP32-S3 + K230 边缘 AI 视觉工程

ESP32-S3 与 K230 协作的边缘视觉项目合集：K230 侧跑图像采集与模型推理，ESP32 侧做通信、控制与上位机联调，`robot/` 存放机器人整机集成代码。

## 1. 目录一览

| 目录 | 内容 |
| --- | --- |
| `ESP32/` | ESP-IDF 工程：WiFi/串口通信、数据转发、设备控制 |
| `K230+ESP32/ESP32/` | 双板联调用的 ESP32 工程（与 K230 协议对接版） |
| `K230+ESP32/k230/` | K230 侧脚本（CanMV / MicroPython），摄像头采集、检测、串口发送 |
| `K230+ESP32/docs/` | 通信协议、接线表、调试记录 |
| `robot/` | 机器人整机集成（云台/舵机/语音交互） |

## 2. 环境准备

**ESP32-S3 侧**

- ESP-IDF 5.x（建议 v5.2 以上），安装后激活环境：

```powershell
$env:IDF_PATH="C:\Espressif\frameworks\esp-idf-v5.2"
& "$env:IDF_PATH\export.ps1"
```

- USB 驱动（CH343/CP210x）安装后确认设备管理器出现串口。
- 编译烧录：

```powershell
cd .\ESP32
idf.py set-target esp32s3
idf.py menuconfig        # 按需改 WiFi SSID / 波特率
idf.py build
idf.py -p COMx flash monitor
```

**K230 侧**

- CanMV-K230 固件（MicroPython），插卡启动后通过 IDE 或 REPL 运行 `k230/` 下脚本。
- 摄像头为板载 CIS 或外接 GC2093 类模组，模型文件放 K230 文件系统 `/sdcard/models/`。

## 3. 联调要点

1. 先单独验证两端：K230 能在 IDE 里出帧率打印，ESP32 能 `idf.py monitor` 看到启动日志。
2. 接线：K230 UART → ESP32 UART（TX/RX 交叉、共地），波特率两端一致（常见 115200 / 460800）。
3. 协议字段与帧头尾以 `K230+ESP32/docs/` 为准，改协议要两端同时改。
4. 联合调试时先跑“固定字符串回环”，再切到图像结果，便于定位丢帧。

## 4. 常见问题

- **ESP32 反复重启**：多为供电不足（摄像头/舵机同时工作时尤其明显），改用外部 5V 并共地。
- **K230 帧率低**：降低分辨率或关闭显示叠加；`sensor.set_framesize` 改 QVGA 先验证链路。
- **串口乱码**：波特率不匹配或 TX/RX 未交叉；K230 端注意 3.3V 电平。
- **烧录失败**：按住 BOOT 再上电进入下载模式。

## 5. 维护说明

- 原 `k230-esp32-vision-link`、`dancing-robot-multimodal` 两个仓库已并入本仓库（文件内容逐一比对），原仓库已删除。
- 排除清单已嘲讽为 [`EXCLUDED_FILES.md`](EXCLUDED_FILES.md) 摘要（原 4.4 MB 逐文件 CSV 已删除，明细存本地）。
- 第三方库与模型授权请单独确认。
