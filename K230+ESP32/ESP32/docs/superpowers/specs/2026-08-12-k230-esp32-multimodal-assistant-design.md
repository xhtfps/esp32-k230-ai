# K230 + ESP32 多模态 AI 助手设计

## 1. 目标与范围

在现有 K230 视觉链路和 ESP32 网络工程基础上，构建一个按键/触摸说话的中文多模态 AI 助手：

- ESP32-S3-CAM V1.3 直连 1.8 英寸 ST7735S 表情屏、INMP441 数字麦克风和 MAX98357 I2S 功放；
- K230 V3.0 保留摄像头、本地视觉推理、800×480 LCD 与 LVGL 触控交互；
- ESP32 调用云端 ASR、LLM、TTS；
- K230 与 ESP32 保持现有 921600 baud UART 二进制协议，不传连续音频或视频。

本期不使用 ESP32 的 OV2640，也不使用 ESP32 板载 Micro SD 或 ST7735 板载 Micro SD。OV2640 可保留物理连接，但 ESP32 固件不得初始化相机。

## 2. 硬件连接

### 2.1 ESP32-S3-CAM V1.3

| 外设 | 信号 | ESP32 引脚 |
|---|---|---:|
| K230 UART2 | TX | GPIO1（G1） |
| K230 UART2 | RX | GPIO2（G2） |
| ST7735S | SCLK | GPIO41（G41） |
| ST7735S | MOSI | GPIO42（G42） |
| ST7735S | CS | GPIO47（G47） |
| ST7735S | DC | GPIO14（G14） |
| ST7735S | RST | GPIO15（G15） |
| ST7735S | BL | 3V3（常亮） |
| INMP441 + MAX98357 | BCLK | GPIO40（G40） |
| INMP441 + MAX98357 | WS/LRCLK | GPIO39（G39） |
| INMP441 | DOUT（输入 ESP32） | GPIO38（G38） |
| MAX98357 | DIN（输出 ESP32） | GPIO16（G16） |

- ST7735S 的 VCC 接 3V3，GND 接公共地；初始 SPI 时钟为 10 MHz，稳定后可提高至 20 MHz。
- 该 TFT 背面顶排丝印依次为 `VCC/GND/CS/RESET/A0/SDA/SCK/LED`：其中 `SDA` 是 SPI 的 MOSI，不是 I2C 的 SDA；`A0` 是数据/命令选择（DC）。下排 `SD-CS/SD-MOSI/SD-MISO/SD-SCK` 仅服务于板载 Micro SD，本期全部悬空。
- INMP441 的 VDD 接 3V3、L/R 接 GND（左声道）、GND 接公共地。
- MAX98357 的 VIN 接稳定 5V、GND 接公共地；扬声器只接 SPK+、SPK-，不接系统 GND。
- ESP32 的一个 GND 引脚只需引出一次到面包板负电源轨、接线端子或焊接公共地节点；TFT、INMP441、MAX98357、K230 GND 和 5V 电源负极均接到此公共节点，无需 ESP32 为每个模块提供独立 GND 针脚。MAX98357 的 5V 与 GND 线应短且较粗，并优先回到 5V 电源入口；长期使用不应让功放大电流经 ESP32 的 3V3 稳压器或细长地线回流。
- GPIO33--GPIO37 为板载 8 MB 八线 PSRAM 专用信号，绝不能分配给外设；尤其 GPIO35/36/37 分别为 DQ6/DQ7/DQS。否则会在 `app_main()` 前触发 PSRAM 内存测试失败并重启。
- GPIO14--GPIO16 仅在 ESP32 的 OV2640 未使用且排线拔掉时可用于外设；本期由 DC、RST、MAX98357 DIN 使用。
- GPIO0、GPIO45、GPIO46、GPIO48 不分配给新外设，分别避免影响启动模式、调试功能或板载 RGB LED。
- 板载 SD 不插卡也不挂载，GPIO38--40 复用于 I2S。若将来启用 SD，必须重新安排 I2S。

### 2.2 K230 V3.0

K230 不再承担音频 I2S。它保留：摄像头、ST7701 800×480 LCD、触控、UART2 和视觉推理。

UART 接线保持 3.3 V TTL、交叉收发且共地：

```text
K230 GPIO5 / UART2_TX  -> ESP32 GPIO2 / RX
K230 GPIO6 / UART2_RX  <- ESP32 GPIO1 / TX
K230 GND               <-> ESP32 GND
```

K230 与 ESP32 各自供电，不以一块板的 3V3 为另一块开发板供电。

## 3. 运行架构

```text
INMP441 -> ESP32 I2S RX -> 云端 ASR -> 云端 LLM -> 云端 TTS -> ESP32 I2S TX -> MAX98357
                             |                 |
                             |                 +-> ST7735 表情状态
                             v
K230 摄像头 -> 本地推理 -> UART 视觉结果/JPEG -> ESP32
K230 LVGL/触控 <-------- UART 状态、转写、回答 -------- ESP32
```

ESP32 是会话编排者：拥有 Wi-Fi、云端密钥、语音状态机和 ST7735 表情状态。K230 不保存云端密钥；其触摸事件只通过 UART 请求 ESP32 开始或结束一次会话。

