import sys
import unittest

sys.path.insert(0, "k230")

import assistant_page
from assistant_page import AssistantPage


class FakePoint:
    def __init__(self, x, y, event):
        self.x = x
        self.y = y
        self.event = event


class FakeTouch:
    # V3.0 官方 lvgl_touch_demo.py 将 2/3 视为 pressed。
    EVENT_DOWN = 2
    EVENT_MOVE = 3
    EVENT_UP = 4
    points = []

    def __init__(self, index):
        self.index = index

    def read(self, count):
        points = type(self).points
        type(self).points = []
        return points


class TouchServiceTest(unittest.TestCase):
    def setUp(self):
        self.original_touch = assistant_page.TOUCH
        assistant_page.TOUCH = FakeTouch

    def tearDown(self):
        assistant_page.TOUCH = self.original_touch

    def test_right_edge_swipe_opens_assistant(self):
        page = AssistantPage()

        # 当前横屏摄像头程序直接使用原始触控坐标。
        FakeTouch.points = [FakePoint(790, 220, FakeTouch.EVENT_DOWN)]
        self.assertFalse(page.poll_preview_gesture())
        FakeTouch.points = [FakePoint(650, 225, FakeTouch.EVENT_MOVE)]

        self.assertTrue(page.poll_preview_gesture())

    def test_screen_point_matches_board_touch_demo_rotation(self):
        point = FakePoint(720, 430, FakeTouch.EVENT_DOWN)

        self.assertEqual(AssistantPage._screen_point(point), (720, 430))


if __name__ == "__main__":
    unittest.main()
