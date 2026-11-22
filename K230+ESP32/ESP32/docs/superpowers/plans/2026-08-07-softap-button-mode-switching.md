# SoftAP Button Mode Switching Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add GPIO0 short-press SoftAP provisioning and three-second long-press STA reconnection with credentials persisted in NVS.

**Architecture:** `WIFI` owns credentials, NVS persistence, Wi-Fi lifecycle, and the current AP/STA mode. `WEB` renders either provisioning or camera preview based on the Wi-Fi mode and submits credentials to `WIFI`. `EXIT` converts debounced GPIO0 press durations into the two mode requests; `main` initializes it after Wi-Fi and HTTP services are ready.

**Tech Stack:** ESP-IDF, FreeRTOS, esp_wifi, esp_netif, NVS, esp_http_server, Unity component tests.

## Global Constraints

- SoftAP SSID is exactly `OV2640-Setup`; SoftAP password is exactly `66666666`.
- A press released before three seconds starts provisioning; a press held at least three seconds starts STA mode.
- STA uses NVS credentials first and falls back to compile-time `WIFI_SSID` and `WIFI_PASSWORD` only if NVS has no credentials.
- Target Wi-Fi passwords are never logged or returned in HTTP responses.
- Invalid/overlong credentials, NVS failures, and mode-switch failures return clear errors without replacing saved credentials.
- Provisioning and STA-connecting states use red/blue alternating LED; IP acquisition is solid blue; five failed STA attempts are solid red followed by a five-second retry.
- A confirmed long press lights green once for `200 ms` before switching to STA; a short press does not use the green indicator.
- The project must build with ESP-IDF.

---

### Task 1: Make GPIO0 duration-aware and connect it to Wi-Fi mode APIs

**Files:**
- Modify: `components/BSP/EXIT/exit.c`
- Modify: `components/BSP/EXIT/exit.h`
- Modify: `main/main.c`
- Test: `components/BSP/EXIT/test/test_exit_press.c`

**Interfaces:**
- Consumes: `esp_err_t wifi_start_provisioning(void)` and `esp_err_t wifi_start_station(void)` from `wifi.h`.
- Produces: `exit_init(void)` that dispatches one short-press or long-press mode request per GPIO0 press.

- [ ] **Step 1: Write the failing press-classification test**

Create `components/BSP/EXIT/test/test_exit_press.c` with a pure duration classifier exposed as `exit_classify_press(uint32_t held_ms)`. Assert `exit_classify_press(2999) == EXIT_PRESS_SHORT` and `exit_classify_press(3000) == EXIT_PRESS_LONG`; name the tests after the incorrect boundary they catch.

- [ ] **Step 2: Run the test to verify it fails**

Run: `idf.py -C components/BSP/EXIT/test build`

Expected: build fails because `exit_classify_press` and the `exit_press_type_t` API do not exist.

- [ ] **Step 3: Implement the minimum press classification and dispatch**

Add `EXIT_LONG_PRESS_MS` set to `3000`, define `exit_press_type_t` and `exit_classify_press` in `exit.h`, and measure press duration using `esp_timer_get_time()` after debounce. On release, call `wifi_start_provisioning()` for a short press and `wifi_start_station()` for a long press; log only the selected mode and any `esp_err_t`, never credentials. Remove `led_next_color()` from the button path. Include `exit.h` in `main.c` and call `exit_init()` after `web_start()` succeeds.

- [ ] **Step 4: Run the test and project build**

Run: `idf.py -C components/BSP/EXIT/test build` and `idf.py build`

Expected: press test target and application build succeed.

- [ ] **Step 5: Commit the task**

Run: `git add components/BSP/EXIT/exit.c components/BSP/EXIT/exit.h components/BSP/EXIT/test/test_exit_press.c main/main.c && git commit -m "feat: map GPIO0 press duration to Wi-Fi mode requests"`

### Task 2: Add Wi-Fi mode state, NVS credentials, and safe AP/STA transitions

**Files:**
- Modify: `components/BSP/WIFI/wifi.c`
- Modify: `components/BSP/WIFI/wifi.h`
- Create: `components/BSP/WIFI/wifi_credentials.h`
- Test: `components/BSP/WIFI/test/test_wifi_credentials.c`

**Interfaces:**
- Consumes: `wifi_start_provisioning(void)`, `wifi_start_station(void)`, and `wifi_save_credentials(const char *ssid, const char *password)` called by EXIT and WEB.
- Produces: mode-safe, serialized AP/STA transitions and validated NVS credential operations.

- [ ] **Step 1: Write the failing credential validation test**

Create `components/BSP/WIFI/test/test_wifi_credentials.c` for a pure `wifi_credentials_validate` helper. Assert that an empty SSID is rejected, a 32-byte SSID and 63-byte password are accepted, and a 33-byte SSID or 64-byte password is rejected with `ESP_ERR_INVALID_ARG`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `idf.py -C components/BSP/WIFI/test build`

