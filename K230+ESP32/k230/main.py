# ============================================================
# Python / MicroPython 基础模块
# ============================================================

import gc       # 垃圾回收，用于释放不用的内存
import os       # 系统相关功能，例如 exitpoint
import sys      # 系统功能，例如打印异常
import time     # 延时、计时


# ============================================================
# 导入 CanMV image 图像处理模块
# ============================================================

try:
    import image
except (ImportError, OSError):
    # 如果当前环境不是 K230 CanMV，
    # 或者固件不包含 image 模块，则设置为空
    image = None


# ============================================================
# 导入 K230 摄像头 / LCD / MediaManager
# ============================================================

try:
    # Sensor：摄像头控制
    from media.sensor import Sensor

    # Display：LCD / HDMI / IDE 等显示控制
    from media.display import Display

    # MediaManager：管理视频缓冲区、媒体资源等
    from media.media import MediaManager

    # 标记当前是否运行在支持 CanMV 的环境中
    CANMV_AVAILABLE = True

except (ImportError, OSError):
    Sensor = None
    Display = None
    MediaManager = None

    CANMV_AVAILABLE = False


# ============================================================
# 导入 ESP32 通信和视觉应用模块
# ============================================================

try:
    # VisionLink：
    # K230 与 ESP32 之间的 UART 通信模块
    from vision_link import VisionLink

    # VisionApp：
    # 负责图像发送、识别结果接收等
    #
    # empty_face_result：
    # 当前还没有真正的人脸识别模型时使用的空识别函数
    from vision_app import VisionApp, empty_face_result

    VISION_LINK_AVAILABLE = True

except (ImportError, OSError):
    VisionLink = None
    VisionApp = None
    empty_face_result = None

    VISION_LINK_AVAILABLE = False


# ============================================================
# 星云智脑二级页面（业务代码，不依赖 tests 目录）
# ============================================================

try:
    from assistant_page import AssistantPage
    ASSISTANT_PAGE_AVAILABLE = True
except (ImportError, OSError):
    AssistantPage = None
    ASSISTANT_PAGE_AVAILABLE = False


# ============================================================
# 摄像头参数
# ============================================================

# 高分辨率图像通道尺寸
#
# 这个通道主要用于：
#   1. 图像处理
#   2. JPEG 压缩
#   3. 发送给 ESP32 / 云端
IMAGE_WIDTH = 1280
IMAGE_HEIGHT = 720


# ============================================================
# LCD 显示参数
# ============================================================

# ST7701 LCD 固定分辨率
DISPLAY_WIDTH = 800
DISPLAY_HEIGHT = 480


# ============================================================
# 图像方向设置
# ============================================================

# 是否将摄像头画面旋转 180°
#
# True：
#   同时打开水平镜像和垂直翻转
#
# 等效于：
#   sensor.set_hmirror(True)
#   sensor.set_vflip(True)
ROTATE_IMAGE_180 = True


# ============================================================
# 摄像头通道设置
# ============================================================

# 通道 0：
# 专门用于 LCD 硬件绑定显示
DISPLAY_CAMERA_CHANNEL = 0

# 通道 1：
# 专门用于图像处理 / JPEG 截图
PROCESS_CAMERA_CHANNEL = 1


# ============================================================
# FPS 参数
# ============================================================

# 每隔多少毫秒重新计算一次 FPS
#
# 500ms = 每 0.5 秒刷新一次 FPS
FPS_UPDATE_INTERVAL_MS = 500


# ============================================================
# 当前是否使用硬件绑定显示模式
# ============================================================

# False：
# 默认还没有确定固件是否支持硬件绑定
#
# 如果 sensor.bind_info() 和 Display.bind_layer()
# 等接口存在，camera_init() 会自动设置为 True。
DISPLAY_BIND_MODE = False


# ============================================================
# ESP32 UART 通信开关
# ============================================================

# True：启用 ESP32 UART 图像通信
# False：完全关闭 ESP32 通信
ENABLE_ESP32_LINK = True


# ============================================================
# JPEG 压缩质量
# ============================================================

