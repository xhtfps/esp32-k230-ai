import importlib
import sys
import types
import unittest


class FakeSensor:
    RGB565 = "RGB565"
    YUV420SP = "YUV420SP"
    last = None

    def __init__(self, width, height):
        self.width = width
        self.height = height
        self.calls = []
        self.frames = 0
        FakeSensor.last = self

    def reset(self):
        self.calls.append("reset")

    def set_framesize(self, width, height, chn=0):
        self.calls.append(("set_framesize", width, height, chn))

    def set_pixformat(self, pixformat, chn=0):
        self.calls.append(("set_pixformat", pixformat, chn))

    def set_hmirror(self, value):
        self.calls.append(("set_hmirror", value))

    def set_vflip(self, value):
        self.calls.append(("set_vflip", value))

    def run(self):
        self.calls.append("run")

    def snapshot(self):
        self.frames += 1
        return "frame-%d" % self.frames

    def stop(self):
        self.calls.append("stop")


class FakeHighResolutionSensor(FakeSensor):
    def snapshot(self):
        self.frames += 1
        return FakeSourceImage(1280, 720)


class FakeJpegEncoded:
    def bytearray(self):
        return bytearray(b"\xff\xd8fake-jpeg\xff\xd9")


class FakeJpegSource:
    def compressed(self, quality=50):
        self.quality = quality
        return FakeJpegEncoded()


class FakeJpegSensor(FakeSensor):
    def __init__(self, width=1280, height=720):
        super().__init__(width, height)

    def snapshot(self, chn=1):
        return FakeJpegSource()


class FakeBoundSensor(FakeSensor):
    def bind_info(self):
        return {"source": "camera-channel-0"}


class FakeSourceImage:
    def __init__(self, width, height):
        self._width = width
        self._height = height

    def width(self):
        return self._width

    def height(self):
        return self._height


class FakeDisplayImage:
    def __init__(self, width, height, pixel_format):
        self._width = width
        self._height = height
        self.pixel_format = pixel_format
        self.calls = []

    def clear(self):
        self.calls.append("clear")

    def draw_image(self, source, x, y, x_scale=1.0, y_scale=1.0):
        self.calls.append(("draw_image", source, x, y, x_scale, y_scale))

    def draw_string(self, x, y, text, color=None, scale=1, **kwargs):
        self.calls.append(("draw_string", x, y, text, color, scale, kwargs))


class FakeImageModule:
    RGB565 = "RGB565"
    ARGB8888 = "ARGB8888"
    Image = FakeDisplayImage


class FakeDisplay:
    ST7701 = "ST7701"
    LAYER_VIDEO1 = "VIDEO1"
    LAYER_OSD0 = "OSD0"
    init_calls = []
    bind_calls = []
    shown = []

    @classmethod
    def bind_layer(cls, **kwargs):
        cls.bind_calls.append(kwargs)

    @classmethod
    def init(cls, *args, **kwargs):
        cls.init_calls.append((args, kwargs))

    @classmethod
    def show_image(cls, image, *args, **kwargs):
        cls.shown.append(image)

    @classmethod
    def deinit(cls):
        pass


class FakeMediaManager:
    init_calls = 0

    @classmethod
    def init(cls):
        cls.init_calls += 1

    @classmethod
    def deinit(cls):
        pass


class FakeVisionLink:
    instances = []

    def __init__(self):
        self.__class__.instances.append(self)


class FakeVisionApp:
    instances = []

    def __init__(self, link, infer_fn, jpeg_fn=None, result_interval_ms=200):
        self.link = link
        self.infer_fn = infer_fn
        self.jpeg_fn = jpeg_fn
        self.result_interval_ms = result_interval_ms
        self.calls = 0
        self.__class__.instances.append(self)

    def run_once(self):
        self.calls += 1
        return self.infer_fn()


