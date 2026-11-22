# ESP32 Multimodal Peripheral Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the testable ESP32 foundation for the ST7735 face screen, shared-clock I2S microphone/amplifier, and K230 assistant-state UART messages.

**Architecture:** `face_display` owns only ST7735 rendering; `audio_i2s` owns only I2S capture/playback; `assistant_state` owns state transitions and fans state changes out to the face display and K230. Existing vision/JPEG messaging remains unchanged.

**Tech Stack:** ESP-IDF 5.x, C, FreeRTOS, SPI master driver, I2S standard-mode driver, existing K230 UART framing/CRC16.

## Global Constraints

- Target ESP32-S3; never initialize ESP32 OV2640 or Micro SD.
- K230 UART remains GPIO1 TX, GPIO2 RX, 921600 8N1.
- ST7735S uses GPIO41 SCLK, GPIO42 MOSI, GPIO47 CS, GPIO14 DC, GPIO15 RST, RGB565.
- I2S uses GPIO40 BCLK, GPIO39 WS, GPIO38 RX data, GPIO16 TX data.
- Audio is 16 kHz with 32-bit Philips stereo slots; the INMP441 left channel is converted to PCM16 and L/R is grounded.
- Existing UART types `0x01`, `0x02`, `0x10`--`0x13`, `0x7F` remain compatible; UART never carries PCM.
- All new module APIs return `esp_err_t`; internal modules do not use `ESP_ERROR_CHECK`.

## File Structure

- Create `components/BSP/FACE_DISPLAY/face_display.[ch]`: ST7735 initialization and five facial expressions.
- Create `components/BSP/AUDIO_I2S/audio_i2s.[ch]`: full-duplex shared-clock I2S init/read/write.
- Create `components/BSP/ASSISTANT/assistant_state.[ch]`: session state, face mapping, UART state/text publication.
- Modify `components/BSP/K230_LINK/k230_protocol.h`: message types `0x20`--`0x24`.
- Modify `components/BSP/CMakeLists.txt`: component discovery and `esp_driver_spi`, `esp_driver_i2s` dependencies.
- Modify `main/main.c`: start peripherals, remove ASRPRO startup from active path, route K230 UI actions.
- Create `docs/hardware/esp32-peripheral-smoke-test.md`: precise physical test sequence.

## Task 1: Assistant UART Vocabulary

**Files:**
- Modify `components/BSP/K230_LINK/k230_protocol.h`

**Produces:** `K230_FRAME_UI_ACTION=0x20U`, `K230_FRAME_ASSISTANT_STATE=0x21U`, `K230_FRAME_TRANSCRIPT=0x22U`, `K230_FRAME_ANSWER_CHUNK=0x23U`, `K230_FRAME_ASSISTANT_ERROR=0x24U`.

- [ ] Add those five macros immediately after `K230_FRAME_JPEG_ACK`.
- [ ] Run `idf.py build`; expected: successful build without macro collisions.
- [ ] Commit: `git add components/BSP/K230_LINK/k230_protocol.h && git commit -m "feat: define assistant uart message types"`.

## Task 2: Assistant State Module

**Files:**
- Create `components/BSP/ASSISTANT/assistant_state.h`
- Create `components/BSP/ASSISTANT/assistant_state.c`

**Consumes:** `k230_link_send(uint8_t, const uint8_t *, size_t)`, `face_display_set_expression(face_expression_t)`.

**Produces:**

```c
typedef enum {
    ASSISTANT_STATE_IDLE = 0, ASSISTANT_STATE_LISTENING,
    ASSISTANT_STATE_TRANSCRIBING, ASSISTANT_STATE_THINKING,
    ASSISTANT_STATE_SPEAKING, ASSISTANT_STATE_ERROR,
} assistant_state_t;
esp_err_t assistant_state_init(void);
esp_err_t assistant_state_set(assistant_state_t state, uint8_t error_code);
assistant_state_t assistant_state_get(void);
esp_err_t assistant_state_publish_text(uint8_t frame_type, const char *text);
```

- [ ] Use a FreeRTOS mutex to protect state and a monotonically increasing `uint16_t session_id`.
- [ ] Map states to face expressions in one static function.
- [ ] Encode an assistant-state payload as `{state(uint8), session_id(uint16 little-endian), error_code(uint8)}` and send `K230_FRAME_ASSISTANT_STATE`.
- [ ] Validate enum and text-frame input, return `ESP_ERR_INVALID_ARG` for invalid values.
- [ ] Run `idf.py build`; expected: no warnings in `ASSISTANT`.
- [ ] Commit: `git add components/BSP/ASSISTANT && git commit -m "feat: add assistant state coordinator"`.

## Task 3: ST7735 Face Display

**Files:**
- Create `components/BSP/FACE_DISPLAY/face_display.h`
- Create `components/BSP/FACE_DISPLAY/face_display.c`