# JPEG 质量范围通常为：
#
# 低质量 ← 0 -------- 100 → 高质量
#
# 数值越高：
#   图像质量越好
#   文件越大
#   UART / 网络传输越慢
#
# 这里只影响发送给 ESP32 / 云端的 JPEG，
# 不影响 LCD 上显示的视频质量。
JPEG_QUALITY = 50


# ============================================================
# 延时函数
# ============================================================

def _sleep_ms(milliseconds):
    """
    毫秒级延时函数。

    优先使用 MicroPython / CanMV 的：
        time.sleep_ms()

    如果是在普通 Python 环境运行，
    没有 sleep_ms()，则自动使用：
        time.sleep()
    """

    try:
        # CanMV / MicroPython
        time.sleep_ms(int(milliseconds))

    except AttributeError:
        # 普通 Python
        time.sleep(float(milliseconds) / 1000.0)


# ============================================================
# CanMV IDE 停止点检测
# ============================================================

def _exitpoint():
    """
    检查 CanMV IDE 是否要求程序退出。

    CanMV IDE 点击“停止程序”时，
    os.exitpoint() 可以让程序及时退出循环。
    """

    exitpoint = getattr(os, "exitpoint", None)

    if exitpoint is not None:
        exitpoint()


# ============================================================
# 异常打印函数
# ============================================================

def _print_exception(error):
    """
    打印异常信息。

    CanMV / MicroPython 通常提供：
        sys.print_exception()

    普通 Python 环境可能没有，
    所以自动退化为 print()。
    """

    printer = getattr(sys, "print_exception", None)

    if printer is not None:
        printer(error)

    else:
        print(error)


# ============================================================
# 获取当前毫秒时间
# ============================================================

def _ticks_ms():
    """
    获取当前系统运行毫秒数。

    主要用于 FPS 计算。
    """

    ticks_ms = getattr(time, "ticks_ms", None)

    if ticks_ms is not None:
        # MicroPython / CanMV
        return ticks_ms()

    # 普通 Python 兼容方式
    return int(time.time() * 1000)


# ============================================================
# 计算两个时间点之间的时间差
# ============================================================

def _ticks_diff(new_ticks, old_ticks):
    """
    计算两个毫秒计时值之间的差。

    使用 time.ticks_diff() 可以正确处理
    MicroPython 的 ticks 计数溢出问题。
    """

    ticks_diff = getattr(time, "ticks_diff", None)

    if ticks_diff is not None:
        return ticks_diff(new_ticks, old_ticks)

    # 普通 Python 兼容方式
    return new_ticks - old_ticks


# ============================================================
# 创建 ESP32 UART 视觉应用
# ============================================================

def create_vision_app(link=None, sensor=None, assistant_store=None):
    """
    创建 K230 → ESP32 UART 通信应用。

    如果：
        ENABLE_ESP32_LINK = False

    或：
        vision_link / vision_app 模块不存在

    则直接返回 None。
    """

    # ESP32 功能关闭，或者模块导入失败
    if not ENABLE_ESP32_LINK or not VISION_LINK_AVAILABLE:
        return None

    try:

        # 如果调用函数时没有提供 VisionLink，
        # 就自动创建一个 UART 通信对象
        if link is None:
            link = VisionLink()

        # 创建视觉应用
        app_kwargs = dict(

            # K230 ↔ ESP32 通信接口
            link=link,

            # 当前没有真正运行人脸模型，
            # 所以暂时使用空的人脸检测结果
            infer_fn=empty_face_result,

            # 如果已经传入 sensor，
            # 就创建 JPEG 摄像头截图回调函数
            jpeg_fn=(
                create_jpeg_capture(sensor)
                if sensor is not None
                else None
            ),

            # 每 1000ms 发送/处理一次识别结果
            result_interval_ms=1000,
        )

        # ``assistant_store`` 是新版助手页的可选能力。仅在实际启用
        # 二级页面时传入，避免旧版 VisionApp 或旧测试替身失去兼容性。
        if assistant_store is not None:
            app_kwargs["assistant_store"] = assistant_store

        try:
            app = VisionApp(**app_kwargs)
        except TypeError as error:
            # 与旧工程中尚未加入 ``assistant_store`` 的 VisionApp 保持兼容；
            # 真正的参数错误仍然继续抛出，不能被这个回退掩盖。
            if (assistant_store is None or
                    "assistant_store" not in str(error)):
                raise
            del app_kwargs["assistant_store"]
            app = VisionApp(**app_kwargs)

        print("ESP32 UART2 vision link initialized")

        return app

    except Exception as error:

        # 如果 UART 初始化失败，
        # 不让整个摄像头程序崩溃
        print(
            "ESP32 UART2 vision link unavailable: %s"
            % error
        )

        return None


