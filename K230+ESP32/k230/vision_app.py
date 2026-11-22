"""
模型无关的 K230 视觉主循环。

提供的 face_detection.py 流水线可提供 `infer_fn` 函数；本模块
负责将 UART JSON 和 JPEG 请求处理与模型内部实现解耦。
"""

import time

try:
    from assistant_page import (
        STATE_ERROR,
        STATE_IDLE,
        STATE_LISTENING,
        STATE_SPEAKING,
        AssistantStore,
    )
except ImportError:
    AssistantStore = None

from vision_link import VisionLink, monotonic_ms, ticks_diff
from vision_protocol import (
    FRAME_ANSWER_CHUNK,
    FRAME_ASSISTANT_ERROR,
    FRAME_ASSISTANT_STATE,
    FRAME_ERROR,
    FRAME_REQUEST_JPEG,
    FRAME_TRANSCRIPT,
)


class VisionApp:
    """视觉应用主类，负责定时推理、结果上报、心跳维护与 JPEG 传输。"""

    def __init__(self, link, infer_fn, jpeg_fn=None, result_interval_ms=200,
                         assistant_store=None):
        """
        参数:
            link: VisionLink 实例，用于底层 UART 通信。
            infer_fn: 无参数推理函数，返回结果字典或可比较对象。
            jpeg_fn: 无参数函数，返回当前 JPEG 图像字节串；若为 None 则不支持拍照。
            result_interval_ms: 推理结果最小上报间隔（毫秒），避免频繁发送相同结果。
        """
        self.link = link
        self.infer_fn = infer_fn
        self.jpeg_fn = jpeg_fn
        self.result_interval_ms = result_interval_ms
        self.assistant_store = assistant_store

        self.last_result = None           # 上次推理结果
        self.last_result_ms = 0           # 上次发送结果的时间戳(ms)
        self.last_heartbeat_ms = 0        # 上次发送心跳的时间戳(ms)

    def run_once(self):
        """单次主循环：发送心跳（如果需要），处理 UART 请求，执行推理并上报结果。"""
        now = monotonic_ms()

        # 每 2 秒发送一次心跳，维持连接
        if ticks_diff(now, self.last_heartbeat_ms) >= 2000:
            self.link.send_heartbeat()
            self.last_heartbeat_ms = now

        # Process ESP32 assistant state first, then retain JPEG behaviour.
        for frame in self.link.poll():
            if self._apply_assistant_frame(frame):
                continue
            if frame[0] == FRAME_REQUEST_JPEG:
                self._send_requested_jpeg(frame)

        # 执行一次推理
        result = self.infer_fn()

        # 判定是否需要上报：结果变化 或 距上次上报超过间隔
        changed = result != self.last_result
        due = ticks_diff(now, self.last_result_ms) >= self.result_interval_ms
        if changed or due:
            self.link.send_result(result)
            self.last_result = result
            self.last_result_ms = now

        return result

    def _apply_assistant_frame(self, frame):
        """Apply one ESP32 assistant frame without touching JPEG semantics."""
        if self.assistant_store is None:
            return False

        frame_type = frame[0]
        payload = frame[2]
        if frame_type == FRAME_ASSISTANT_STATE:
            if len(payload) == 4:
                session_id = payload[1] | (payload[2] << 8)
                self.assistant_store.apply_state(payload[0], session_id, payload[3])
            return True
        if frame_type == FRAME_TRANSCRIPT:
            self.assistant_store.set_transcript(payload.decode("utf-8", "replace"))
            return True
        if frame_type == FRAME_ANSWER_CHUNK:
            self.assistant_store.append_answer(payload.decode("utf-8", "replace"))
            return True
        if frame_type == FRAME_ASSISTANT_ERROR:
            error_code = payload[0] if payload else 0
            message = payload[1:].decode("utf-8", "replace") if payload else ""
            self.assistant_store.set_error(error_code, message)
            return True
        return False

    def _send_requested_jpeg(self, frame):
        """响应 JPEG 请求：分块发送图像数据及结束校验帧。

        JPEG 数据被分割为 2048 字节的块，通过 0x11 帧发送；
        结束后通过 0x12 帧发送总长度及 CRC32 校验值。
        """
        request_id = self.link.request_id(frame)
        try:
            if self.jpeg_fn is None:
                raise RuntimeError("JPEG capture is not configured")

            jpeg = self.jpeg_fn()
            if jpeg is None:
                raise RuntimeError("JPEG capture returned None")
            jpeg = bytes(jpeg)
            if len(jpeg) == 0:
                raise RuntimeError("JPEG capture returned empty data")

            print("JPEG request %d captured %d bytes" % (request_id, len(jpeg)))
            for offset in range(0, len(jpeg), 2048):
                chunk = jpeg[offset:offset + 2048]
                self.link.write_frame(0x11, request_id.to_bytes(2, "little") +
                                      offset.to_bytes(4, "little") + chunk)

            crc = crc32(jpeg)
            self.link.write_frame(0x12, request_id.to_bytes(2, "little") +
                                  len(jpeg).to_bytes(4, "little") +
                                  crc.to_bytes(4, "little"))
            print("JPEG request %d sent" % request_id)
        except Exception as error:
            print("JPEG request %d failed: %s" % (request_id, error))
            message = str(error).encode("utf-8")[:120]
            try:
                self.link.write_frame(
                    FRAME_ERROR,
                    request_id.to_bytes(2, "little") + message,
                )
            except Exception as send_error:
                print("JPEG error report failed: %s" % send_error)


def crc32(data):
    """计算字节数据的 CRC32 校验值（与 zlib 兼容）。"""
    crc = 0xFFFFFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xEDB88320
            else:
                crc >>= 1
    return crc ^ 0xFFFFFFFF


def empty_face_result():
    """返回一个空的人脸检测结果字典，用于初始状态或无人脸场景。"""
    return {"task": "face", "count": 0, "items": []}


def main():
    """Run the camera/LCD entry point when this file is launched directly."""
    from main import run

    run()


if __name__ == "__main__":
    main()
