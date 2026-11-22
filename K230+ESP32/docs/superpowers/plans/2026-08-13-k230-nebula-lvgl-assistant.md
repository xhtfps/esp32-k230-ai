# K230「星云智脑」LVGL 二级助手页 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不影响 K230 摄像头主预览的前提下，为 CanMV v1.2.2 增加通过边缘滑动进入的「星云智脑」LVGL 二级助手页，并与 ESP32 会话状态协议连通。

**Architecture:** 现有 Sensor/Display 视频通路继续负责实时画面。新增的 `assistant_store` 保存由 UART 和触控产生的状态，`assistant_ui` 是唯一操作 LVGL 的模块，`touch_service` 将触点转成边缘滑动和按住说话动作。所有模块由 `main.py` 主循环轮询，不让 UART 或推理代码直接操作 LVGL。

**Tech Stack:** CanMV K230 v1.2.2、MicroPython、`lvgl`、`machine.TOUCH`、`media.Display`、现有 `VisionLink` / 自定义 CRC16 UART 协议、CPython `unittest`。

## Global Constraints

- 目标屏幕固定为 ST7701 800×480 横屏；视频预览始终优先。
- AI 页面是 OSD 二级层：右边缘向左滑打开，左边缘向右滑关闭。
- K230 不采集音频、不保存云端密钥；音频与云端调用只在 ESP32。
- UART 固定 GPIO5 TX、GPIO6 RX、921600 8N1；保留 `0x01`、`0x02`、`0x10`--`0x13`、`0x7F` 的既有语义。
- 新帧为 `0x20 UI_ACTION`、`0x21 ASSISTANT_STATE`、`0x22 TRANSCRIPT`、`0x23 ANSWER_CHUNK`、`0x24 ASSISTANT_ERROR`。
- UART 接收任务、视觉推理任务不得创建、销毁或更新 LVGL 控件；仅 `assistant_ui.refresh()` 允许操作 LVGL。
- 语音球动画只使用圆环尺寸/透明度/线宽变化，周期 120 ms；不能每帧重建对象或使用全屏模糊。
- 所有 K230 Python 模块必须兼容 MicroPython：不使用 `dataclasses`、`enum`、`threading` 或 CPython 专属缓冲区删除操作。

---

## File Structure

- Create `k230/assistant_store.py`: 会话、链路、文本和视觉摘要的有界状态容器；定义 UI 动作常量。
- Create `k230/touch_service.py`: 触控 API 适配、边缘滑动和语音球按住/松开识别；产出动作，不依赖 LVGL。
- Create `k230/assistant_ui.py`: LVGL 对象树、星云智脑主题、状态差量刷新与语音球动画；唯一 LVGL 所有者。
- Create `k230/test_assistant_store.py`: App Store 的 CPython 单元测试。
- Create `k230/test_touch_service.py`: 手势状态机的 CPython 单元测试。
- Create `k230/test_assistant_protocol.py`: 新 UART 帧解析与 UI 动作编码的 CPython 单元测试。
- Create `k230/diagnose_lvgl_touch.py`: 仅上板运行的导入、显示和触控坐标诊断脚本。
- Modify `k230/vision_protocol.py`: 定义 `0x20`--`0x24` 帧常量。
- Modify `k230/vision_link.py`: 增加 UI 动作发送及已解码会话帧辅助判断。
- Modify `k230/vision_app.py`: 接收会话帧并投递 App Store；保留 JPEG 行为。
- Modify `k230/main.py`: 初始化 UI/触控/App Store，在视频循环中轮询并刷新；不重写现有摄像头通路。
- Modify `k230/README.md`: 更新部署文件、手势、协议和诊断步骤。

## Task 1: LVGL 与触控上板诊断

**Files:**
- Create: `k230/diagnose_lvgl_touch.py`
- Modify: `k230/README.md`

**Consumes:** CanMV v1.2.2 已验证可导入的 `lvgl`、`machine.TOUCH`。