# ============================================================
# 创建 JPEG 摄像头截图函数
# ============================================================

def create_jpeg_capture(sensor):
    """
    返回一个 JPEG 截图回调函数。

    使用 PROCESS_CAMERA_CHANNEL（通道1）
    获取 1280×720 高分辨率图像，
    然后压缩成 JPEG。

    JPEG 主要用于发送给 ESP32 / 云端。
    """

    def capture_jpeg():

        # ----------------------------------------------------
        # 从高分辨率摄像头通道获取一帧
        # ----------------------------------------------------

        try:
            frame = sensor.snapshot(
                chn=PROCESS_CAMERA_CHANNEL
            )

        except TypeError:
            # 某些旧版本固件 snapshot()
            # 可能没有 chn 参数
            frame = sensor.snapshot()


        # ----------------------------------------------------
        # 将图像压缩成 JPEG
        # ----------------------------------------------------

        if hasattr(frame, "compressed"):

            # 某些 CanMV 固件使用 compressed()
            encoded = frame.compressed(
                quality=JPEG_QUALITY
            )

        elif hasattr(frame, "compress"):

            # 某些版本可能使用 compress()
            encoded = frame.compress(
                quality=JPEG_QUALITY
            )

        else:

            raise RuntimeError(
                "camera frame has no JPEG compression method"
            )


        # ----------------------------------------------------
        # 将 JPEG 图像对象转换成 bytes
        # ----------------------------------------------------

        if hasattr(encoded, "bytearray"):

            jpeg = bytes(
                encoded.bytearray()
            )

        else:

            jpeg = bytes(encoded)


        # ----------------------------------------------------
        # 检查 JPEG 是否有效
        # ----------------------------------------------------

        if len(jpeg) == 0:

            raise RuntimeError(
                "camera compression returned empty JPEG"
            )

        # 返回 JPEG 二进制数据
        return jpeg


    # 注意这里返回的是函数，
    # 而不是立即拍照得到的数据
    return capture_jpeg


# ============================================================
# 创建 FPS OSD 图层
# ============================================================

def create_fps_osd():
    """
    创建透明 OSD 图层。

    OSD = On Screen Display
    即“屏幕叠加显示”。

    摄像头视频走 VIDEO 层，
    FPS 文字走 OSD 层。

    这样修改 FPS 文字时，
    不需要重新复制整张摄像头画面。
    """

    # ARGB8888 支持透明通道，
    # 因此适合作为文字叠加层
    if image is None or not hasattr(image, "ARGB8888"):

        raise RuntimeError(
            "CanMV ARGB8888 image support unavailable"
        )


    # 创建一个与 LCD 一样大的透明图层
    return image.Image(
        DISPLAY_WIDTH,
        DISPLAY_HEIGHT,
        image.ARGB8888
    )


# ============================================================
# 更新 FPS 显示
# ============================================================

def update_fps_osd(osd_image, fps):
    """
    清除 OSD 图层上的旧 FPS，
    然后重新绘制新的 FPS 数字。
    """

    # 清除旧内容
    osd_image.clear()

    # 生成 FPS 字符串
    #
    # 例如：
    # FPS: 29.8
    text = "FPS: %.1f" % fps


    # 在 LCD 左上角绘制文字
    osd_image.draw_string(

        # X 坐标
        16,

        # Y 坐标
        16,

        # 显示内容
        text,

        # 文字颜色：红色
        color=(255, 0, 0),

        # 字体放大 2 倍
        scale=2,
    )


    return osd_image


# ============================================================
# 将摄像头画面转换成 LCD 尺寸
# ============================================================