### 3.1 会话状态机

状态固定为 `IDLE -> LISTENING -> TRANSCRIBING -> THINKING -> SPEAKING -> IDLE`，任何状态均可进入 `ERROR`，错误确认后回到 `IDLE`。

- K230 LVGL 触控按下发送“开始说话”；松开发送“结束说话”。
- ESP32 在 LISTENING 中录制 16 kHz、单声道、PCM16 音频。
- 录音结束后，ESP32 请求 ASR；得到完整转写后调用 LLM；得到回答后调用 TTS 并边缓冲边播放。
- ST7735 仅渲染对应表情，例如空闲、倾听、思考、说话、故障；K230 大屏显示状态、转写和回答文字。
- 视觉结果可作为 LLM 的上下文摘要；只有用户明确请求“看一下/分析画面”时，ESP32 请求 K230 发送 JPEG。

## 4. K230 LVGL 架构

采用官方 AI+LVGL 思路：摄像头/AI 图层与 LVGL UI 图层隔离，双缓冲刷新，触控事件异步进入应用事件队列。

- `vision_service`：采集、推理和视觉摘要；不得直接操作 LVGL 对象。
- `uart_service`：收发二进制帧、心跳、重同步；不得直接操作 LVGL 对象。
- `touch_service`：将触摸转换为 UI 事件。
- `app_store`：保存会话状态、转写、回答、视觉摘要和链路状态。
- `ui_controller`：唯一允许创建或更新 LVGL 对象的模块；按状态差量刷新。

大屏包含对话主页面、可展开的视觉卡片、历史记录和设置页。对话文字是主信息，摄像头画面是辅助信息；不得把 UI 简化成孤立滑块或调试页。

## 5. UART 协议

物理层固定为 `921600 8N1`。沿用既有帧格式：`A5 5A | version | type | length(uint16 LE) | sequence(uint16 LE) | payload | CRC16-CCITT`。

保留既有视觉消息：

| 类型 | 方向 | 用途 |
|---:|---|---|
| `0x01` | K230 -> ESP32 | 视觉结果 JSON |
| `0x02` | 双向 | 心跳/链路状态 |
| `0x10`--`0x13` | 双向 | 请求、分块、完成、确认 JPEG |
| `0x7F` | 双向 | 错误帧 |

增加会话语义消息：

| 类型 | 方向 | Payload |
|---:|---|---|
| `0x20` | K230 -> ESP32 | `UI_ACTION`：开始说话、结束说话、停止播放、请求视觉分析 |
| `0x21` | ESP32 -> K230 | `ASSISTANT_STATE`：状态码、会话 ID、可选错误码 |
| `0x22` | ESP32 -> K230 | `TRANSCRIPT`：UTF-8 最终转写 |
| `0x23` | ESP32 -> K230 | `ANSWER_CHUNK`：UTF-8 回答增量或最终回答 |
| `0x24` | ESP32 -> K230 | `ASSISTANT_ERROR`：稳定错误码和 UTF-8 短说明 |

UART 不传连续原始音频。视觉 JSON 限流为变化时或每 200 ms 一次；JPEG 传输期间暂停常规视觉摘要；JPEG 上限维持 256 KiB 和既有 CRC32 校验。

## 6. 云端与安全

- INMP441 以 16 kHz、32-bit I2S 槽位采集其 24-bit 有效数据，ESP32 取左声道高 16 位转换为单声道 PCM16；录音以 20 ms 帧聚合后发送 ASR。
- TTS 请求 16 kHz、单声道 PCM/WAV；先缓存至少 120 ms 再开始 MAX98357 播放，以减少卡顿。
- LLM 沿用现有 DashScope OpenAI 兼容调用方式；视觉请求才附加 K230 JPEG。
- API Key 不得写入源代码或通过 UART 发送给 K230。使用 NVS 安全存储或不纳入 Git 的本地配置；已有明文泄露的 Key 必须轮换。
- 云端超时、网络中断、ASR/TTS 格式不符须转为 `ASSISTANT_ERROR`，并让两块屏回到可操作状态。

## 7. 验收标准

1. ST7735 独立显示 128×160 RGB565 表情，连续切换 30 分钟无花屏或重启。
2. INMP441 录制 16 kHz PCM，MAX98357 播放测试音正常，无明显欠采样或爆音。
3. 从 K230 触控开始说话到 ESP32 进入 LISTENING 的状态同步不超过 300 ms。
4. 一次 10 秒普通中文语音可完成 ASR、LLM、TTS，并在 K230 大屏显示转写和回答、ST7735 显示对应表情。
5. K230 视觉结果持续运行时，语音会话稳定；用户触发视觉分析时可完成一张 JPEG 传输和云端分析。
6. 拔掉 K230 UART 或断开 Wi-Fi 时，两个界面能显示明确错误，恢复连接后无需重新烧录。

## 8. 实施边界

第一阶段只实现外设自检与状态联动，不改变既有 K230 视觉结果/JPEG 协议。第二阶段实现 ESP32 音频采集、播放和云端调用。第三阶段接入成熟 K230 LVGL 应用层及会话语义消息。每阶段完成后先做硬件单测，再做双板集成测试。