**Produces:** 可确认 v1.2.2 实际 `TOUCH` 初始化方法、读点方法、坐标范围与 LVGL 显示初始化兼容性的上板日志。

- [ ] **Step 1: 写入最小诊断脚本**

```python
import time
import lvgl as lv
from machine import TOUCH

SCREEN_WIDTH = 800
SCREEN_HEIGHT = 480

def main():
    print("LVGL module:", lv)
    print("TOUCH class:", TOUCH)
    print("TOUCH attributes:", dir(TOUCH))
    screen = lv.screen_active()
    label = lv.label(screen)
    label.set_text("LVGL / TOUCH diagnostic")
    label.center()
    while True:
        lv.timer_handler()
        time.sleep_ms(10)

if __name__ == "__main__":
    main()
```

- [ ] **Step 2: 部署到 K230 并记录 API 证据**

将脚本上传为 `/sdcard/diagnose_lvgl_touch.py`，在 CanMV IDE 运行。

预期：串口输出存在 `TOUCH attributes`；LCD 显示 `LVGL / TOUCH diagnostic`。若 `lv.screen_active()` 不存在，记录 `dir(lv)` 输出，且仅根据实际输出替换这一行，不修改后续模块。

- [ ] **Step 3: 扩展为触控读点诊断**

在确认 `TOUCH` 构造函数和读取方法后，加入：

```python
touch = TOUCH(...actual constructor from Step 2...)
while True:
    point = touch.read(...actual read call from Step 2...)
    print("touch:", point)
    lv.timer_handler()
    time.sleep_ms(20)
```

预期：点击屏幕四角和中心时能得到稳定坐标；记录按下、移动、松开在 v1.2.2 中的实际返回结构。

- [ ] **Step 4: 更新部署说明**

在 `README.md` 增加“先运行 `diagnose_lvgl_touch.py`，记录触控构造函数和读点返回值；诊断未通过前不得替换主程序”的说明。

- [ ] **Step 5: Commit**

```bash
git add k230/diagnose_lvgl_touch.py k230/README.md
git commit -m "test: add K230 LVGL touch diagnostic"
```

## Task 2: 有界助手状态容器

**Files:**
- Create: `k230/assistant_store.py`
- Create: `k230/test_assistant_store.py`

**Consumes:** 无硬件依赖。

**Produces:**

```python
STATE_IDLE = 0
STATE_LISTENING = 1
STATE_TRANSCRIBING = 2
STATE_THINKING = 3
STATE_SPEAKING = 4
STATE_ERROR = 5

ACTION_START_LISTENING = 1
ACTION_STOP_LISTENING = 2
ACTION_STOP_PLAYBACK = 3
ACTION_ANALYZE_VISION = 4

class AssistantStore:
    def apply_state(self, state, session_id, error_code=0): ...
    def set_transcript(self, text): ...
    def append_answer(self, text): ...
    def set_error(self, error_code, message): ...
    def set_link_online(self, online, now_ms): ...
    def is_link_online(self, now_ms, timeout_ms=3000): ...
    def snapshot(self): ...
```

- [ ] **Step 1: 写入失败测试**

```python
from assistant_store import AssistantStore, STATE_LISTENING

def test_state_snapshot_preserves_session_and_error():
    store = AssistantStore()
    store.apply_state(STATE_LISTENING, 0x1234, 7)
    state = store.snapshot()
    assert state["state"] == STATE_LISTENING
    assert state["session_id"] == 0x1234
    assert state["error_code"] == 7

def test_answer_is_bounded_and_marks_truncation():
    store = AssistantStore()
    store.append_answer("A" * 500)
    state = store.snapshot()
    assert len(state["answer"].encode("utf-8")) <= 384
    assert state["answer"].endswith("...")

def test_link_expires_after_three_seconds():
    store = AssistantStore()
    store.set_link_online(True, 1000)
    assert store.is_link_online(3999)
    assert not store.is_link_online(4001)
```