def prepare_display_image(source_image, display_image=None):
    """
    将摄像头图像缩放到 800×480 LCD。

    使用“等比例填充”方式：

        scale = max(宽度缩放比例, 高度缩放比例)

    好处：
        图像不会发生拉伸变形。

    缺点：
        如果摄像头和屏幕长宽比不同，
        部分边缘可能会被裁切。

    例如：

        摄像头：
            1280×720
            比例 16:9

        LCD：
            800×480
            比例 5:3

    因此会稍微裁剪左右部分。
    """

    # --------------------------------------------------------
    # 获取原始图像宽高
    # --------------------------------------------------------

    try:
        source_width = source_image.width()
        source_height = source_image.height()

    except (AttributeError, TypeError):

        # 为 PC 测试环境或者某些特殊图像对象提供兼容
        return source_image


    # --------------------------------------------------------
    # 如果原图本身就是 800×480，
    # 完全不需要缩放
    # --------------------------------------------------------

    if (
        source_width == DISPLAY_WIDTH
        and source_height == DISPLAY_HEIGHT
    ):
        return source_image


    # --------------------------------------------------------
    # 缩放必须依赖 CanMV image 模块
    # --------------------------------------------------------

    if image is None:

        raise RuntimeError(
            "CanMV image module unavailable for frame scaling"
        )


    # --------------------------------------------------------
    # 第一次运行时创建 LCD 显示缓冲区
    # --------------------------------------------------------

    if display_image is None:

        display_image = image.Image(
            DISPLAY_WIDTH,
            DISPLAY_HEIGHT,

            # LCD 常用 RGB565
            image.RGB565
        )


    # --------------------------------------------------------
    # 计算缩放比例
    # --------------------------------------------------------

    scale = max(

        float(DISPLAY_WIDTH)
        / source_width,

        float(DISPLAY_HEIGHT)
        / source_height,
    )


    # 缩放后的图像宽度
    scaled_width = int(
        source_width * scale
    )

    # 缩放后的图像高度
    scaled_height = int(
        source_height * scale
    )


    # --------------------------------------------------------
    # 计算居中坐标
    # --------------------------------------------------------

    # 如果缩放后宽度超过 LCD，
    # x 会是负数，
    # 超出的左右部分自然被裁剪
    x = int(
        (DISPLAY_WIDTH - scaled_width) / 2
    )

    # 如果缩放后高度超过 LCD，
    # y 会是负数
    y = int(
        (DISPLAY_HEIGHT - scaled_height) / 2
    )


    # --------------------------------------------------------
    # 清空上一帧
    # --------------------------------------------------------

    display_image.clear()


    # --------------------------------------------------------
    # 将摄像头图像缩放后画到显示缓冲区
    # --------------------------------------------------------

    display_image.draw_image(

        # 原始图像
        source_image,

        # 显示起始位置
        x,
        y,

        # X方向缩放比例
        x_scale=scale,

        # Y方向缩放比例
        y_scale=scale,
    )

    return display_image


# ============================================================
# 摄像头 + LCD 初始化
# ============================================================

