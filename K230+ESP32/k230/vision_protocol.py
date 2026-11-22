"""
K230 端与 ESP32 视觉网关之间的帧封装与解析模块。

该模块定义了 UART 通信链路上使用的自定义帧格式，
提供帧的编码（打包）与解码（解包）功能，并包含 CRC16
校验以保证数据的完整性。
"""

import struct

# ---------- 帧格式常量 ----------
MAGIC = b"\xA5\x5A"         # 帧同步字 (magic number)，用于帧头定位
VERSION = 0x01              # 协议版本号，用于未来兼容性
MAX_PAYLOAD = 0xFFFF        # 最大负载长度 (65535 字节)

# 帧类型定义
FRAME_RESULT = 0x01         # 推理结果帧
FRAME_HEARTBEAT = 0x02      # 心跳帧
FRAME_REQUEST_JPEG = 0x10   # JPEG 图像请求帧
FRAME_JPEG_CHUNK = 0x11     # JPEG 数据块帧
FRAME_JPEG_COMPLETE = 0x12  # JPEG 传输完成帧（含校验信息）
FRAME_JPEG_ACK = 0x13       # JPEG 请求应答帧
FRAME_UI_ACTION = 0x20      # K230 UI 动作帧
FRAME_ASSISTANT_STATE = 0x21  # ESP32 会话状态帧
FRAME_TRANSCRIPT = 0x22     # ESP32 最终转写帧
FRAME_ANSWER_CHUNK = 0x23   # ESP32 回答文本帧
FRAME_ASSISTANT_ERROR = 0x24  # ESP32 助手错误帧
FRAME_ERROR = 0x7F          # 错误帧


def crc16(data):
    """
    计算 CCITT CRC-16 (多项式 0x1021) 校验值。

    适用于 16 位嵌入式通信，与常见硬件 CRC 模块兼容。
    输入 data 为字节串，返回 16 位无符号整数。
    """
    crc = 0xFFFF
    for value in data:
        crc ^= (value << 8)         # 将新字节移入高字节
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def encode_frame(frame_type, sequence, payload=b""):
    """
    按协议格式编码一个完整帧。

    帧结构（共 8 + N + 2 字节）：
      - MAGIC   : 2 字节
      - VERSION : 1 字节
      - 帧类型   : 1 字节
      - 负载长度 : 2 字节 (小端序)
      - 序列号   : 2 字节 (小端序)
      - 负载数据 : N 字节
      - CRC16    : 2 字节 (小端序，覆盖从 VERSION 到负载结束的全部数据)

    参数:
        frame_type: 1 字节帧类型标识
        sequence: 16 位帧序号
        payload: 负载字节串

    返回:
        编码后的字节串帧

    抛出:
        ValueError: 负载长度超过 MAX_PAYLOAD
    """
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload exceeds 65535 bytes")

    # 构建帧头：魔法字、版本、类型、长度、序号（均为小端序）
    header = struct.pack("<2sBBHH", MAGIC, VERSION, frame_type, len(payload), sequence)
    # 计算校验和，范围从版本字段开始到负载结束
    checksum = crc16(header[2:] + payload)
    return header + payload + struct.pack("<H", checksum)


class FrameDecoder:
    """
    帧解码器，用于从 UART 字节流中提取并校验完整帧。

    内部维护一个缓冲区，支持分次喂入数据（feed），
    自动处理粘包、半包、错误帧丢弃，并返回通过校验的帧列表。
    """

    def __init__(self, max_payload=MAX_PAYLOAD):
        """
        初始化解码器。

        参数:
            max_payload: 允许的最大负载长度，用于丢弃异常帧。
        """
        self.max_payload = max_payload
        self.buffer = bytearray()   # 接收缓冲区

    def feed(self, data):
        """
        将新到达的字节数据送入解码器，返回本次成功解析的帧列表。

        参数:
            data: 新接收的字节串

        返回:
            帧列表，每个元素为 (帧类型, 序列号, 负载) 元组。
            负载为原始字节串（不包含帧头/帧尾校验）。
        """
        self.buffer.extend(data)
        frames = []

        # 循环解析，直到缓冲区无法再构成完整帧
        while True:
            # 查找下一个同步字的位置
            start = self.buffer.find(MAGIC)
            if start < 0:
                # 未找到同步字，仅保留最后一个字节以防同步字跨缓冲区边界
                self.buffer = self.buffer[-1:]
                break

            # 丢弃同步字之前的无效数据
            if start > 0:
                # K230 MicroPython 不支持 bytearray 的切片删除。
                self.buffer = self.buffer[start:]

            # 最小帧长度为 8 字节（帧头）
            if len(self.buffer) < 8:
                break

            # 解析帧头字段（跳过 2 字节 MAGIC）
            version, frame_type, length, sequence = struct.unpack_from("<BBHH", self.buffer, 2)

            # 版本不匹配或负载长度超限，丢弃当前第一个字节并继续尝试
            if version != VERSION or length > self.max_payload:
                self.buffer = self.buffer[1:]
                continue

            # 完整帧长度 = 帧头(8) + 负载(length) + CRC(2)
            total = 8 + length + 2
            if len(self.buffer) < total:
                break  # 数据不足，等待更多字节

            # 提取负载字节
            payload = bytes(self.buffer[8:8 + length])
            # 提取接收到的 CRC 值
            received_crc = struct.unpack_from("<H", self.buffer, 8 + length)[0]
            # 计算期望的 CRC（校验范围从 VERSION 到负载结束）
            expected_crc = crc16(bytes(self.buffer[2:8 + length]))

            # 无论校验是否通过，都将已处理的字节移出缓冲区
            # 使用切片重建缓冲区，兼容 K230 MicroPython。
            self.buffer = self.buffer[total:]

            # 校验通过，加入结果列表
            if received_crc == expected_crc:
                frames.append((frame_type, sequence, payload))

        return frames
