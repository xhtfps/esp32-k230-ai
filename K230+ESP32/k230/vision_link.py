"""
K230 CanMV 平台 UART2 通信链接模块。

用于与 ESP32 视觉网关通过自定义协议进行双向通信，
负责帧的编码、解码、发送与接收。
"""

import json
import time

from vision_protocol import (
    FRAME_ANSWER_CHUNK,
    FRAME_ASSISTANT_ERROR,
    FRAME_ASSISTANT_STATE,
    FRAME_HEARTBEAT,       # 心跳帧类型标识
    FRAME_JPEG_ACK,        # JPEG 应答帧类型标识
    FRAME_REQUEST_JPEG,    # JPEG 请求帧类型标识
    FRAME_TRANSCRIPT,
    FRAME_UI_ACTION,
    FrameDecoder,          # 帧解码器，用于从字节流中提取完整帧
    encode_frame,          # 帧编码函数，将类型、序号、负载打包为传输帧
)


class VisionLink:
    """UART 通信链路抽象，封装底层 UART 操作并提供协议级接口。"""

    def __init__(self, uart=None, baudrate=921600, tx_pin=5, rx_pin=6):
        """
        初始化链路。

        参数:
            uart: 外部传入的 UART 对象，若为 None 则自动创建 UART2。
            baudrate: UART 波特率，默认 921600。
            tx_pin: 发送引脚编号，默认 5。
            rx_pin: 接收引脚编号，默认 6。
        """
        self.sequence = 0                    # 帧序号，每次发送递增，用于对端去重或排序
        self.decoder = FrameDecoder()        # 帧解码器实例，处理接收到的字节流
        self.uart = uart if uart is not None else self._create_uart(baudrate, tx_pin, rx_pin)

    @staticmethod
    def _create_uart(baudrate, tx_pin, rx_pin):
        """在 K230 CanMV 平台上创建并配置 UART2 外设。

        返回:
            配置好的 UART 对象。
        """
        from machine import FPIOA, UART

        fpioa = FPIOA()
        # 将物理引脚映射到 UART2 的 TX 和 RX 功能
        fpioa.set_function(tx_pin, FPIOA.UART2_TXD)
        fpioa.set_function(rx_pin, FPIOA.UART2_RXD)
        return UART(UART.UART2, baudrate)

    def write_frame(self, frame_type, payload=b""):
        """编码并发送一个完整帧。

        参数:
            frame_type: 帧类型标识（1 字节）。
            payload: 负载数据（字节串）。

        异常:
            OSError: 当 UART 写入字节数与预期不符时抛出。
        """
        # 使用当前序号编码帧
        frame = encode_frame(frame_type, self.sequence, payload)
        # 发送后序号递增（16 位循环）
        self.sequence = (self.sequence + 1) & 0xFFFF
        written = self.uart.write(frame)
        # 某些 UART 实现可能返回 None，仅在返回值有效且不等于预期长度时报错
        if written is not None and written != len(frame):
            raise OSError("short UART write")

    def send_heartbeat(self):
        """发送心跳帧，通知对端当前链路存活。"""
        self.write_frame(FRAME_HEARTBEAT, b'{"state":"alive"}')

    def send_ui_action(self, action):
        """Send one K230 touch action to the ESP32 assistant coordinator."""
        if action not in (1, 2, 3, 4):
            raise ValueError("unsupported UI action")
        self.write_frame(FRAME_UI_ACTION, bytes((action,)))

    def send_result(self, result):
        """发送推理结果帧。

        参数:
            result: 可 JSON 序列化的结果对象（通常为字典）。
        """
        # 使用紧凑 JSON 格式（无空格）以节省带宽
        try:
            encoded = json.dumps(result, separators=(",", ":"))
        except TypeError:
            # CanMV 的 MicroPython/ujson 版本可能不支持 separators 参数。
            encoded = json.dumps(result)
        payload = encoded.encode("utf-8") if isinstance(encoded, str) else bytes(encoded)
        self.write_frame(0x01, payload)   # 0x01 表示结果帧

    def poll(self):
        """轮询接收缓冲区，返回解码后的完整帧列表。

        返回:
            帧列表，每个元素为 (帧类型, 序号, 负载) 元组。
            若无新数据则返回空列表。
        """
        data = self.uart.read()
        if not data:
            return []
        print("UART RX bytes=%d" % len(data))
        # 将新读取的字节送入解码器，解码器内部维护缓冲区并提取完整帧
        frames = self.decoder.feed(data)
        for frame in frames:
            print("UART RX frame type=0x%02x len=%d" % (frame[0], len(frame[2])))
        return frames

    def send_jpeg_ack(self, request_id):
        """发送 JPEG 数据请求的应答帧（确认或拒绝）。

        参数:
            request_id: 对应 JPEG 请求的标识 ID。
        """
        # 将 request_id 拆分为 2 字节小端序
        self.write_frame(FRAME_JPEG_ACK, bytes((request_id & 0xFF, request_id >> 8)))

    @staticmethod
    def is_assistant_frame(frame):
        """Return True for a decoded ESP32-to-K230 assistant frame."""
        return frame[0] in (FRAME_ASSISTANT_STATE, FRAME_TRANSCRIPT,
                            FRAME_ANSWER_CHUNK, FRAME_ASSISTANT_ERROR)

    @staticmethod
    def requested_jpeg(frame):
        """判断一个帧是否为有效的 JPEG 请求帧。

        参数:
            frame: 解码后的帧元组 (帧类型, 序号, 负载)。

        返回:
            True 当帧类型为 JPEG 请求且负载长度恰好为 2 字节（request_id）。
        """
        return frame[0] == FRAME_REQUEST_JPEG and len(frame[2]) == 2

    @staticmethod
    def request_id(frame):
        """从 JPEG 请求帧中提取 request_id。

        参数:
            frame: 解码后的帧元组 (帧类型, 序号, 负载)。

        返回:
            16 位无符号整数 request_id。
        """
        # 负载为 2 字节小端序，直接拼接为整数
        return frame[2][0] | (frame[2][1] << 8)


def monotonic_ms():
    """获取单调递增的毫秒级时间戳（基于 CanMV 的 ticks_ms，可处理溢出）。"""
    ticks_ms = getattr(time, "ticks_ms", None)
    if ticks_ms is not None:
        return ticks_ms()
    return int(time.monotonic() * 1000)


def ticks_diff(new_ticks, old_ticks):
    """Use the MicroPython wrap-safe primitive with a CPython test fallback."""
    difference = getattr(time, "ticks_diff", None)
    if difference is not None:
        return difference(new_ticks, old_ticks)
    return new_ticks - old_ticks
