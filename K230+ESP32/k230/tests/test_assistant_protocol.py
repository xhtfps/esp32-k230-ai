import sys
import unittest

sys.path.insert(0, "k230")

from vision_link import VisionLink
from vision_protocol import FRAME_UI_ACTION, FrameDecoder


class MockUart:
    def __init__(self):
        self.writes = []

    def write(self, data):
        self.writes.append(bytes(data))
        return len(data)

    def read(self):
        return b""


class AssistantProtocolTest(unittest.TestCase):
    def test_ui_action_encodes_one_byte_payload(self):
        uart = MockUart()

        VisionLink(uart=uart).send_ui_action(1)

        frame = FrameDecoder().feed(uart.writes[0])[0]
        self.assertEqual(frame[0], FRAME_UI_ACTION)
        self.assertEqual(frame[2], b"\x01")

    def test_invalid_ui_action_is_rejected(self):
        uart = MockUart()

        with self.assertRaises(ValueError):
            VisionLink(uart=uart).send_ui_action(99)


if __name__ == "__main__":
    unittest.main()