**Produces:**

```c
typedef enum { FACE_IDLE, FACE_LISTENING, FACE_THINKING, FACE_SPEAKING, FACE_ERROR } face_expression_t;
esp_err_t face_display_init(void);
esp_err_t face_display_set_expression(face_expression_t expression);
```

- [ ] Initialize `SPI2_HOST` with SCLK GPIO41, MOSI GPIO42, CS GPIO47, DC GPIO14, RST GPIO15.
- [ ] Issue ST7735 reset, sleep-out, RGB565 color mode (`0x3A, 0x05`), MADCTL, display-on commands.
- [ ] Implement bounded `write_command`, `write_data`, `fill_rect` helpers; use a DMA-capable line buffer, never a full-screen framebuffer.
- [ ] Render a solid background, two white eye rectangles and a state-specific mouth; reject invalid expression values.
- [ ] Run `idf.py build`; expected: no camera startup calls and SPI symbols link.
- [ ] Commit: `git add components/BSP/FACE_DISPLAY && git commit -m "feat: add st7735 face display"`.

## Task 4: Shared-clock I2S Audio

**Files:**
- Create `components/BSP/AUDIO_I2S/audio_i2s.h`
- Create `components/BSP/AUDIO_I2S/audio_i2s.c`

**Produces:**

```c
#define AUDIO_I2S_SAMPLE_RATE_HZ 16000
#define AUDIO_I2S_SLOT_WIDTH_BITS 32
esp_err_t audio_i2s_init(void);
esp_err_t audio_i2s_read_pcm16(int16_t *samples, size_t capacity, size_t *read, uint32_t timeout_ms);
esp_err_t audio_i2s_write_pcm16(const int16_t *samples, size_t count, size_t *written, uint32_t timeout_ms);
```

- [ ] Allocate TX/RX simultaneously with `i2s_new_channel` in standard full-duplex master mode.
- [ ] Configure both channels as 16 kHz, 32-bit Philips stereo slots and GPIO `{bclk=40, ws=39, dout=37, din=38, mclk=I2S_GPIO_UNUSED}`.
- [ ] Convert the INMP441 left slot to PCM16 and expand PCM16 playback into identical 32-bit left/right slots.
- [ ] Add a compile-time 250 ms, low-volume 440 Hz startup diagnostic behind `CONFIG_ASSISTANT_PERIPHERAL_SELF_TEST`.
- [ ] Run `idf.py build`; expected: I2S symbols link and no SD driver is configured.
- [ ] Commit: `git add components/BSP/AUDIO_I2S && git commit -m "feat: add i2s audio primitives"`.

## Task 5: Integrate Startup and K230 UI Actions

**Files:**
- Modify `components/BSP/CMakeLists.txt`
- Modify `main/main.c`

**Consumes:** public APIs from Tasks 2--4.

- [ ] Add the three new source/include directory blocks using existing BSP glob style; add `esp_driver_spi` and `esp_driver_i2s` to `REQUIRES`.
- [ ] After Wi-Fi, vision-state and K230-link startup, invoke in order:

```c
ESP_ERROR_CHECK(face_display_init());
ESP_ERROR_CHECK(audio_i2s_init());
ESP_ERROR_CHECK(assistant_state_init());
ESP_ERROR_CHECK(assistant_state_set(ASSISTANT_STATE_IDLE, 0));
```

- [ ] Remove ASRPRO header, callback and `asrpro_link_start` from `main.c`; leave source files untouched.
- [ ] For inbound `K230_FRAME_UI_ACTION`, interpret action bytes `1=start`, `2=stop-record`, `3=stop-playback`, mapping them to LISTENING, TRANSCRIBING, IDLE. Log and ignore other values.
- [ ] Run `idf.py build`; expected: clean build.
- [ ] Commit: `git add components/BSP/CMakeLists.txt main/main.c && git commit -m "feat: initialize assistant peripherals"`.

## Task 6: Hardware Smoke Test Document

**Files:**
- Create `docs/hardware/esp32-peripheral-smoke-test.md`

- [ ] Document: power off before rewiring; verify 3V3/5V rails are not shorted; flash; confirm five face states from serial actions; run low-volume 440 Hz test; record a five-second I2S buffer and verify its byte count; confirm K230 UI transitions; never connect speaker terminals to GND.
- [ ] Run `git diff --check`; expected: no whitespace errors.
- [ ] Commit: `git add docs/hardware/esp32-peripheral-smoke-test.md && git commit -m "docs: add esp32 peripheral smoke test"`.

## Plan Self-Review

- Covers the specified pin map, no-camera/no-SD constraint, ST7735, I2S, UART state semantics and physical validation.
- Cloud ASR/LLM/TTS and K230 LVGL are intentionally deferred to the next two plans, after this hardware foundation is proven.
- All later task APIs are defined by earlier tasks; no placeholder or ambiguous interface remains.