def camera_init():
    """
    初始化：

        摄像头
            ↓
        摄像头通道
            ↓
        ST7701 LCD
            ↓
        MediaManager
            ↓
        sensor.run()

    返回初始化完成的 sensor 对象。
    """

    global DISPLAY_BIND_MODE


    # --------------------------------------------------------
    # 检查是否处于 CanMV 环境
    # --------------------------------------------------------

    if not CANMV_AVAILABLE:

        raise RuntimeError(
            "CanMV hardware modules unavailable"
        )


    # --------------------------------------------------------
    # 创建摄像头对象
    # --------------------------------------------------------

    sensor = Sensor(
        width=IMAGE_WIDTH,
        height=IMAGE_HEIGHT
    )


    # 复位摄像头
    sensor.reset()


    # ========================================================
    # 检查当前固件是否支持“摄像头硬件绑定 LCD”
    # ========================================================

    can_bind_camera = all((

        # 摄像头是否支持查询绑定信息
        hasattr(sensor, "bind_info"),

        # Display 是否支持绑定视频层
        hasattr(Display, "bind_layer"),

        # 是否存在 VIDEO1 图层
        hasattr(Display, "LAYER_VIDEO1"),

        # 摄像头是否支持 YUV420SP
        hasattr(Sensor, "YUV420SP"),
    ))


    # ========================================================
    # 模式一：硬件绑定模式
    # ========================================================

    if can_bind_camera:

        # ----------------------------------------------------
        # 摄像头通道 0
        #
        # 用于 LCD 实时显示
        #
        # 分辨率直接设置为 LCD 的：
        #     800×480
        # ----------------------------------------------------

        sensor.set_framesize(
            width=DISPLAY_WIDTH,
            height=DISPLAY_HEIGHT,
            chn=DISPLAY_CAMERA_CHANNEL,
        )


        # LCD 视频层使用 YUV420SP
        #
        # YUV 数据可以通过硬件视频通路直接送到 LCD，
        # 减少 CPU 复制图像造成的性能损耗。
        sensor.set_pixformat(
            Sensor.YUV420SP,
            chn=DISPLAY_CAMERA_CHANNEL,
        )


        # ----------------------------------------------------
        # 摄像头通道 1
        #
        # 用于后续：
        #     图像处理
        #     AI
        #     JPEG
        #     ESP32
        #
        # 保留 1280×720
        # ----------------------------------------------------

        sensor.set_framesize(
            width=IMAGE_WIDTH,
            height=IMAGE_HEIGHT,
            chn=PROCESS_CAMERA_CHANNEL,
        )


        # 图像处理使用 RGB565
        sensor.set_pixformat(
            Sensor.RGB565,
            chn=PROCESS_CAMERA_CHANNEL,
        )


        # ----------------------------------------------------
        # 获取摄像头硬件绑定信息
        # ----------------------------------------------------

        bind_info = sensor.bind_info()


        # ----------------------------------------------------
        # 将摄像头画面绑定到 LCD VIDEO1 层
        # ----------------------------------------------------

        Display.bind_layer(
            **bind_info,
            layer=Display.LAYER_VIDEO1,
        )


        # 记录当前已经进入硬件绑定模式
        DISPLAY_BIND_MODE = True


    # ========================================================
    # 模式二：兼容模式
    # ========================================================

    else:

        # 某些老版本 CanMV 固件可能不支持：
        #     bind_info()
        #     bind_layer()
        #
        # 此时只设置单个摄像头通道，
        # 然后由 CPU 使用 snapshot()
        # 获取画面，再 show_image() 显示。

        sensor.set_framesize(
            width=IMAGE_WIDTH,
            height=IMAGE_HEIGHT
        )

        sensor.set_pixformat(
            Sensor.RGB565
        )

        DISPLAY_BIND_MODE = False


    # ========================================================
    # 摄像头旋转 180°
    # ========================================================

    if ROTATE_IMAGE_180:

        # 水平镜像
        sensor.set_hmirror(True)

        # 垂直翻转
        sensor.set_vflip(True)

        # 水平镜像 + 垂直翻转
        # 相当于图像旋转 180°


    # ========================================================
    # 初始化 ST7701 LCD
    # ========================================================

    print("initializing ST7701 LCD...")


    Display.init(

        # LCD 驱动类型
        Display.ST7701,

        # LCD 宽度
        width=DISPLAY_WIDTH,

        # LCD 高度
        height=DISPLAY_HEIGHT,

        # 创建 1 个 OSD 图层
        # 用来显示 FPS 等文字
        osd_num=1,

        # 同时把画面发送到 CanMV IDE
        to_ide=True,
    )


    print(
        "ST7701 LCD display initialized (%dx%d)"
        % (
            DISPLAY_WIDTH,
            DISPLAY_HEIGHT
        )
    )


    # ========================================================
    # 初始化媒体资源管理器
    # ========================================================

    print("initializing media manager...")

    MediaManager.init()


    # ========================================================
    # 启动摄像头
    # ========================================================

    print("starting camera stream...")

    sensor.run()

    print("camera stream started")


    # --------------------------------------------------------
    # 摄像头启动后等待 1 秒
    #
    # 给 Sensor / ISP / LCD 一点稳定时间
    # --------------------------------------------------------

    _sleep_ms(1000)


    # 返回摄像头对象
    return sensor


# ============================================================
# 释放摄像头 / LCD / MediaManager
# ============================================================

