# K230 + ESP32 Vision Gateway Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Connect a K230 V3.0 camera/NPU module to the ESP32-S3 over UART so K230 performs local vision inference and ESP32 provides Wi-Fi, web control, and optional cloud multimodal analysis.

**Architecture:** K230 sends compact local-result JSON frames continuously and sends a JPEG only after an ESP32 request or local event. ESP32 owns a framed UART link, recent vision state, JPEG reassembly, and the existing HTTPS LLM client; the OV2640 path is removed from the normal ESP32 flow. K230 and ESP32 communicate over 3.3 V UART2-to-UART1 with common ground.

**Tech Stack:** ESP-IDF 5.5.5 C, FreeRTOS UART driver, ESP-IDF `esp_http_client`, mbedTLS certificate bundle, K230 CanMV MicroPython, K230 `PipeLine`/`AIBase`, CRC16-CCITT and CRC32.

## Global Constraints

- K230 UART2 uses GPIO5/TXD (40-pin header pin 17) and GPIO6/RXD (header pin 18), based on the supplied K230 V3.0 pinout.
- UART starts at `921600 8N1` and must support a `460800 8N1` fallback.
- All frames use magic `0xA5 0x5A`, version `0x01`, little-endian lengths, sequence number, and CRC16-CCITT.
- Individual UART frame payloads are limited to 65535 bytes by the 16-bit length field; JPEG images are chunked at no more than 2048 bytes and are limited to 256 KiB in aggregate on ESP32.
- API keys remain on ESP32 and are never sent to K230.
- SoftAP/provisioning code is not changed by this feature.
- Every task ends with a focused test and a separate commit.

---

### Task 1: Freeze shared protocol constants and host-side test vectors

**Files:**
- Create: `k230/vision_protocol.py`
- Create: `k230/protocol_vectors.json`
- Create: `components/BSP/K230_LINK/k230_protocol.h`
- Create: `components/BSP/K230_LINK/k230_protocol.c`
- Create: `tests/test_k230_protocol.c`
- Modify: `components/BSP/CMakeLists.txt`

**Interfaces:**
- K230 produces `encode_frame(frame_type, sequence, payload)` and consumes `FrameDecoder.feed(bytes) -> list[Frame]`.
- ESP32 exposes `k230_protocol_encode(uint8_t type, uint16_t sequence, const uint8_t *payload, uint16_t length, uint8_t *out, size_t out_size, size_t *written)` and a byte-wise decoder state.
- The shared frame layout is `magic[2] | version | type | length_le[2] | sequence_le[2] | payload | crc16_le[2]`.

- [ ] **Step 1: Write failing protocol tests** for an empty payload, a JSON payload, a split frame arriving one byte at a time, a bad magic byte, a frame payload of 65536 bytes, and a CRC mismatch.
- [ ] **Step 2: Run the protocol tests** with the repository's Unity test command and confirm the new tests fail because the protocol implementation is absent.
- [ ] **Step 3: Implement the C encoder/decoder** in `k230_protocol.c` with a bounded receive buffer, byte-wise magic resynchronization, little-endian parsing, and CRC16-CCITT over `version` through `payload`.
- [ ] **Step 4: Implement the matching MicroPython encoder/decoder** in `k230/vision_protocol.py`; use the same vectors in `protocol_vectors.json` so both sides produce byte-identical frames.
- [ ] **Step 5: Run C tests and a host Python vector check**; verify all vectors pass and malformed frames are rejected without unbounded allocation.
- [ ] **Step 6: Commit** with `git add k230 components/BSP/K230_LINK tests components/BSP/CMakeLists.txt && git commit -m "feat: add K230 UART frame protocol"`.

### Task 2: Add the ESP32 K230 UART link task

**Files:**
- Create: `components/BSP/K230_LINK/k230_link.h`
- Create: `components/BSP/K230_LINK/k230_link.c`
- Create: `components/BSP/K230_LINK/k230_link_config.h`
- Modify: `components/BSP/CMakeLists.txt`
- Modify: `main/main.c`

**Interfaces:**
- `esp_err_t k230_link_start(const k230_link_config_t *config)` starts the UART driver and RX task.
- `esp_err_t k230_link_send(uint8_t type, const uint8_t *payload, size_t length)` queues one framed message and rejects lengths above 65535.
- `esp_err_t k230_link_request_jpeg(uint16_t request_id)` sends type `0x10` with the request ID.
- `void k230_link_set_frame_callback(k230_link_frame_callback_t callback, void *context)` registers decoded-frame delivery.
- Default config uses ESP32 UART1 on the board's right-side G1/G2 header, 921600 baud, RX buffer 16 KiB, and TX buffer 4 KiB; the config header must allow these pins to be changed without touching the parser.

