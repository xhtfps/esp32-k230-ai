# OV2640 MJPEG 网页预览实现计划

> **供执行代理使用：** 必须使用 `superpowers:subagent-driven-development`（推荐）或 `superpowers:executing-plans` 逐任务执行本计划；步骤使用复选框追踪。

**目标：** 让 ESP32-S3 连接指定热点后，通过浏览器实时预览 OV2640 的 VGA JPEG 画面。

**架构：** WIFI 模块负责 STA 入网与重连；WEB 模块负责 HTML 与 MJPEG 流。`/stream` 是唯一连续取帧者，每帧发送完成后立即归还，因此不再保留应用层的取帧任务。

**技术栈：** ESP-IDF 5.5.5、FreeRTOS、esp_wifi、esp_event、esp_http_server、esp32-camera。

## 全局约束

- 预览参数固定为 JPEG/VGA（640×480）、JPEG 质量 10、双 PSRAM 帧缓冲。
- Wi-Fi 凭据仅保留在未跟踪的本地配置头文件中，日志不得输出密码。
- HTTP 服务仅用于同一局域网内预览，不添加公网访问、认证、保存或 SD 卡功能。
- 每个成功获取的 `camera_fb_t` 必须通过 `camera_return_frame` 归还一次。

---

### Task 1：建立本地 Wi-Fi 配置与 STA 连接模块

**文件：**

- 新建：`components/BSP/WIFI/wifi_config.h.example`
- 新建：`components/BSP/WIFI/wifi.h`
- 新建：`components/BSP/WIFI/wifi.c`
- 修改：`.gitignore`
- 修改：`components/BSP/CMakeLists.txt`
- 修改：`main/CMakeLists.txt`

**接口：**

- 产出：`esp_err_t wifi_init_sta(void)`；连接成功后打印 IPv4 地址。

- [ ] **步骤 1：写出配置缺失检查**

让 `wifi.c` 包含 `wifi_config.h` 后运行 `idf.py build`。

预期：未复制配置模板时构建失败，提示找不到 `wifi_config.h`，防止凭据误提交。

- [ ] **步骤 2：添加本地配置模板与忽略规则**

模板定义：

```c
#define WIFI_SSID "在此填写热点名称"
#define WIFI_PASSWORD "在此填写热点密码"
```

在 `.gitignore` 中加入 `components/BSP/WIFI/wifi_config.h`；将模板复制为本地配置，写入用户提供的热点凭据。

- [ ] **步骤 3：实现 STA 重连**

`wifi_init_sta` 创建默认事件循环和 STA 网络接口，注册 `WIFI_EVENT_STA_DISCONNECTED` 与 `IP_EVENT_STA_GOT_IP`；断开时调用 `esp_wifi_connect()`，获取 IP 时以 `esp_ip4addr_ntoa` 记录访问地址。组件依赖加入 `esp_wifi esp_event esp_netif`。

- [ ] **步骤 4：构建验证与提交**

运行：`idf.py build`

预期：成功，且不会把 `wifi_config.h` 纳入 Git 状态。

运行：`git add .gitignore components/BSP/WIFI/wifi_config.h.example components/BSP/WIFI/wifi.h components/BSP/WIFI/wifi.c components/BSP/CMakeLists.txt main/CMakeLists.txt; git commit -m "feat: add Wi-Fi station support"`

### Task 2：实现 HTML 页面与 MJPEG 流服务

**文件：**

- 新建：`components/BSP/WEB/web.h`
- 新建：`components/BSP/WEB/web.c`
- 修改：`components/BSP/CMakeLists.txt`

**接口：**

- 依赖：`camera_get_frame()`、`camera_return_frame()`、`esp_http_server`。
- 产出：`esp_err_t web_start(void)`；`/` 返回 HTML，`/stream` 返回 multipart MJPEG。

- [ ] **步骤 1：写出路由链接检查**

在 `web.c` 仅声明 `web_start` 后让 `main.c` 调用，运行 `idf.py build`。

预期：在实现前链接失败，提示 `web_start` 未定义。

- [ ] **步骤 2：实现 `/` 路由**

HTML 响应包含 `<img src="/stream" alt="OV2640 实时画面">`，并设置响应类型 `text/html; charset=utf-8`。

- [ ] **步骤 3：实现 `/stream` 路由**

响应类型为 `multipart/x-mixed-replace;boundary=frame`。循环执行：获取帧；写入 `--frame`、`Content-Type: image/jpeg`、`Content-Length` 头；发送 JPEG 数据；在所有发送分支后归还帧。发送错误或客户端断开时退出循环并返回。

- [ ] **步骤 4：构建验证与提交**

运行：`idf.py build`

预期：成功，编译器检查 HTTP 与 CAMERA 接口。

运行：`git add components/BSP/WEB components/BSP/CMakeLists.txt; git commit -m "feat: add MJPEG web preview"`

### Task 3：接入启动顺序、VGA 参数与运行验证

**文件：**

- 修改：`components/BSP/CAMERA/camera.c`
- 修改：`main/main.c`

**接口：**

- 依赖：`camera_init()`、`camera_set_quality(10)`、`wifi_init_sta()`、`web_start()`。
- 产出：启动后连接热点并启动网页服务；不再运行独立取帧任务。

- [ ] **步骤 1：更新配置并移除竞争消费者**

将 `camera.c` 的 `.frame_size` 改为 `FRAMESIZE_VGA`。删除 `main.c` 中 `camera_frame_task`、`esp_timer.h` 和任务创建；依次调用 `camera_init()`、`camera_set_quality(10)`、`wifi_init_sta()`、`web_start()`。

- [ ] **步骤 2：构建验证**

运行：`idf.py build`

预期：成功，输出 `build/main.bin`。

- [ ] **步骤 3：烧录与浏览器验收**

运行：`idf.py -p COM19 flash monitor`

预期：串口显示热点连接成功和 `http://<IP>/`；手机浏览器访问该地址出现实时 VGA 画面。关闭再打开页面后，画面应恢复。

- [ ] **步骤 4：提交**

运行：`git add components/BSP/CAMERA/camera.c main/main.c; git commit -m "feat: start VGA MJPEG preview"`

## 计划自检

- 规格覆盖：Task 1 覆盖入网和重连；Task 2 覆盖网页与帧缓冲生命周期；Task 3 覆盖 VGA 参数、启动顺序、构建与实机浏览器验证。
- 完整性检查：每个任务均列出文件、接口、命令和可观察验收结果。
- 边界检查：凭据被忽略，未加入外网访问、认证、存储或 SD 卡功能。