def camera_deinit(sensor):
    """
    释放 camera_init() 初始化的硬件资源。

    顺序：
        sensor.stop()
        Display.deinit()
        MediaManager.deinit()
    """

    # --------------------------------------------------------
    # 停止摄像头
    # --------------------------------------------------------

    try:
        if sensor is not None:
            sensor.stop()

    except Exception:
        # 即使摄像头停止失败，也继续释放其它资源
        pass


    # --------------------------------------------------------
    # 关闭 LCD
    # --------------------------------------------------------

    try:
        Display.deinit()

    except Exception:
        pass


    # --------------------------------------------------------
    # 释放媒体缓冲区
    # --------------------------------------------------------

    try:
        MediaManager.deinit()

    except Exception:
        pass


# ============================================================
# 摄像头 LCD 主循环
# ============================================================

def run_camera_lcd(sensor=None, iterations=None):
    """
    摄像头实时显示主程序。

    参数：

    sensor：
        可以传入已经初始化好的 Sensor。
        如果不传，则自动调用 camera_init()。

    iterations：
        指定运行多少帧。

        例如：
            iterations=100

        表示运行 100 次循环后退出。

        如果：
            iterations=None

        则一直运行。
    """


    # ========================================================
    # 如果没有提供 sensor，自动初始化
    # ========================================================

    if sensor is None:
        sensor = camera_init()


    # LCD 图像缓冲区
    display_image = None

    # FPS OSD 图像缓冲区
    osd_image = None


    # ========================================================
    # 初始化 ESP32 UART 视觉应用
    # ========================================================

    assistant_page = AssistantPage() if ASSISTANT_PAGE_AVAILABLE else None

    vision_app = create_vision_app(
        sensor=sensor,
        assistant_store=(assistant_page.store if assistant_page is not None else None),
    )

    if (assistant_page is not None and vision_app is not None and
            hasattr(vision_app.link, "send_ui_action")):
        assistant_page.on_action = vision_app.link.send_ui_action
        print("Nebula assistant page initialized")


    try:

        # ----------------------------------------------------
        # 总循环帧计数
        # ----------------------------------------------------

        frame_count = 0


        # ----------------------------------------------------
        # FPS 统计区间内的帧数
        # ----------------------------------------------------

        fps_frame_count = 0


        # 当前 FPS
        fps = 0.0


        # FPS 开始计时时间
        fps_start_ms = _ticks_ms()


        # ====================================================
        # 如果是硬件绑定显示模式
        # 创建一个 OSD 图层显示 FPS
        # ====================================================

        if DISPLAY_BIND_MODE:

            # 创建透明 OSD 图层
            osd_image = create_fps_osd()


            # 初始化显示 FPS = 0.0
            update_fps_osd(
                osd_image,
                fps
            )


            # 将 OSD 放到 LCD 的 OSD0 图层
            Display.show_image(

                osd_image,

                x=0,
                y=0,

                layer=Display.LAYER_OSD0,
            )


        # ====================================================
        # 摄像头主循环
        # ====================================================

        while (
            iterations is None
            or frame_count < iterations
        ):

            # ------------------------------------------------
            # 检查 IDE 是否请求退出
            # ------------------------------------------------

            _exitpoint()

            # =================================================
            # 星云智脑二级页
            #
            # 摄像头页：从右侧边缘向左滑打开。
            # 助手页：从左侧边缘向右滑返回。
            # =================================================

            if assistant_page is not None and assistant_page.visible:
                if assistant_page.pump():
                    assistant_page.close()

                if assistant_page.visible:
                    if vision_app is not None:
                        try:
                            vision_app.run_once()
                        except Exception as error:
                            _print_exception(error)
                            vision_app = None
                    frame_count += 1
                    gc.collect()
                    continue

            if assistant_page is not None and assistant_page.poll_preview_gesture():
                assistant_page.open()
                if assistant_page.visible:
                    frame_count += 1
                    gc.collect()
                    continue


            # =================================================
            # 情况一：
            # 使用摄像头 → LCD 硬件绑定
            # =================================================

            if DISPLAY_BIND_MODE:

                # ---------------------------------------------
                # 通道 0 已经通过硬件绑定直接送到 LCD
                #
                # 因此这里不需要：
                #
                # Display.show_image(camera_image)
                #
                # LCD 会自己持续刷新。
                # ---------------------------------------------


                # ---------------------------------------------
                # 获取通道 1 高分辨率图像
                #
                # 目的是：
                # 保持图像处理 / AI 通道工作。
                # ---------------------------------------------

                try:
                    img = sensor.snapshot(
                        chn=PROCESS_CAMERA_CHANNEL
                    )

                except TypeError:

                    # 兼容老固件
                    img = sensor.snapshot()


                # 当前暂时没有进一步处理 img，
                # 所以立即取消引用，方便释放内存
                img = None


                # ---------------------------------------------
                # FPS 帧数 +1
                # ---------------------------------------------

                fps_frame_count += 1


                # 获取当前时间
                now_ms = _ticks_ms()


                # 计算距离上次 FPS 更新已经过去多久
                elapsed_ms = _ticks_diff(
                    now_ms,
                    fps_start_ms
                )


                # =============================================
                # 每 500ms 更新一次 FPS
                # =============================================

                if elapsed_ms >= FPS_UPDATE_INTERVAL_MS:

                    # FPS 计算：
                    #
                    #       帧数 × 1000
                    # FPS = -----------
                    #        时间(ms)
                    #
                    fps = (
                        fps_frame_count * 1000.0
                    ) / elapsed_ms


                    # FPS 统计帧数清零
                    fps_frame_count = 0


                    # 重新设置 FPS 起始时间
                    fps_start_ms = now_ms


                    # 更新 OSD 上显示的 FPS
                    update_fps_osd(
                        osd_image,
                        fps
                    )


                    # 将新的 OSD 图层显示出来
                    Display.show_image(

                        osd_image,

                        x=0,
                        y=0,

                        layer=Display.LAYER_OSD0,
                    )


            # =================================================
            # 情况二：
            # 固件不支持摄像头硬件绑定
            # =================================================

            else:

                # ---------------------------------------------
                # CPU 主动从摄像头获取一帧
                # ---------------------------------------------

                img = sensor.snapshot()


                # ---------------------------------------------
                # 将 1280×720 摄像头画面
                # 等比例缩放并裁剪为 800×480
                # ---------------------------------------------

                display_image = prepare_display_image(
                    img,
                    display_image
                )


                # ---------------------------------------------
                # 显示到 LCD
                # ---------------------------------------------

                Display.show_image(
                    display_image
                )


                # 释放原始摄像头帧引用
                img = None


            # =================================================
            # ESP32 UART 通信
            # =================================================

            if vision_app is not None:

                try:

                    # 执行一次 UART 应用任务
                    #
                    # 可能包括：
                    #   接收 ESP32 指令
                    #   发送识别结果
                    #   获取 JPEG
                    #   发送 JPEG
                    vision_app.run_once()

                except Exception as error:

                    # 打印 UART / VisionApp 错误
                    _print_exception(error)


                    # 如果通信出现异常，
                    # 暂时禁用 vision_app
                    #
                    # 这样即使 ESP32 出问题，
                    # 摄像头和 LCD 仍然可以继续工作。
                    vision_app = None


            # =================================================
            # 主循环次数 +1
            # =================================================

            frame_count += 1


            # =================================================
            # 主动垃圾回收
            #
            # K230 / MicroPython 内存有限，
            # 摄像头图像又比较大，
            # 所以定期 gc.collect()
            # 可以降低内存不足风险。
            # =================================================

            gc.collect()


    # ========================================================
    # 捕获主循环中的异常
    # ========================================================

    except Exception as error:

        _print_exception(error)


    # ========================================================
    # 无论程序正常退出还是异常，
    # 最后都释放硬件资源
    # ========================================================

    finally:

        camera_deinit(sensor)


# ============================================================
# 主入口函数
# ============================================================

def run():
    """
    CanMV 主程序入口。

    如果把该文件保存为：

        /sdcard/main.py

    K230 启动时可以自动运行。
    """

    run_camera_lcd()


# ============================================================
# Python 主程序入口
# ============================================================

if __name__ == "__main__":

    run()