- [ ] **Step 1: Add a failing initialization test** that checks the config defaults and rejects a zero-length callback or a frame payload larger than 65535 bytes.
- [ ] **Step 2: Implement UART installation and the RX task** using `uart_driver_install`, `uart_param_config`, and `uart_set_pin`; feed received bytes into the protocol decoder and invoke the callback only for valid frames.
- [ ] **Step 3: Implement bounded TX and request framing**; return `ESP_ERR_INVALID_SIZE` for oversized payloads and `ESP_ERR_TIMEOUT` when the UART queue cannot accept a frame.
- [ ] **Step 4: Add a heartbeat task** that sends type `0x02` every 2 seconds and records the last valid RX timestamp; mark the link offline after 6 seconds without a valid frame.
- [ ] **Step 5: Run the ESP-IDF build and the host protocol tests**; confirm the UART component links without re-enabling the ESP32 camera.
- [ ] **Step 6: Commit** with `git add components/BSP/K230_LINK components/BSP/CMakeLists.txt main/main.c tests && git commit -m "feat: add ESP32 K230 UART link"`.

### Task 3: Add K230 MicroPython heartbeat and local-result sender

**Files:**
- Create: `k230/vision_link.py`
- Create: `k230/vision_app.py`
- Create: `k230/README.md`

**Interfaces:**
- `VisionLink.write_frame(frame_type, sequence, payload)` sends a protocol frame on K230 UART2.
- `VisionLink.poll()` decodes ESP32 commands and returns `(frame_type, payload)` without blocking longer than 20 ms.
- `VisionApp.run_once()` returns a compact UTF-8 JSON result and does not expose K230 model internals to ESP32.

- [ ] **Step 1: Write a host Python test** for `VisionLink` frame encoding, heartbeat payloads, and JSON serialization of a face result.
- [ ] **Step 2: Configure K230 FPIOA** for GPIO5/UART2_TXD and GPIO6/UART2_RXD and initialize UART2 at 921600 8N1; keep the baud rate in one constant for fallback.
- [ ] **Step 3: Implement heartbeat and result throttling**: send a heartbeat every 2 seconds and send changed results immediately or one summary at least every 200 ms.
- [ ] **Step 4: Adapt the supplied `face_detection.py` pipeline** so the first model emits `{task,count,items}` JSON while display remains optional; do not add OCR or multiple models in this task.
- [ ] **Step 5: Run the script on K230 with a loopback or ESP32 receiver** and verify 10 minutes of result frames without decoder desynchronization.
- [ ] **Step 6: Commit** with `git add k230 && git commit -m "feat: send K230 local vision results"`.

### Task 4: Add ESP32 vision state and JPEG reassembly

**Files:**
- Create: `components/BSP/VISION/vision_state.h`
- Create: `components/BSP/VISION/vision_state.c`
- Create: `tests/test_vision_state.c`
- Modify: `components/BSP/CMakeLists.txt`
- Modify: `main/main.c`

**Interfaces:**
- `esp_err_t vision_state_init(void)` initializes the mutex and bounded state.
- `esp_err_t vision_state_accept_result(const uint8_t *json, size_t length)` stores the latest valid result and timestamp.
- `esp_err_t vision_state_begin_jpeg(uint16_t request_id)` starts one reassembly buffer; total length arrives in the completion frame.
- `esp_err_t vision_state_append_jpeg(const uint8_t *chunk, size_t length)` appends a bounded chunk.
- `esp_err_t vision_state_finish_jpeg(uint32_t expected_crc32, const uint8_t **jpeg, size_t *jpeg_length)` validates and transfers ownership to the caller.
- `void vision_state_release_jpeg(const uint8_t *jpeg)` releases the buffer transferred by `vision_state_finish_jpeg`.
- `void vision_state_abort_jpeg(void)` frees an incomplete image.

