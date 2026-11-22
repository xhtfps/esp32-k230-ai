import sys
import unittest

sys.path.insert(0, "k230")

import vision_link
from vision_app import VisionApp
from vision_link import VisionLink
from vision_protocol import (
    FRAME_ASSISTANT_STATE,
    FRAME_ERROR,
    FRAME_REQUEST_JPEG,
    FrameDecoder,
)


class MockUart:
    def __init__(self):
        self.writes = []

    def write(self, data):
        self.writes.append(bytes(data))
        return len(data)

    def read(self):
        return b""


class MicroPythonJson:
    def dumps(self, value):
        return '{"task":"face","count":0,"items":[]}'


class MockVisionLink:
    def __init__(self):
        self.writes = []

    def request_id(self, frame):
        return frame[2][0] | (frame[2][1] << 8)

    def write_frame(self, frame_type, payload):
        self.writes.append((frame_type, payload))


class MockPollVisionLink(MockVisionLink):
    def __init__(self, frames):
        super().__init__()
        self.frames = frames

    def poll(self):
        frames = self.frames
        self.frames = []
        return frames

    def send_heartbeat(self):
        pass

    def send_result(self, result):
        pass


class FakeAssistantStore:
    def __init__(self):
        self.states = []

    def apply_state(self, state, session_id, error_code):
        self.states.append((state, session_id, error_code))


class MicroPythonCompatibilityTest(unittest.TestCase):
    def test_result_serialization_works_without_separators_argument(self):
        original_json = vision_link.json
        vision_link.json = MicroPythonJson()
        try:
            uart = MockUart()
            VisionLink(uart=uart).send_result(
                {"task": "face", "count": 0, "items": []}
            )
        finally:
            vision_link.json = original_json

        frames = FrameDecoder().feed(uart.writes[0])
        self.assertEqual(frames[0][2], b'{"task":"face","count":0,"items":[]}')

    def test_jpeg_callback_is_split_into_protocol_frames(self):
        link = MockVisionLink()
        jpeg = b"jpeg" * 600
        app = VisionApp(link, lambda: {}, jpeg_fn=lambda: jpeg)
        request = (FRAME_REQUEST_JPEG, 3, (9).to_bytes(2, "little"))

        app._send_requested_jpeg(request)

        self.assertEqual([frame[0] for frame in link.writes], [0x11, 0x11, 0x12])
        self.assertEqual(link.writes[0][1][:6], b"\x09\x00\x00\x00\x00\x00")
        self.assertEqual(link.writes[1][1][:6], b"\x09\x00\x00\x08\x00\x00")
        self.assertEqual(int.from_bytes(link.writes[-1][1][2:6], "little"), len(jpeg))

    def test_jpeg_capture_failure_is_reported_without_escaping(self):
        link = MockVisionLink()

        def failing_capture():
            raise RuntimeError("jpeg compression failed")

        app = VisionApp(link, lambda: {}, jpeg_fn=failing_capture)
        request = (FRAME_REQUEST_JPEG, 3, (9).to_bytes(2, "little"))

        app._send_requested_jpeg(request)

        self.assertEqual(link.writes[0][0], FRAME_ERROR)
        self.assertEqual(link.writes[0][1][:2], b"\x09\x00")

    def test_assistant_state_frame_updates_store_without_affecting_jpeg(self):
        store = FakeAssistantStore()
        link = MockPollVisionLink([
            (FRAME_ASSISTANT_STATE, 1, b"\x01\x34\x12\x00"),
        ])
        app = VisionApp(link, lambda: {}, assistant_store=store)

        app.run_once()

        self.assertEqual(store.states, [(1, 0x1234, 0)])


if __name__ == "__main__":
    unittest.main()
