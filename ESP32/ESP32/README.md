# ESP32-S3 + Arduino ATmega328 机器人控制系统

## 项目概述

本项目是一个基于 ESP32-S3 和 Arduino ATmega328 的双控制器机器人系统：

- **ESP32-S3**: 负责摄像头视频流、WiFi AP、HTTP 服务器、SU-03T 语音模块控制
- **ATmega328**: 负责接收 ESP32 指令并转发给舵机控制板
- **上位机**: Python 视觉识别程序，通过 WiFi 与 ESP32 通信

## 项目结构

```
ESP32/
├── platformio.ini          # PlatformIO 多环境配置文件
├── README.md               # 本文件
├── .gitignore              # Git 忽略规则
│
├── src/                    # 源代码目录（PlatformIO 默认）
│   ├── main.cpp            # ESP32-S3 主程序
│   ├── arduino_receiver.cpp # ATmega328 接收端程序
│   └── config.h            # 双方共享的配置文件
│
├── include/                # 头文件目录
│   └── README              # 目录说明
│
├── lib/                    # 库文件目录
│   └── README              # 目录说明
│
├── test/                   # 测试目录
│   └── README              # 目录说明
│
├── pose_tracker.py         # 上位机视觉识别程序
└── test_voice.py           # SU-03T 语音测试工具
```

## 快速开始

### 1. 编译 ESP32-S3 程序

```bash
# 默认编译 ESP32
pio run

# 或显式指定
pio run -e esp32s3box
```

### 2. 编译 ATmega328 程序

修改 `platformio.ini` 中的 `default_envs` 为 `atmega328`，然后编译：

```bash
pio run -e atmega328
```

### 3. 上传程序

```bash
# 上传 ESP32
pio run -e esp32s3box --target upload

# 上传 Arduino
pio run -e atmega328 --target upload
```

### 4. 运行上位机视觉程序

```bash
pip install opencv-python mediapipe requests
python pose_tracker.py
```

## 配置文件说明

所有硬件参数和通信协议集中在 `src/config.h` 中：

| 配置区域 | 说明 |
|---------|------|
| 公共通信配置 | 波特率、指令码表、协议格式（双方共享） |
| ESP32 专属配置 | WiFi、摄像头、HTTP 服务器、SU-03T |
| ATmega328 专属配置 | SoftwareSerial 引脚、舵机转发开关 |
| 指令映射表 | `CMD_TABLE` 数组，添加新动作只需改这里 |

### 添加新动作

在 `src/config.h` 的 `CMD_TABLE` 数组中增加一行：

```cpp
{CMD_NEW, 'M', "NEW_ACTION"},  // 指令码 -> 舵机字符 "名称"
```

无需修改 `arduino_receiver.cpp`！

## 硬件连接

### ESP32-S3 ↔ ATmega328

| ESP32-S3 | ATmega328 |
|---------|-----------|
| GPIO14 (TX) | Pin12 (RX) |
| GPIO15 (RX) | Pin13 (TX) |
| GND | GND |

### ATmega328 ↔ 舵机控制板

| ATmega328 | 舵机控制板 |
|---------|-----------|
| A0 (RX) | TX |
| A1 (TX) | RX |

## 注意事项

1. 修改 `config.h` 后需要重新编译并上传双方程序
2. ESP32 和 ATmega328 的通信波特率必须一致
3. 首次编译 AVR 环境时 PlatformIO 会自动下载工具链