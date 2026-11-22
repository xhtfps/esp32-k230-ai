# Wi-Fi LED 状态提示实现计划

> **供执行代理使用：** 必须使用 `superpowers:subagent-driven-development`（推荐）或 `superpowers:executing-plans` 逐任务执行本计划；步骤使用复选框追踪。

**目标：** 用 GPIO48 的 WS2812 显示 Wi-Fi 三态：连接中红蓝交替、连接成功蓝灯常亮、连续五次失败红灯常亮。

**架构：** LED 模块管理唯一 FreeRTOS 闪烁任务；WIFI 事件回调仅设置连接中、成功或失败状态。失败状态每五秒重新发起一次连接。

**技术栈：** ESP-IDF 5.5.5、FreeRTOS、led_strip、esp_wifi、esp_event。

## 全局约束

- 启动和重连期间红、蓝两色每 500 ms 交替。
- 获取 IP 后蓝灯常亮。
- 连续五次失败后红灯常亮，并每五秒重新尝试连接。
- 不阻塞 Wi-Fi 事件回调，不创建重复 LED 任务。

### Task 1：实现 LED 状态任务

**文件：**

- 修改：`components/BSP/LED/led.h`
- 修改：`components/BSP/LED/led.c`

**接口：**

- 产出：`void led_set_wifi_connecting(void)`、`void led_set_wifi_connected(void)` 与 `void led_set_wifi_failed(void)`。

- [ ] **步骤 1：写出任务生命周期检查**

在未实现接口时由 `wifi.c` 调用，运行 `idf.py build`。

预期：链接失败并提示接口未定义。

- [ ] **步骤 2：实现唯一闪烁任务**

连接中任务循环调用 `led_set_preset_color(LED_COLOR_RED)`、延时 500 ms、`led_set_preset_color(LED_COLOR_BLUE)`、延时 500 ms。保存任务句柄；`led_set_wifi_connecting` 在句柄为 NULL 时创建任务；`led_set_wifi_connected` 和 `led_set_wifi_failed` 都停止任务，分别设置蓝灯或红灯。

- [ ] **步骤 3：构建验证与提交**

运行：`idf.py build`

预期：成功。

运行：`git add components/BSP/LED/led.h components/BSP/LED/led.c; git commit -m "feat: add Wi-Fi LED blink task"`

### Task 2：接入 Wi-Fi 状态事件

**文件：**

- 修改：`components/BSP/WIFI/wifi.c`

**接口：**

- 依赖：三个 `led_set_wifi_*` 状态接口。
- 产出：启动和重连时交替闪烁，获取 IP 时蓝灯常亮，五次失败后红灯常亮。

- [ ] **步骤 1：绑定事件处理**

包含 `led.h`；启动时设置连接中。断开事件将失败计数加一：前四次设置连接中并立即重连，第五次设置失败并延迟五秒重连；获取 IP 时清零失败计数并设置连接成功。

- [ ] **步骤 2：构建与实机验证**

运行：`idf.py build; idf.py -p COM19 flash monitor`

预期：连接期间 LED 红蓝交替；获取 IP 后蓝灯常亮；关闭热点后五次失败变为红灯常亮；恢复热点后回到蓝灯常亮。

- [ ] **步骤 3：提交**

运行：`git add components/BSP/WIFI/wifi.c; git commit -m "feat: indicate Wi-Fi status with LED"`

## 计划自检

- Task 1 提供不阻塞且无重复任务的三态 LED 生命周期；Task 2 将 Wi-Fi 事件映射为三种 LED 状态和五次失败重试。
