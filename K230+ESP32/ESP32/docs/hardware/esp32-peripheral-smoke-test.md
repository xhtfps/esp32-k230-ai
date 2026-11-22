# ESP32 外设冒烟测试

## 烧录前

1. 断电后检查 TFT、INMP441、MAX98357 和 K230 均接公共 GND。
2. 万用表确认 3V3 电源轨与 5V 电源轨之间没有短路。
3. 确认 MAX98357 扬声器只接 `+/-` 输出端，任何一端都不接 GND。
4. 确认 INMP441 的 `SD` 接 GPIO38；MAX98357 的 `SD` 接 GPIO16，两者没有互连。
5. 此板的 8 MB 八线 PSRAM 占用 GPIO33--37；GPIO35、GPIO36、GPIO37 必须悬空，绝不能接 TFT 或音频模块。
6. 本工程不使用 ESP32 的 OV2640：使用 GPIO15、GPIO16 前，必须将 OV2640 排线从 ESP32 板上拔掉。

## 构建与烧录

旧 `build` 目录记录了另一工程路径，使用独立构建目录：

```powershell
& 'D:\Espressif\frameworks\esp-idf-v5.5.5\export.ps1'
idf.py -B build_codex_clean -DIDF_COMPONENT_MANAGER=0 build
idf.py -B build_codex_clean -p COM端口 flash monitor
```

启动日志应出现：

```text
FACE_DISPLAY: ST7735 ready: 128x160 SCLK=41 MOSI=42 CS=47 DC=14 RST=15
AUDIO_I2S: I2S ready: rate=16000 BCLK=40 WS=39 DIN=38 DOUT=16
K230_LINK: UART链路已启动
MAIN: Assistant peripherals ready; ASRPRO/OV2640/SD are disabled
```

## 预期现象

- 开机后 ST7735 显示深蓝背景、两只青色眼睛和一条嘴，表示 `IDLE`。
- 默认开启 `CONFIG_ASSISTANT_PERIPHERAL_SELF_TEST`：开机会播放约 250 ms 的低音量测试音，并输出 `INMP441 capture test: samples=320 peak=...`。确认接线后可用 `menuconfig` 关闭。
- K230 发送 `UI_ACTION=1` 后，表情变为绿色倾听状态。
- `UI_ACTION=2` 后，表情变为紫色思考状态。
- `UI_ACTION=3` 后，返回深蓝空闲状态。
- `audio_i2s_play_test_tone(500)` 可播放约 500 ms 的低音量 440 Hz 方波，用于验证 MAX98357；该函数不会自动运行。
- `audio_i2s_read_pcm16()` 每次最多读取 320 个单声道样本，即 20 ms、640 字节 PCM16。

若屏幕背光亮但没有图形，优先检查 `CS/G47`、`RESET/G15`、`A0/G14`、`SDA/G42`、`SCK/G41`。若画面整体偏移，可在 `face_display.c` 调整 `FACE_LCD_X_OFFSET` 和 `FACE_LCD_Y_OFFSET`。
