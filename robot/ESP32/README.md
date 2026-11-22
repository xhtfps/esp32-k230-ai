# ESP32 舞蹈机器人主控

本目录是 ESP32-S3 固件，负责连接 SU-03T、Arduino、电脑视觉和 INMP441。

## 目录结构

```text
main/                         启动入口和事件路由
components/BSP/               硬件与业务模块
  SERIAL_BRIDGE/              SU-03T、Arduino 串口和命令映射
  COMPETITION/                比赛状态机
  WEB/                        摄像头网页和视觉结果接口
  POSE/                       ESP32 本地姿态识别
  MUSIC/                      INMP441 音乐识别
  AUDIO_I2S/                  音频采集/播放
  CAMERA/                     摄像头
tests/                        契约测试
tools/                        音乐特征和串口工具
docs/                         接线、规则和硬件验证
managed_components/           ESP-IDF 依赖
sdkconfig                     当前芯片配置
partitions-*.csv              分区表
```

## 设备链路

```text
SU-03T → ESP32 UART1 → ESP32 UART2 → Arduino → 蓝牙 → STM32
电脑视觉 → Wi-Fi HTTP → ESP32
INMP441 → I2S → ESP32
```

## 任务命令

```text
0x44 进入任务一，随后接受语音动作 0x01-0x0D
0x55 进入任务二；0x22 武装一次视觉识别
0x66 进入任务三；0x33 启动一次音乐识别
音乐 ID 1-5 -> 播报消息 7-11 -> 5 秒 -> R1/0x44
音乐 ID 6   -> 播报消息 12   -> 5 秒 -> R2/0x45
维护命令 0x46 -> RESET/J，不占用上述 19 个比赛动作键位
```

## 视觉模式切换

BOOT 按键用于切换视觉模式：

- 蓝色 LED：PC/Wi-Fi 模式，摄像头通过 `/stream` 提供给电脑识别；
- 绿色 LED：ESP32 本地模式，使用板载 ESP-DL 姿态模型，不依赖电脑或 Wi-Fi。

切换到本地模式时会停止 HTTP/Wi-Fi 服务并释放 PC 模式摄像头资源；切回 PC/Wi-Fi
模式时会重新初始化摄像头、Wi-Fi 和网页服务。比赛视觉环节中，本地模式收到
`0x22` 后才接受一次稳定姿态，随后播报识别结果、派发 Arduino 动作，并等待
Arduino ACK 完成该项动作。

本地 Team 1 与电脑端使用相同视觉动作编号：1 大字站、2 举双手、3 蹲下、4 弓箭步、
5 叉腰、6 敬礼。
```

## 常用操作

```powershell
idf.py build
idf.py -p COMx flash
idf.py -p COMx monitor
```

音乐特征重新生成：

```powershell
python -m pip install -r tools/requirements-music.txt
python -B tools/music_features.py `
  --source "D:\robot\舞蹈-表演赛指定音乐" `
  --output "components\BSP\MUSIC\generated_music_features.h"
```

硬件细节见 `docs/hardware/competition-serial-smoke-test.md`。