- [ ] **Step 2: 运行测试确认 RED**

Run: `python -m unittest k230.test_assistant_store -v`

Expected: FAIL with `ModuleNotFoundError: No module named 'assistant_store'`.

- [ ] **Step 3: 实现最小状态容器**

```python
def _bounded_utf8(text, limit):
    raw = str(text).encode("utf-8")
    if len(raw) <= limit:
        return str(text), False
    return raw[:limit - 3].decode("utf-8", "ignore") + "...", True

class AssistantStore:
    def __init__(self):
        self._state = STATE_IDLE
        self._session_id = 0
        self._error_code = 0
        self._transcript = ""
        self._answer = ""
        self._link_online = False
        self._last_link_ms = 0
```

实现接口中列出的所有方法；`append_answer` 对当前回答拼接后统一调用 `_bounded_utf8(..., 384)`；`snapshot` 返回新字典而不是内部字典。

- [ ] **Step 4: 运行测试确认 GREEN**

Run: `python -m unittest k230.test_assistant_store -v`

Expected: PASS (3 tests).

- [ ] **Step 5: Commit**

```bash
git add k230/assistant_store.py k230/test_assistant_store.py
git commit -m "feat: add K230 assistant state store"
```

## Task 3: 纯 Python 边缘手势状态机

**Files:**
- Create: `k230/touch_service.py`
- Create: `k230/test_touch_service.py`

**Consumes:** `ACTION_START_LISTENING`、`ACTION_STOP_LISTENING` from `assistant_store.py`。

**Produces:**

```python
GESTURE_NONE = 0
GESTURE_OPEN_ASSISTANT = 1
GESTURE_CLOSE_ASSISTANT = 2

class TouchService:
    def feed(self, x, y, pressed, assistant_visible): ...
```

`feed` 返回一个字典：`{"gesture": int, "action": int}`。边缘宽度为 42 px、最小水平位移为 96 px、最大垂直漂移为 72 px。语音球矩形固定为 `(70, 145, 240, 240)`，仅当助手页已显示时对其产生按住/松开动作。

- [ ] **Step 1: 写入失败测试**

```python
from assistant_store import ACTION_START_LISTENING, ACTION_STOP_LISTENING
from touch_service import GESTURE_OPEN_ASSISTANT, TouchService

def test_right_edge_swipe_opens_assistant():
    service = TouchService()
    service.feed(790, 220, True, False)
    result = service.feed(650, 225, False, False)
    assert result["gesture"] == GESTURE_OPEN_ASSISTANT

def test_voice_ball_emits_press_and_release_actions():
    service = TouchService()
    assert service.feed(150, 220, True, True)["action"] == ACTION_START_LISTENING
    assert service.feed(150, 220, False, True)["action"] == ACTION_STOP_LISTENING
```

- [ ] **Step 2: 运行测试确认 RED**

Run: `python -m unittest k230.test_touch_service -v`

Expected: FAIL with `ModuleNotFoundError: No module named 'touch_service'`.

- [ ] **Step 3: 实现最小状态机**

```python
class TouchService:
    def __init__(self):
        self._down = False
        self._start_x = 0
        self._start_y = 0
        self._voice_pressed = False

    def feed(self, x, y, pressed, assistant_visible):
        result = {"gesture": GESTURE_NONE, "action": ACTION_NONE}
        # pressed rising edge: capture origin or emit voice start.
        # pressed falling edge: classify a one-shot edge swipe or emit voice stop.
        return result
```

分类只在松开时发生；在助手页隐藏时从右边缘向左判定打开，在助手页显示时从左边缘向右判定关闭。任何无效坐标、短滑动和大幅垂直漂移返回 `GESTURE_NONE`。

- [ ] **Step 4: 运行测试确认 GREEN**

Run: `python -m unittest k230.test_touch_service -v`

Expected: PASS (2 tests).

