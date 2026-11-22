"""K230 星云智脑助手页。

业务状态、触控手势和 LVGL 界面均在此文件；测试代码位于 ``k230/tests``。
"""

try:
    import lvgl as lv
    from machine import TOUCH
    import image
    import uctypes
except (ImportError, OSError):
    lv = None
    TOUCH = None
    image = None
    uctypes = None


STATE_IDLE = 0
STATE_LISTENING = 1
STATE_TRANSCRIBING = 2
STATE_THINKING = 3
STATE_SPEAKING = 4
STATE_ERROR = 5

ACTION_NONE = 0
ACTION_START_LISTENING = 1
ACTION_STOP_LISTENING = 2
ACTION_STOP_PLAYBACK = 3
ACTION_ANALYZE_VISION = 4

DISPLAY_WIDTH = 800
DISPLAY_HEIGHT = 480
EDGE_WIDTH = 46
SWIPE_DISTANCE = 115
VOICE_X = 72
VOICE_Y = 140
VOICE_W = 244
VOICE_H = 244

TRANSCRIPT_LIMIT = 96
ANSWER_LIMIT = 384
ERROR_MESSAGE_LIMIT = 96


def _bounded_utf8(text, limit):
    raw = str(text).encode("utf-8")
    if len(raw) <= limit:
        return str(text)
    return raw[:limit - 3].decode("utf-8", "ignore") + "..."


class AssistantStore:
    """只保存小而有界的 UI 状态，适合 MicroPython 长时间运行。"""

    def __init__(self):
        self._state = STATE_IDLE
        self._session_id = 0
        self._error_code = 0
        self._transcript = ""
        self._answer = ""
        self._error_message = ""
        self._link_online = False
        self._last_link_ms = 0
        self._vision_summary = ""

    def apply_state(self, state, session_id, error_code=0):
        if state not in (STATE_IDLE, STATE_LISTENING, STATE_TRANSCRIBING,
                         STATE_THINKING, STATE_SPEAKING, STATE_ERROR):
            return False
        self._state = state
        self._session_id = int(session_id) & 0xFFFF
        self._error_code = int(error_code) & 0xFF
        if state != STATE_ERROR:
            self._error_message = ""
        return True

    def set_transcript(self, text):
        self._transcript = _bounded_utf8(text, TRANSCRIPT_LIMIT)

    def append_answer(self, text):
        self._answer = _bounded_utf8(self._answer + str(text), ANSWER_LIMIT)

    def clear_answer(self):
        self._answer = ""

    def set_error(self, error_code, message):
        self._state = STATE_ERROR
        self._error_code = int(error_code) & 0xFF
        self._error_message = _bounded_utf8(message, ERROR_MESSAGE_LIMIT)

    def set_link_online(self, online, now_ms):
        self._link_online = bool(online)
        if self._link_online:
            self._last_link_ms = int(now_ms)

    def is_link_online(self, now_ms, timeout_ms=3000):
        return self._link_online and int(now_ms) - self._last_link_ms < timeout_ms

    def set_vision_summary(self, text):
        self._vision_summary = _bounded_utf8(text, ANSWER_LIMIT)

    def snapshot(self):
        return {
            "state": self._state, "session_id": self._session_id,
            "error_code": self._error_code, "transcript": self._transcript,
            "answer": self._answer, "error_message": self._error_message,
            "link_online": self._link_online,
            "last_link_ms": self._last_link_ms,
            "vision_summary": self._vision_summary,
        }


