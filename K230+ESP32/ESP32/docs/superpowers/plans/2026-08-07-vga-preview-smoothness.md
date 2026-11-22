# VGA Preview Smoothness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the VGA MJPEG preview smoother without changing its JPEG quality setting or image resolution.

**Architecture:** Keep OV2640 output as VGA JPEG quality 10. Increase capture clock and PSRAM buffering, choose latest-frame capture to prevent stale preview frames, and add an in-stream FPS measurement for board-side verification.

**Tech Stack:** ESP-IDF, esp32-camera, PSRAM, esp_http_server.

## Global Constraints

- Keep `FRAMESIZE_VGA` and JPEG quality `10` unchanged.
- Use a 20 MHz XCLK, three PSRAM frame buffers, and `CAMERA_GRAB_LATEST`.
- Do not log image contents or add per-frame logging that would reduce stream performance.
- Build with ESP-IDF after the change.

---

### Task 1: Tune capture buffering and add aggregate FPS telemetry

**Files:**
- Modify: `components/BSP/CAMERA/camera.c`
- Modify: `components/BSP/WEB/web.c`
- Test: `tests/test_camera_preview_config.c`

**Interfaces:**
- Consumes: the existing `camera_get_frame` and `camera_return_frame` API.
- Produces: a VGA/JPEG preview configured for latest-frame capture and periodic, aggregate FPS logs.

- [ ] **Step 1: Write the failing configuration-contract test**

Create `tests/test_camera_preview_config.c` that includes a new pure camera preview configuration header. Assert the constants are exactly `20000000` for XCLK, `3` for frame-buffer count, and `CAMERA_GRAB_LATEST` for grab mode; the test names must identify the stale-frame or low-clock regression they catch.

- [ ] **Step 2: Run the test to verify it fails**

Run: `xtensa-esp32s3-elf-gcc -std=c11 -I components/BSP/CAMERA -c tests/test_camera_preview_config.c -o build/test_camera_preview_config.o`

Expected: compilation fails because the preview configuration header does not exist.

- [ ] **Step 3: Implement the camera and stream changes**

Create a focused configuration header defining `CAMERA_XCLK_HZ` as `20000000`, `CAMERA_FRAME_BUFFER_COUNT` as `3`, and `CAMERA_GRAB_LATEST`. Use those values in `camera.c`, retain `FRAMESIZE_VGA` and JPEG quality `10`, and correct the startup log to say VGA. In `stream_handler`, count completed frames and use `esp_timer_get_time()` to emit one FPS log no more than once each five seconds; reset the counter after each report.

- [ ] **Step 4: Run the test and build**

Run: `xtensa-esp32s3-elf-gcc -std=c11 -I components/BSP/CAMERA -c tests/test_camera_preview_config.c -o build/test_camera_preview_config.o` and `idf.py build`

Expected: configuration test compiles and ESP-IDF produces `build/main.bin` without warnings.

- [ ] **Step 5: Commit the task**

Run: `git add components/BSP/CAMERA/camera.c components/BSP/CAMERA/camera_preview_config.h components/BSP/WEB/web.c tests/test_camera_preview_config.c && git commit -m "perf: smooth VGA camera preview"`