- [ ] **Step 5: Commit**

```bash
git add k230/touch_service.py k230/test_touch_service.py
git commit -m "feat: add K230 edge gesture service"
```

## Task 4: UART 会话帧与 UI 动作

**Files:**
- Modify: `k230/vision_protocol.py`
- Modify: `k230/vision_link.py`
- Create: `k230/test_assistant_protocol.py`

**Consumes:** `encode_frame`、`FrameDecoder`、动作常量。

**Produces:**

```python
FRAME_UI_ACTION = 0x20
FRAME_ASSISTANT_STATE = 0x21
FRAME_TRANSCRIPT = 0x22
FRAME_ANSWER_CHUNK = 0x23
FRAME_ASSISTANT_ERROR = 0x24

VisionLink.send_ui_action(action)
VisionLink.is_assistant_frame(frame)
```

`FRAME_ASSISTANT_STATE` 负载必须为 4 字节 `{state, session_lo, session_hi, error_code}`。`FRAME_UI_ACTION` 负载必须为 1 字节。

- [ ] **Step 1: 写入失败测试**

```python
from vision_link import VisionLink
from vision_protocol import FRAME_UI_ACTION, FrameDecoder

class MockUart:
    def __init__(self): self.writes = []
    def write(self, data): self.writes.append(bytes(data)); return len(data)
    def read(self): return b""

def test_ui_action_encodes_one_byte_payload():
    uart = MockUart()
    VisionLink(uart=uart).send_ui_action(1)
    frame = FrameDecoder().feed(uart.writes[0])[0]
    assert frame[0] == FRAME_UI_ACTION
    assert frame[2] == b"\x01"

def test_invalid_ui_action_is_rejected():
    uart = MockUart()
    try:
        VisionLink(uart=uart).send_ui_action(99)
        assert False, "expected ValueError"
    except ValueError:
        pass
```

- [ ] **Step 2: 运行测试确认 RED**

Run: `python -m unittest k230.test_assistant_protocol -v`

Expected: FAIL with `ImportError` for `FRAME_UI_ACTION` or `AttributeError` for `send_ui_action`.

- [ ] **Step 3: 实现帧常量和接口**

在 `vision_protocol.py` 紧跟 JPEG ACK 常量添加：

```python
FRAME_UI_ACTION = 0x20
FRAME_ASSISTANT_STATE = 0x21
FRAME_TRANSCRIPT = 0x22
FRAME_ANSWER_CHUNK = 0x23
FRAME_ASSISTANT_ERROR = 0x24
```

在 `VisionLink` 中添加：

```python
def send_ui_action(self, action):
    if action not in (1, 2, 3, 4):
        raise ValueError("unsupported UI action")
    self.write_frame(FRAME_UI_ACTION, bytes((action,)))

@staticmethod
def is_assistant_frame(frame):
    return frame[0] in (FRAME_ASSISTANT_STATE, FRAME_TRANSCRIPT,
                        FRAME_ANSWER_CHUNK, FRAME_ASSISTANT_ERROR)
```

- [ ] **Step 4: 运行协议与兼容测试确认 GREEN**

Run: `python -m unittest k230.test_assistant_protocol k230.test_vision_protocol_compat k230.test_micropython_compat -v`

Expected: PASS，且既有 JPEG 分块测试仍通过。

- [ ] **Step 5: Commit**

```bash
git add k230/vision_protocol.py k230/vision_link.py k230/test_assistant_protocol.py
git commit -m "feat: add K230 assistant UART frames"
```

## Task 5: LVGL 星云智脑 UI 控制器

**Files:**
- Create: `k230/assistant_ui.py`
- Modify: `k230/diagnose_lvgl_touch.py`

**Consumes:** `AssistantStore.snapshot()` and LVGL API evidence from Task 1.

**Produces:**