Expected: build fails because the validation helper and credential type are absent.

- [ ] **Step 3: Implement validated atomic credential persistence and transition APIs**

Define `wifi_credentials_t` with ESP-IDF-sized SSID and password buffers, plus `wifi_credentials_validate`. Store validated credential records under an NVS namespace such as `wifi_cfg`; write SSID and password, then commit, and return an error without changing the active configuration if any write fails. During station startup, read NVS first and use the compile-time defaults only for `ESP_ERR_NVS_NOT_FOUND`. Add `wifi_start_provisioning()` to stop an active STA, configure `WIFI_MODE_AP`, set the exact SoftAP credentials, and start it. Add `wifi_start_station()` to stop an active AP, configure `WIFI_MODE_STA`, load credentials, set configuration, and start/connect. Protect transitions using a FreeRTOS mutex so button, HTTP, and event callbacks cannot race. Preserve the existing retry and LED policy; cancel stale retry timers whenever leaving STA.

- [ ] **Step 4: Run the test and project build**

Run: `idf.py -C components/BSP/WIFI/test build` and `idf.py build`

Expected: validation target and application build succeed.

- [ ] **Step 5: Commit the task**

Run: `git add components/BSP/WIFI/wifi.c components/BSP/WIFI/wifi.h components/BSP/WIFI/wifi_credentials.h components/BSP/WIFI/test/test_wifi_credentials.c && git commit -m "feat: add persistent Wi-Fi mode switching"`

### Task 3: Provide a mode-aware provisioning web endpoint

**Files:**
- Modify: `components/BSP/WEB/web.c`
- Modify: `components/BSP/WEB/web.h`
- Test: `components/BSP/WEB/test/test_web_provisioning.c`

**Interfaces:**
- Consumes: `wifi_is_provisioning(void)`, `wifi_save_credentials(const char *, const char *)`, and `wifi_start_station(void)` from `wifi.h`.
- Produces: `GET /` that serves a setup form in AP mode and MJPEG preview in STA mode, plus `POST /provision` for form submission.

- [ ] **Step 1: Write the failing request-validation test**

Create `components/BSP/WEB/test/test_web_provisioning.c` around a pure URL-form decoder/validator API. Assert that `ssid=Lab&password=secret` produces `Lab` and `secret`, while a request with missing `ssid` produces `ESP_ERR_INVALID_ARG`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `idf.py -C components/BSP/WEB/test build`

Expected: build fails because the provisioning form decoder is absent.

- [ ] **Step 3: Implement the setup page and protected submission path**

Keep the existing `/stream` handler. Make `GET /` return a setup page containing `ssid` and password inputs only while `wifi_is_provisioning()` is true; otherwise return the existing preview page. Add `POST /provision`, bound request-body and field lengths to ESP-IDF Wi-Fi limits, decode form data, call `wifi_save_credentials`, and then call `wifi_start_station` only after the NVS commit succeeds. Return HTTP 400 for empty/overlong input, HTTP 500 for persistence/mode failures, and a success status page on a successful handoff. Never include the submitted password in logs, HTML, or error text.

- [ ] **Step 4: Run the test and project build**

Run: `idf.py -C components/BSP/WEB/test build` and `idf.py build`

Expected: web validation target and application build succeed.

- [ ] **Step 5: Commit the task**

Run: `git add components/BSP/WEB/web.c components/BSP/WEB/web.h components/BSP/WEB/test/test_web_provisioning.c && git commit -m "feat: add SoftAP provisioning web page"`

### Task 4: Build and perform the on-device acceptance pass

**Files:**
- Modify: `docs/superpowers/specs/2026-08-07-softap-provisioning-design.md`

**Interfaces:**
- Consumes: the complete application mode-switch and web APIs.
- Produces: verified build evidence and documented manual hardware results.

- [ ] **Step 1: Build the production configuration**

Run: `idf.py build`

Expected: exit status zero with a generated firmware image.

- [ ] **Step 2: Flash and exercise short press**

Run: `idf.py -p <SERIAL_PORT> flash monitor`

Expected: after short pressing GPIO0, the device advertises `OV2640-Setup`; a phone can open the setup page at the AP gateway address.

- [ ] **Step 3: Exercise form submission and long press**

Submit a valid target network through the form, confirm the serial monitor logs an IP address without logging the password, then short press to re-enter AP mode and hold GPIO0 for at least three seconds. Expected: the AP stops, STA reconnects using saved NVS credentials, LED becomes blue after IP acquisition, and `/` serves the MJPEG preview from the STA IP.

- [ ] **Step 4: Commit verification documentation**

Record the actual serial port, build result, and hardware observations in the specification verification section without recording any target Wi-Fi password. Run: `git add docs/superpowers/specs/2026-08-07-softap-provisioning-design.md && git commit -m "docs: verify SoftAP provisioning mode switching"`
