import sys
import unittest

sys.path.insert(0, "k230")

from vision_link import VisionLink
from vision_protocol import FRAME_REQUEST_JPEG, FrameDecoder
from vision_app import crc32


class MockUart:
    def __init__(self):
        self.writes = []
        self.read_buffer = b""

    def write(self, data):
        self.writes.append(bytes(data))
        return len(data)

    def read(self):
        data = self.read_buffer
        self.read_buffer = b""
        return data


class VisionLinkTest(unittest.TestCase):
    def test_result_and_heartbeat_frames(self):
        uart = MockUart()
        link = VisionLink(uart=uart)
        link.send_heartbeat()
        link.send_result({"task": "face", "count": 0, "items": []})
        decoder = FrameDecoder()
        frames = []
        for packet in uart.writes:
            frames.extend(decoder.feed(packet))
        self.assertEqual([frame[0] for frame in frames], [2, 1])

    def test_jpeg_request_helpers(self):
        uart = MockUart()
        link = VisionLink(uart=uart)
        request = (FRAME_REQUEST_JPEG, 4, (123).to_bytes(2, "little"))
        self.assertTrue(link.requested_jpeg(request))
        self.assertEqual(link.request_id(request), 123)
        self.assertEqual(crc32(b"abc"), 0x352441C2)


if __name__ == "__main__":
    unittest.main()