```python
class AssistantUi:
    def __init__(self, lv_module, width=800, height=480): ...
    def set_visible(self, visible): ...
    def refresh(self, snapshot, now_ms): ...
    def destroy(self): ...
```

- [ ] **Step 1: 写入 LVGL 对象创建验收脚本**

在 `diagnose_lvgl_touch.py` 中加入：

```python
from assistant_store import AssistantStore
from assistant_ui import AssistantUi

store = AssistantStore()
ui = AssistantUi(lv)
ui.set_visible(True)
ui.refresh(store.snapshot(), 0)
```

预期上板：显示深蓝半透明助手面板、左侧青蓝圆环、右侧三张圆角卡片；无异常或内存持续上涨。

- [ ] **Step 2: 实现一次性对象树**

`AssistantUi.__init__` 创建一次：根容器、HUD 标签、语音球容器、内外圆环、状态标签、转写标签、回答标签、视觉卡标签、返回提示。全部尺寸按 800×480 固定布局，根容器初始隐藏。

主题常量：

```python
COLOR_PANEL = 0x07152E
COLOR_CYAN = 0x39D9FF
COLOR_PURPLE = 0x9A63FF
COLOR_GREEN = 0x58FFC1
COLOR_RED = 0xFF5875
COLOR_TEXT = 0xD8EBFF
```

- [ ] **Step 3: 实现状态差量刷新和动效**

`refresh` 比较最新快照和 `self._last_snapshot`；仅在值变化时更新标签。每 120 ms 更新一次圆环半径/透明度：

```python
phase = (now_ms // 120) % 10
pulse = phase if phase <= 5 else 10 - phase
```

根据状态使用固定颜色；页面隐藏时不更新圆环。文本使用 `snapshot["transcript"]`、`snapshot["answer"]` 和错误说明；ESP32 链路离线时显示“ESP32 离线”。

- [ ] **Step 4: 上板验证静态页面与动画**

Run: `diagnose_lvgl_touch.py`

Expected: 语音球在状态切换时改色并以约 8 Hz 以内的频率呼吸；摄像头视频尚未接入时允许使用纯 LVGL 诊断背景。

- [ ] **Step 5: Commit**

```bash
git add k230/assistant_ui.py k230/diagnose_lvgl_touch.py
git commit -m "feat: add nebula LVGL assistant UI"
```

## Task 6: 接入视频主循环、触控和 UART 状态

**Files:**
- Modify: `k230/main.py`
- Modify: `k230/vision_app.py`
- Modify: `k230/README.md`
- Test: `k230/test_direct_camera_lcd.py`

**Consumes:** `AssistantStore`、`TouchService`、`AssistantUi`、`VisionLink.send_ui_action`。

**Produces:** K230 上可在实时视频中右滑打开助手页、按住语音球发送 UI 动作、接收 ESP32 状态/文本刷新页面的主程序。

- [ ] **Step 1: 为 VisionApp 写入会话帧投递失败测试**

在 `test_micropython_compat.py` 加入 fake store 和用 `encode_frame` 生成的 `FRAME_ASSISTANT_STATE`：

```python
def test_assistant_state_frame_updates_store_without_affecting_jpeg():
    store = FakeAssistantStore()
    link = MockPollVisionLink([(FRAME_ASSISTANT_STATE, 1, b"\x01\x34\x12\x00")])
    app = VisionApp(link, lambda: {}, assistant_store=store)
    app.run_once()
    assert store.states == [(1, 0x1234, 0)]
```

- [ ] **Step 2: 运行测试确认 RED**

Run: `python -m unittest k230.test_micropython_compat -v`

Expected: FAIL because `VisionApp` does not accept `assistant_store`.

- [ ] **Step 3: 最小改动 VisionApp**

为 `VisionApp.__init__` 添加可选 `assistant_store=None`。在遍历 `self.link.poll()` 时，优先把 `0x21`--`0x24` 转换为 store 调用：