- [ ] **Step 1: Write failing tests** for result replacement, stale-result age, 256 KiB acceptance, 256 KiB+1 rejection, CRC success, CRC failure, and concurrent status reads.
- [ ] **Step 2: Implement the state mutex and result buffer** with a fixed 1024-byte JSON limit and an explicit `link_online` flag.
- [ ] **Step 3: Implement JPEG reassembly** using PSRAM first and heap fallback; reject a second image while one is active and release buffers on every error path.
- [ ] **Step 4: Register the K230 frame callback** so types `0x01`, `0x02`, `0x11`, and `0x12` update state; parse `0x11` as `request_id(2)+offset(4)+data`, send `0x13` only after a successful CRC32 check, and call `vision_state_release_jpeg` after the LLM request consumes the completed image.
- [ ] **Step 5: Run Unity tests and a full ESP-IDF build**; verify that the existing SoftAP and STA code is unchanged.
- [ ] **Step 6: Commit** with `git add components/BSP/VISION components/BSP/CMakeLists.txt main/main.c tests && git commit -m "feat: add vision state and JPEG reassembly"`.

### Task 5: Route `/analyze` through K230 JPEG and the existing LLM client

**Files:**
- Modify: `components/BSP/LLM/llm.h`
- Modify: `components/BSP/LLM/llm.c`
- Modify: `components/BSP/WEB/web.c`
- Modify: `components/BSP/WEB/web.h`
- Modify: `main/main.c`

**Interfaces:**
- Add `esp_err_t llm_analyze_jpeg(const uint8_t *jpeg, size_t jpeg_length, char *answer, size_t answer_size)`; it must not access `camera_get_frame()`.
- `/analyze` first calls `k230_link_request_jpeg(request_id)`, waits up to 10 seconds for `vision_state_finish_jpeg`, then calls `llm_analyze_jpeg`.
- `/` displays link status, latest local result age, and a disabled analysis message when K230 is offline.

- [ ] **Step 1: Add a unit-level request-body test** that verifies a supplied JPEG becomes a `data:image/jpeg;base64,...` message and that an empty or oversized JPEG is rejected.
- [ ] **Step 2: Refactor the existing LLM request builder** to accept an external JPEG buffer; preserve the existing endpoint, model, TLS bundle, timeout, and response parsing.
- [ ] **Step 3: Replace the current camera capture in `/analyze`** with the K230 request/wait sequence; return HTTP 503 for an offline K230 and HTTP 504 for a JPEG timeout.
- [ ] **Step 4: Update the status page** to show `K230 online/offline`, last local result, and a single “拍照并交给大模型分析” action.
- [ ] **Step 5: Run a full build** and test malformed K230 frames, no-K230 timeout, API-key failure, and successful JPEG analysis using a recorded JPEG fixture.
- [ ] **Step 6: Commit** with `git add components/BSP/LLM components/BSP/WEB main/main.c tests && git commit -m "feat: analyze K230 JPEG with cloud vision"`.

### Task 6: Hardware-in-the-loop validation and handoff documentation

**Files:**
- Modify: `k230/README.md`
- Create: `docs/superpowers/specs/2026-08-08-k230-esp32-vision-gateway-test-report.md`
- Modify: `docs/superpowers/specs/2026-08-08-k230-esp32-vision-gateway-design.md` only if a tested pin or timing value must be corrected

**Interfaces:**
- Test report records firmware versions, UART pins, baud rate, test duration, frame counts, CRC errors, JPEG size, cloud latency, and failure behavior.

- [ ] **Step 1: Wire K230 GPIO5/GPIO6 to the selected ESP32 UART pins** with TX/RX crossed and common ground; verify both boards remain 3.3 V logic.
- [ ] **Step 2: Run the 1000-frame protocol test** and record CRC error count and resynchronization behavior.
- [ ] **Step 3: Run local vision for 10 minutes** and verify result timestamps, offline detection, and recovery after unplugging/reconnecting K230.
- [ ] **Step 4: Run 20 JPEG `/analyze` requests** and record success rate, JPEG sizes, UART transfer time, HTTPS latency, and cloud response errors.
- [ ] **Step 5: Verify security constraints** by confirming no API key appears in K230 files, UART payloads, serial logs, or Flash data.
- [ ] **Step 6: Commit the test report** with `git add k230 docs/superpowers/specs/2026-08-08-k230-esp32-vision-gateway-test-report.md docs/superpowers/specs/2026-08-08-k230-esp32-vision-gateway-design.md && git commit -m "test: validate K230 ESP32 vision gateway"`.

## Self-review checklist

- Protocol constants, frame layout, message types, CRC coverage, and size limits are defined once in Task 1 and consumed consistently by Tasks 2--5.
- No task changes SoftAP/provisioning behavior; only the `/analyze` data source changes from local OV2640 to K230 JPEG.
- All buffers have explicit upper bounds, and all JPEG failure paths release ownership.
- The plan includes local-result operation without cloud access and cloud fallback only on demand.
- The plan contains no API key, Wi-Fi password, or device-specific secret.
- Each task has a focused test and a separate commit.