class DirectCameraLcdTest(unittest.TestCase):
    def setUp(self):
        sensor_module = types.ModuleType("media.sensor")
        sensor_module.Sensor = FakeSensor
        display_module = types.ModuleType("media.display")
        display_module.Display = FakeDisplay
        media_module = types.ModuleType("media.media")
        media_module.MediaManager = FakeMediaManager
        image_module = types.ModuleType("image")
        image_module.RGB565 = FakeImageModule.RGB565
        image_module.ARGB8888 = FakeImageModule.ARGB8888
        image_module.Image = FakeImageModule.Image
        root_media = types.ModuleType("media")
        root_media.__path__ = []

        sys.modules["media"] = root_media
        sys.modules["media.sensor"] = sensor_module
        sys.modules["media.display"] = display_module
        sys.modules["media.media"] = media_module
        sys.modules["image"] = image_module
        sys.path.insert(0, "k230")
        sys.modules.pop("main", None)
        self.main = importlib.import_module("main")

        FakeDisplay.init_calls = []
        FakeDisplay.bind_calls = []
        FakeDisplay.shown = []
        FakeMediaManager.init_calls = 0
        FakeVisionLink.instances = []
        FakeVisionApp.instances = []

    def tearDown(self):
        for name in ("main", "media", "media.sensor", "media.display", "media.media", "image"):
            sys.modules.pop(name, None)

    def test_camera_init_matches_working_reference_project(self):
        sensor = self.main.camera_init()

        self.assertEqual((sensor.width, sensor.height), (1280, 720))
        self.assertIn(("set_pixformat", FakeSensor.RGB565, 0), sensor.calls)
        self.assertIn(("set_hmirror", True), sensor.calls)
        self.assertIn(("set_vflip", True), sensor.calls)
        self.assertEqual(
            FakeDisplay.init_calls,
            [((FakeDisplay.ST7701,), {
                "width": 800,
                "height": 480,
                "osd_num": 1,
                "to_ide": True,
            })],
        )
        self.assertEqual(FakeMediaManager.init_calls, 1)
        self.assertEqual(sensor.calls[-1], "run")

    def test_preview_shows_sensor_snapshot(self):
        sensor = self.main.camera_init()

        self.main.run_camera_lcd(sensor=sensor, iterations=1)

        self.assertEqual(FakeDisplay.shown, ["frame-1"])

    def test_high_resolution_frame_is_scaled_to_lcd_size(self):
        source = FakeSourceImage(1280, 720)

        display_image = self.main.prepare_display_image(source)

        self.assertEqual((display_image._width, display_image._height), (800, 480))
        self.assertEqual(display_image.pixel_format, FakeImageModule.RGB565)
        self.assertEqual(display_image.calls[0], "clear")
        self.assertEqual(display_image.calls[1][0], "draw_image")
        self.assertIs(display_image.calls[1][1], source)
        expected_scale = max(800.0 / 1280.0, 480.0 / 720.0)
        self.assertAlmostEqual(display_image.calls[1][4], expected_scale)
        self.assertAlmostEqual(display_image.calls[1][5], expected_scale)

    def test_preview_sends_scaled_frame_to_display(self):
        sensor = FakeHighResolutionSensor(1280, 720)

        self.main.run_camera_lcd(sensor=sensor, iterations=1)

        self.assertEqual(len(FakeDisplay.shown), 1)
        self.assertIsInstance(FakeDisplay.shown[0], FakeDisplayImage)
        self.assertEqual(
            (FakeDisplay.shown[0]._width, FakeDisplay.shown[0]._height),
            (800, 480),
        )

    def test_camera_init_binds_camera_to_video_layer(self):
        self.main.Sensor = FakeBoundSensor
        sensor = self.main.camera_init()

        self.assertEqual(
            FakeDisplay.bind_calls,
            [{"source": "camera-channel-0", "layer": FakeDisplay.LAYER_VIDEO1}],
        )
        self.assertIn(("set_pixformat", "YUV420SP", 0), sensor.calls)

    def test_fps_is_drawn_on_transparent_osd_image(self):
        osd_image = self.main.create_fps_osd()

        self.main.update_fps_osd(osd_image, 23.5)

        self.assertEqual((osd_image._width, osd_image._height), (800, 480))
        self.assertEqual(osd_image.pixel_format, "ARGB8888")
        self.assertEqual(osd_image.calls[0], "clear")
        self.assertEqual(osd_image.calls[1][0], "draw_string")
        self.assertIn("23.5", osd_image.calls[1][3])

    def test_bound_preview_updates_osd_without_showing_camera_frame(self):
        self.main.Sensor = FakeBoundSensor
        sensor = self.main.camera_init()

        self.main.run_camera_lcd(sensor=sensor, iterations=1)

        self.assertEqual(len(FakeDisplay.shown), 1)
        self.assertIsInstance(FakeDisplay.shown[0], FakeDisplayImage)
        self.assertEqual(FakeDisplay.shown[0].pixel_format, "ARGB8888")

    def test_create_vision_app_initializes_uart_link(self):
        self.main.VisionLink = FakeVisionLink
        self.main.VisionApp = FakeVisionApp
        self.main.empty_face_result = lambda: {"task": "face", "count": 0}

        app = self.main.create_vision_app()

        self.assertIsInstance(app, FakeVisionApp)
        self.assertIsInstance(app.link, FakeVisionLink)
        self.assertEqual(app.result_interval_ms, 1000)

    def test_jpeg_capture_returns_compressed_bytes(self):
        jpeg_fn = self.main.create_jpeg_capture(FakeJpegSensor())

        self.assertEqual(jpeg_fn(), b"\xff\xd8fake-jpeg\xff\xd9")

    def test_cloud_jpeg_quality_limits_upload_payload(self):
        self.assertLessEqual(self.main.JPEG_QUALITY, 50)

    def test_vision_app_has_jpeg_callback(self):
        self.main.VisionLink = FakeVisionLink
        self.main.VisionApp = FakeVisionApp
        self.main.empty_face_result = lambda: {"task": "face", "count": 0}

        app = self.main.create_vision_app(sensor=FakeJpegSensor())

        self.assertIsNotNone(app.jpeg_fn)

    def test_camera_loop_services_vision_app(self):
        self.main.VisionLink = FakeVisionLink
        self.main.VisionApp = FakeVisionApp
        self.main.empty_face_result = lambda: {"task": "face", "count": 0}
        sensor = self.main.camera_init()

        self.main.run_camera_lcd(sensor=sensor, iterations=2)

        self.assertEqual(len(FakeVisionApp.instances), 1)
        self.assertEqual(FakeVisionApp.instances[0].calls, 2)


if __name__ == "__main__":
    unittest.main()