```python
if frame[0] == FRAME_ASSISTANT_STATE and len(frame[2]) == 4:
    self.assistant_store.apply_state(frame[2][0], frame[2][1] | (frame[2][2] << 8), frame[2][3])
elif frame[0] == FRAME_TRANSCRIPT:
    self.assistant_store.set_transcript(frame[2].decode("utf-8", "replace"))
elif frame[0] == FRAME_ANSWER_CHUNK:
    self.assistant_store.append_answer(frame[2].decode("utf-8", "replace"))
elif frame[0] == FRAME_ASSISTANT_ERROR:
    self.assistant_store.set_error(frame[2][0] if frame[2] else 0,
                                   frame[2][1:].decode("utf-8", "replace"))
```

JPEG 分支保持不变。

- [ ] **Step 4: 在 `main.py` 接入组件**

创建 `AssistantStore`、`TouchService`、`AssistantUi`。现有 `run_camera_lcd` 每轮执行以下顺序：

```python
touch_point = read_confirmed_touch_point()
touch_event = touch_service.feed(touch_point.x, touch_point.y,
                                 touch_point.pressed, assistant_visible)
if touch_event["gesture"] == GESTURE_OPEN_ASSISTANT:
    assistant_visible = True
    assistant_ui.set_visible(True)
elif touch_event["gesture"] == GESTURE_CLOSE_ASSISTANT:
    assistant_visible = False
    assistant_ui.set_visible(False)
if touch_event["action"] != ACTION_NONE and store.is_link_online(now_ms):
    vision_app.link.send_ui_action(touch_event["action"])
vision_app.run_once()
assistant_ui.refresh(store.snapshot(), now_ms)
lv.timer_handler()
```

`read_confirmed_touch_point` 的构造与读点调用必须使用 Task 1 已记录的 v1.2.2 API；读取异常时返回“未按下”，不得退出视频循环。

- [ ] **Step 5: 运行 CPython 回归测试确认 GREEN**

Run: `python -m unittest discover -s k230 -p 'test_*.py' -v`

Expected: 全部通过，包括原有协议、JPEG、摄像头显示兼容测试。

- [ ] **Step 6: 双板上板验收**

1. 上传 `main.py`、`vision_app.py`、`vision_link.py`、`vision_protocol.py`、`assistant_store.py`、`touch_service.py`、`assistant_ui.py` 到 K230 `/sdcard/`。
2. K230 运行 `main.py`，确认实时视频持续。
3. 从右边缘向左滑，确认 220 ms 内打开星云助手页。
4. 按住/松开语音球，ESP32 串口分别出现 UI action `1`、`2`。
5. ESP32 发 `0x21` LISTENING、`0x21` THINKING、`0x21` SPEAKING，确认语音球颜色/文案切换。
6. ESP32 发 `0x22` 与 `0x23` UTF-8 文本，确认卡片更新、中文没有乱码。
7. 拔掉 K230/ESP32 UART，3 秒内出现“ESP32 离线”，摄像头预览持续；重新接回后恢复。

- [ ] **Step 7: 更新 README 和 Commit**

说明部署的新增文件、手势、语音球、UART 帧与诊断顺序。

```bash
git add k230/main.py k230/vision_app.py k230/README.md k230/test_micropython_compat.py
git commit -m "feat: integrate K230 assistant page"
```

## Plan Self-Review

- Spec coverage: Task 1 覆盖固件 API 证据；Tasks 2--3 覆盖有界状态和手势；Task 4 覆盖新协议；Task 5 覆盖星云 LVGL 风格和轻量动效；Task 6 覆盖视频共存、状态同步、容错、部署与验收。
- No placeholders: 计划不含 TBD/TODO；唯一运行时变量是触控构造 API，Task 1 强制先从 v1.2.2 实机日志确认，不能猜测。
- Type consistency: `AssistantStore` 方法、UI 动作常量、`VisionLink.send_ui_action` 及新帧常量均在前置任务中定义，Task 6 只消费这些接口。