class AssistantPage:
    """星云智脑二级页；预览页右滑打开，助手页左滑返回。"""

    def __init__(self, on_action=None):
        self.store = AssistantStore()
        self.on_action = on_action
        self.visible = False
        self._touch = None
        self._touch_start = None
        self._last_touch_point = (0, 0)
        self._touch_reported = False
        self._disp = None
        self._buf_a = None
        self._buf_b = None
        self._ui_ready = False
        self._last_text = None
        self._return_requested = False
        self._widgets = {}

    @staticmethod
    def _screen_point(point):
        """转换触控坐标为当前 800x480 ST7701 屏幕坐标。

        V3.0 资料的 LVGL 单独示例使用了 90° 旋转坐标变换，是因为该
        示例的显示朝向与当前摄像头程序不同。当前摄像头预览为横屏，实测
        点击右下角返回的原始坐标已是约 (720, 430)；继续旋转会成为负 y。
        """
        x = max(0, min(DISPLAY_WIDTH - 1, point.x))
        y = max(0, min(DISPLAY_HEIGHT - 1, point.y))
        return x, y

    def poll_preview_gesture(self):
        """在摄像头页调用；检测右边缘向左滑，返回 True 时应打开助手页。

        V3.0 官方例程表明 ``read(1)`` 的事件 2/3 都表示按住/移动，
        松开后可能直接返回空列表而不是 ``EVENT_UP``。因此手势必须在
        仍按住且已经移动到足够距离时立即判定，不能等松开事件。
        """
        if TOUCH is None:
            return False
        if self._touch is None:
            self._touch = TOUCH(0)
            print("K230 touch ready")
        points = self._touch.read(1)
        if not points:
            self._touch_start = None
            return False
        x, y = self._screen_point(points[0])
        self._last_touch_point = (x, y)
        event = points[0].event
        if not self._touch_reported:
            print("K230 touch point: x=%d y=%d event=%d" % (x, y, event))
            self._touch_reported = True

        # 严格沿用资料 lvgl_touch_demo.py：event 2 / 3 为 PRESSED。
        if event in (2, 3):
            if self._touch_start is None:
                self._touch_start = (x, y)
                return False
            start_x, start_y = self._touch_start
            if (start_x >= DISPLAY_WIDTH - EDGE_WIDTH and
                    start_x - x >= SWIPE_DISTANCE and
                    abs(start_y - y) <= 150):
                self._touch_start = None
                print("Assistant page gesture detected")
                return True
        else:
            self._touch_start = (x, y)
        return False

    def open(self):
        if self.visible:
            return True
        if lv is None or image is None:
            print("Assistant page unavailable: lvgl/image module missing")
            return False
        try:
            self._init_lvgl()
            self.visible = True
            self._refresh(True)
            return True
        except Exception as error:
            print("Assistant page open failed: %s" % error)
            self.close()
            return False

    def close(self):
        self.visible = False
        self._touch_start = None
        self._return_requested = False
        if self._ui_ready and lv is not None:
            try:
                lv.deinit()
            except Exception:
                pass
        self._ui_ready = False
        self._disp = None
        self._buf_a = None
        self._buf_b = None
        self._widgets = {}

    def pump(self):
        """在助手页的每次主循环调用；返回 True 表示请求回摄像头页。"""
        if not self.visible or lv is None:
            return False
        self._refresh(False)
        try:
            # 当前 V3.0 官方例程使用 task_handler()；优先同一 API。
            lv.task_handler()
        except AttributeError:
            lv.timer_handler()
        return self._return_requested

    def _init_lvgl(self):
        lv.init()
        self._disp = lv.disp_create(DISPLAY_WIDTH, DISPLAY_HEIGHT)
        self._disp.set_flush_cb(self._flush)
        self._buf_a = image.Image(DISPLAY_WIDTH, DISPLAY_HEIGHT, image.ARGB8888)
        self._buf_b = image.Image(DISPLAY_WIDTH, DISPLAY_HEIGHT, image.ARGB8888)
        self._disp.set_draw_buffers(self._buf_a.bytearray(), self._buf_b.bytearray(),
                                   self._buf_a.size(), lv.DISP_RENDER_MODE.DIRECT)
        self._install_touch_input()
        self._build_ui()
        self._ui_ready = True

    def _flush(self, disp, area, color):
        if disp.flush_is_last():
            try:
                from media.display import Display
                target = self._buf_a
                if target.virtaddr() != uctypes.addressof(color.__dereference__()):
                    target = self._buf_b
                Display.show_image(target, x=0, y=0, layer=Display.LAYER_OSD0)
            except Exception as error:
                print("Assistant page flush failed: %s" % error)
        disp.flush_ready()

    def _install_touch_input(self):
        if TOUCH is None:
            return
        if self._touch is None:
            self._touch = TOUCH(0)
        indev = lv.indev_create()
        indev.set_type(lv.INDEV_TYPE.POINTER)
        indev.set_read_cb(self._read_touch)
        self._widgets["indev"] = indev

    def _read_touch(self, driver, data):
        x, y = self._last_touch_point
        state = lv.INDEV_STATE.RELEASED
        points = self._touch.read(1)
        if points:
            x, y = self._screen_point(points[0])
            self._last_touch_point = (x, y)
            event = points[0].event
            # 与资料例程一致：2 / 3 表示 pressed。
            if event in (2, 3):
                state = lv.INDEV_STATE.PRESSED
            if event in (2, 3) and self._touch_start is None:
                self._touch_start = (x, y)
            elif event in (2, 3) and self._touch_start is not None:
                start_x, start_y = self._touch_start
                if (start_x <= EDGE_WIDTH and x - start_x >= SWIPE_DISTANCE and
                        abs(start_y - y) <= 150):
                    self._return_requested = True
                    self._touch_start = None
        else:
            self._touch_start = None
        data.point = lv.point_t({'x': x, 'y': y})
        data.state = state

    @staticmethod
    def _color(value):
        return lv.color_hex(value)

    def _panel(self, parent, x, y, width, height, color, radius=18):
        obj = lv.obj(parent)
        obj.set_pos(x, y)
        obj.set_size(width, height)
        obj.set_style_bg_color(self._color(color), 0)
        obj.set_style_bg_opa(lv.OPA.COVER, 0)
        obj.set_style_border_width(1, 0)
        obj.set_style_border_color(self._color(0x25436A), 0)
        obj.set_style_radius(radius, 0)
        obj.set_style_pad_all(12, 0)
        obj.clear_flag(lv.obj.FLAG.SCROLLABLE)
        return obj

    def _label(self, parent, text, x, y, width, color=0xD8E9FF):
        label = lv.label(parent)
        label.set_text(text)
        label.set_pos(x, y)
        label.set_width(width)
        label.set_style_text_color(self._color(color), 0)
        label.set_style_text_align(lv.TEXT_ALIGN.LEFT, 0)
        return label

    def _build_ui(self):
        screen = lv.scr_act()
        screen.set_style_bg_color(self._color(0x07111F), 0)
        screen.set_style_bg_opa(lv.OPA.COVER, 0)
        screen.clear_flag(lv.obj.FLAG.SCROLLABLE)

        self._panel(screen, 0, 0, 800, 8, 0x00C8FF, 0)
        self._label(screen, "NEBULA", 34, 28, 210, 0x63E6FF)
        self._label(screen, "AI ASSISTANT", 34, 55, 240, 0xFFFFFF)
        self._label(screen, "K230  |  EDGE SWIPE TO RETURN", 34, 86, 330, 0x6C91BC)
        self._widgets["status"] = self._label(screen, "LINK  CHECKING", 610, 38, 160, 0xFFCA6B)

        orb = self._panel(screen, VOICE_X, VOICE_Y, VOICE_W, VOICE_H, 0x132A46, 122)
        orb.set_style_border_width(3, 0)
        orb.set_style_border_color(self._color(0x3EECFF), 0)
        orb.add_event(self._voice_clicked, lv.EVENT.CLICKED, None)
        self._widgets["orb"] = orb
        self._widgets["orb_title"] = self._label(orb, "READY", 67, 82, 120, 0xFFFFFF)
        self._widgets["orb_subtitle"] = self._label(orb, "TAP TO TALK", 48, 119, 150, 0x80B6E9)

        transcript = self._panel(screen, 350, 135, 410, 112, 0x0C1B31)
        self._label(transcript, "YOU SAID", 0, 0, 200, 0x4BD7FF)
        self._widgets["transcript"] = self._label(transcript, "Tap the voice core to begin.", 0, 32, 380)

        answer = self._panel(screen, 350, 264, 410, 174, 0x101A37)
        self._label(answer, "AI RESPONSE", 0, 0, 220, 0xBEA4FF)
        self._widgets["answer"] = self._label(answer, "Cloud AI is standing by.", 0, 32, 380)

        self._label(screen, "VISION LINK / ONLINE", 48, 424, 250, 0x6C91BC)
        self._label(screen, "< SWIPE FROM LEFT EDGE", 530, 424, 230, 0x6C91BC)

    def _voice_clicked(self, event):
        state = self.store.snapshot()["state"]
        if state in (STATE_LISTENING, STATE_TRANSCRIBING, STATE_THINKING):
            self._send_action(ACTION_STOP_LISTENING)
        elif state == STATE_SPEAKING:
            self._send_action(ACTION_STOP_PLAYBACK)
        else:
            self.store.clear_answer()
            self._send_action(ACTION_START_LISTENING)

    def _send_action(self, action):
        if self.on_action is not None:
            try:
                self.on_action(action)
            except Exception as error:
                self.store.set_error(1, "UART action failed: %s" % error)

    def _refresh(self, force):
        if not self._ui_ready:
            return
        view = self.store.snapshot()
        signature = (view["state"], view["transcript"], view["answer"],
                     view["error_message"], view["link_online"])
        if not force and signature == self._last_text:
            return
        self._last_text = signature
        state_text = {
            STATE_IDLE: ("READY", "TAP TO TALK", 0x3EECFF),
            STATE_LISTENING: ("LISTENING", "TAP TO STOP", 0x57FFB4),
            STATE_TRANSCRIBING: ("HEARING", "TRANSCRIBING", 0xFFCA6B),
            STATE_THINKING: ("THINKING", "CLOUD REASONING", 0xBEA4FF),
            STATE_SPEAKING: ("SPEAKING", "TAP TO MUTE", 0xFF8BC8),
            STATE_ERROR: ("RETRY", "TAP TO RECONNECT", 0xFF6B81),
        }[view["state"]]
        self._widgets["orb_title"].set_text(state_text[0])
        self._widgets["orb_subtitle"].set_text(state_text[1])
        self._widgets["orb"].set_style_border_color(self._color(state_text[2]), 0)
        self._widgets["status"].set_text("LINK  ONLINE" if view["link_online"] else "LINK  WAITING")
        transcript = view["transcript"] or "Tap the voice core to begin."
        answer = view["error_message"] if view["state"] == STATE_ERROR else view["answer"]
        self._widgets["transcript"].set_text(transcript)
        self._widgets["answer"].set_text(answer or "Cloud AI is standing by.")
