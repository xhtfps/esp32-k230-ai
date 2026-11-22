#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================================
// ========================== 公共通信配置（双方共享）==========================
// ============================================================================
// 此区域配置 ESP32 与 ATmega328 之间的通信参数，修改后需双方同步编译上传

// ---------------------- 调试串口波特率 ----------------------
// ESP32 USB 串口和 Arduino USB 串口的调试输出波特率
// 常用值: 9600 / 115200 / 921600
// 注意: 这只是各自本地调试串口的波特率，不影响双方通信波特率
#define DEBUG_BAUDRATE      115200

// ---------------------- ESP32 <-> ATmega328 通信配置 ----------------------
// 双方硬件串口通信波特率，必须严格一致
// 可选: 9600 / 19200 / 38400 / 57600 / 115200
// ATmega328 使用 SoftwareSerial，不建议超过 115200
#define INTER_MCU_BAUDRATE  115200

// ESP32 侧连接 ATmega328 的硬件串口
// ESP32-S3 可选: Serial1 / Serial2
#define ESP32_INTER_SERIAL  Serial2

// ESP32 侧连接 ATmega328 的 GPIO 引脚
// 当前: ESP32 GPIO14(TX) -> Arduino Pin12(RX)
//       ESP32 GPIO??(RX) <- Arduino Pin13(TX)  [需确认接线]
#define ESP32_INTER_TX_PIN  14
#define ESP32_INTER_RX_PIN  15

// ATmega328 侧 SoftwareSerial 引脚（不可更改，已固定接线）
// Arduino Pin12=RX, Pin13=TX
#define AVR_INTER_RX_PIN    12
#define AVR_INTER_TX_PIN    13

// ---------------------- 视觉指令协议 ----------------------
// 视觉播报指令以 '#' 开头，以 '\n' 结尾
// 例如: "#FACE\n", "#OBJ1\n", "#NONE\n"
#define VISUAL_CMD_PREFIX   '#'
#define VISUAL_CMD_SUFFIX   '\n'
// 视觉指令接收超时时间，单位: 毫秒(ms)
// ATmega328 侧读取整行视觉指令时的最大等待时间
#define VISUAL_CMD_TIMEOUT_MS   50

// ---------------------- 语音按键指令码表（双方共享）----------------------
// ESP32 发送给 ATmega328 的单字节指令，ATmega328 映射为舵机控制字符
// 修改此处必须同步修改双方的 switch/case 映射表

// 左摇杆方向
#define CMD_LA      0x31    // '1' -> 舵机 '7'
#define CMD_LB      0x33    // '3' -> 舵机 '4'
#define CMD_LC      0x32    // '2' -> 舵机 '6'
#define CMD_LD      0x34    // '4' -> 舵机 '5'

// 右摇杆方向
#define CMD_RA      0x35    // '5' -> 舵机 '3'
#define CMD_RB      0x37    // '7' -> 舵机 '1'
#define CMD_RC      0x36    // '6' -> 舵机 '2'
#define CMD_RD      0x38    // '8' -> 舵机 '\n'(10)

// 方向键
#define CMD_UP      0x39    // '9' -> 舵机 'D'
#define CMD_DW      0x61    // 'a' -> 舵机 'B'
#define CMD_LF      0x62    // 'b' -> 舵机 'A'
#define CMD_RG      0x63    // 'c' -> 舵机 'C'

// 功能键
#define CMD_L1      0x42    // 'B' -> 舵机 'K'
#define CMD_L2      0x43    // 'C' -> 舵机 '8'
#define CMD_R1      0x44    // 'D' -> 舵机 'L'
#define CMD_R2      0x45    // 'E' -> 舵机 '9'
#define CMD_RESET   0x46    // 'F' -> 舵机 'J'

// 动作键
#define CMD_X       0x66    // 'f' -> 舵机 'E'
#define CMD_Y       0x64    // 'd' -> 舵机 'H'
#define CMD_A       0x65    // 'e' -> 舵机 'F'
#define CMD_B       0x41    // 'A' -> 舵机 'G'

// ---------------------- 语音指令映射表（ATmega328 查表用）----------------------
// 在 config.h 中集中定义所有语音按键指令的映射关系
// 格式: {指令码, 舵机字符, 指令名称}
// 添加新动作只需在此数组中增加一行，无需修改 arduino_receiver.cpp
#ifdef AVR
struct CmdMapping {
  byte cmd;           // 接收到的指令码
  char servoChar;     // 转发给舵机控制板的字符
  const char* name;   // 调试输出名称
};

// 指令映射表，按任意顺序排列
const CmdMapping CMD_TABLE[] = {
  {CMD_LA,    '7',  "LA"},
  {CMD_LB,    '4',  "LB"},
  {CMD_LC,    '6',  "LC"},
  {CMD_LD,    '5',  "LD"},
  {CMD_UP,    'D',  "UP"},
  {CMD_DW,    'B',  "DW"},
  {CMD_LF,    'A',  "LF"},
  {CMD_RG,    'C',  "RG"},
  {CMD_L1,    'K',  "L1"},
  {CMD_L2,    '8',  "L2"},
  {CMD_RESET, 'J',  "RESET"},
  {CMD_X,     'E',  "X"},
  {CMD_Y,     'H',  "Y"},
  {CMD_A,     'F',  "A"},
  {CMD_B,     'G',  "B"},
  {CMD_RA,    '3',  "RA"},
  {CMD_RB,    '1',  "RB"},
  {CMD_RC,    '2',  "RC"},
  {CMD_RD,    10,   "RD"},    // 10 = '\n'
  {CMD_R1,    'L',  "R1"},
  {CMD_R2,    '9',  "R2"},
};

// 映射表元素个数，用于循环遍历
#define CMD_TABLE_SIZE (sizeof(CMD_TABLE) / sizeof(CMD_TABLE[0]))

#endif // AVR

// ---------------------- SU-03T 语音模块配置（双方相关）----------------------
// SU-03T 通信波特率
#define SU03T_BAUDRATE      115200

// 语音指令有效范围，与 SU-03T 固件中配置的指令编号对应
// 当前支持: 01=已完成大字站, 02=已举双手, 03=已蹲下,
//           04=已弓箭步, 05=已金鸡独立, 06=已凹凸
#define VOICE_CMD_MIN       1   // 最小有效指令编号
#define VOICE_CMD_MAX       6   // 最大有效指令编号

// 语音帧格式定义，SU-03T 通信协议帧头帧尾
// 标准帧格式: [0xAA 0x55] <cmd> [0x55 0xAA]
#define VOICE_FRAME_HEADER_0    0xAA    // 帧头第1字节
#define VOICE_FRAME_HEADER_1    0x55    // 帧头第2字节
#define VOICE_FRAME_TAIL_0      0x55    // 帧尾第1字节
#define VOICE_FRAME_TAIL_1      0xAA    // 帧尾第2字节

// ============================================================================
// ========================== ESP32-S3 专属配置 ================================
// ============================================================================
#ifdef ESP32

// ---------------------- 调试串口 ----------------------
#define DEBUG_SERIAL        Serial

// ---------------------- WiFi AP 配置 ----------------------
// AP 热点名称，最大长度 32 字符
#define AP_SSID             "ESP32-S3-CAM"
// AP 热点密码，至少 8 字符；若为空字符串 "" 则为开放网络
#define AP_PASS             "12345678"

// ---------------------- HTTP 服务器配置 ----------------------
// HTTP 服务器监听端口，常用: 80(标准HTTP) / 8080
#define HTTP_SERVER_PORT    80

// ---------------------- SU-03T 语音模块引脚 ----------------------
// SU-03T 使用的硬件串口，ESP32-S3 可选: Serial1 / Serial2
#define SU03T_SERIAL        Serial2
// SU-03T TX 引脚(模块接收)，ESP32-S3 GPIO 范围: 0~48
#define SU03T_TX_PIN        2
// SU03T RX 引脚(模块发送)，ESP32-S3 GPIO 范围: 0~48
#define SU03T_RX_PIN        1

// ---------------------- 摄像头引脚 (ESP32-S3-CAM) ----------------------
// 以下为 ESP32-S3-CAM 开发板默认引脚分配
// 若使用其他板子，请根据实际硬件修改
// 值为 -1 表示该引脚未使用
#define CAM_PIN_PWDN        -1      // 摄像头电源关闭引脚，-1=不使用
#define CAM_PIN_RESET       -1      // 摄像头复位引脚，-1=不使用
#define CAM_PIN_XCLK        15      // 摄像头外部时钟引脚，范围: 0~48
#define CAM_PIN_SIOD        4       // SCCB 数据线(SDA)，范围: 0~48
#define CAM_PIN_SIOC        5       // SCCB 时钟线(SCL)，范围: 0~48
#define CAM_PIN_Y9          16      // 数据位 D7，范围: 0~48
#define CAM_PIN_Y8          17      // 数据位 D6，范围: 0~48
#define CAM_PIN_Y7          18      // 数据位 D5，范围: 0~48
#define CAM_PIN_Y6          12      // 数据位 D4，范围: 0~48
#define CAM_PIN_Y5          10      // 数据位 D3，范围: 0~48
#define CAM_PIN_Y4          8       // 数据位 D2，范围: 0~48
#define CAM_PIN_Y3          9       // 数据位 D1，范围: 0~48
#define CAM_PIN_Y2          11      // 数据位 D0，范围: 0~48
#define CAM_PIN_VSYNC       6       // 帧同步信号，范围: 0~48
#define CAM_PIN_HREF        7       // 行同步信号，范围: 0~48
#define CAM_PIN_PCLK        13      // 像素时钟，范围: 0~48

// ---------------------- 摄像头参数配置 ----------------------
// 摄像头 XCLK 时钟频率，单位 Hz
// 常用值: 8000000(8MHz) / 16000000(16MHz) / 20000000(20MHz)
// OV2640 最高支持 24MHz，过高会导致图像异常
#define CAM_XCLK_FREQ_HZ        20000000

// 像素格式，可选:
// PIXFORMAT_RGB565 / PIXFORMAT_YUV422 / PIXFORMAT_GRAYSCALE /
// PIXFORMAT_JPEG / PIXFORMAT_RGB888 / PIXFORMAT_RAW / PIXFORMAT_RGB444
// 网络传输推荐 PIXFORMAT_JPEG
#define CAM_PIXEL_FORMAT        PIXFORMAT_JPEG

// JPEG 压缩质量，范围: 0~63
// 0=最高质量(文件大)，63=最低质量(文件小)，推荐: 4~12
#define CAM_JPEG_QUALITY        4

// 帧缓冲数量，范围: 1~2
// 1=节省内存但可能掉帧，2=更流畅但需要更多 PSRAM
#define CAM_FB_COUNT            1

// 图像分辨率，可选:
// FRAMESIZE_96X96 / FRAMESIZE_QQVGA(160x120) / FRAMESIZE_QCIF(176x144) /
// FRAMESIZE_HQVGA(240x176) / FRAMESIZE_240X240 / FRAMESIZE_QVGA(320x240) /
// FRAMESIZE_CIF(400x296) / FRAMESIZE_HVGA(480x320) / FRAMESIZE_VGA(640x480) /
// FRAMESIZE_SVGA(800x600) / FRAMESIZE_XGA(1024x768) / FRAMESIZE_HD(1280x720) /
// FRAMESIZE_SXGA(1280x1024) / FRAMESIZE_UXGA(1600x1200)
// 注意: 分辨率越高，帧率越低，内存占用越大
#define CAM_FRAME_SIZE          FRAMESIZE_QVGA

// 帧缓冲存储位置，可选:
// CAMERA_FB_IN_DRAM   - 存放在内部 RAM(速度快，容量小)
// CAMERA_FB_IN_PSRAM  - 存放在 PSRAM(速度慢，容量大，需硬件支持)
#define CAM_FB_LOCATION         CAMERA_FB_IN_DRAM

// 图像抓取模式，可选:
// CAMERA_GRAB_WHEN_EMPTY  - 当缓冲为空时抓取(推荐，避免撕裂)
// CAMERA_GRAB_LATEST      - 始终抓取最新帧(延迟低，可能跳帧)
#define CAM_GRAB_MODE           CAMERA_GRAB_WHEN_EMPTY

// ---------------------- 摄像头传感器调参 ----------------------
// 垂直翻转，0=关闭，1=开启
#define CAM_SENSOR_VFLIP        0
// 水平镜像，0=关闭，1=开启
#define CAM_SENSOR_HMIRROR      0
// 亮度，范围: -2 ~ +2
#define CAM_SENSOR_BRIGHTNESS   0
// 对比度，范围: -2 ~ +2
#define CAM_SENSOR_CONTRAST     0
// 饱和度，范围: -2 ~ +2
#define CAM_SENSOR_SATURATION   0
// 传感器 JPEG 质量，范围: 0~63，通常与 CAM_JPEG_QUALITY 保持一致
#define CAM_SENSOR_QUALITY      4
// 增益上限，可选:
// GAINCEILING_2X / GAINCEILING_4X / GAINCEILING_8X / GAINCEILING_16X /
// GAINCEILING_32X / GAINCEILING_64X / GAINCEILING_128X
#define CAM_SENSOR_GAINCEILING  GAINCEILING_2X
// 镜头校正，0=关闭，1=开启
#define CAM_SENSOR_LENC         0
// 原始伽马校正，0=关闭，1=开启
#define CAM_SENSOR_RAW_GMA      0
// 降采样/去马赛克，0=关闭，1=开启(推荐开启可提升帧率)
#define CAM_SENSOR_DCW          1
// 彩条测试模式，0=关闭，1=开启(调试时使用)
#define CAM_SENSOR_COLORBAR     0
// 特殊效果，可选:
// 0=无 / 1=负片 / 2=灰度 / 3=红色色调 / 4=绿色色调 / 5=蓝色色调 /
// 6=复古 / 7=冷色调 / 8=暖色调
#define CAM_SENSOR_SPECIAL_EFFECT 0

// ---------------------- FPS 计算配置 ----------------------
// FPS 统计刷新间隔，单位: 毫秒(ms)
#define FPS_INTERVAL_MS         1000

#endif // ESP32

// ============================================================================
// ========================== ATmega328 专属配置 ===============================
// ============================================================================
#ifdef AVR

// ---------------------- 调试串口 ----------------------
// ATmega328 使用硬件 Serial 作为 USB 调试输出
#define DEBUG_SERIAL        Serial

// ---------------------- SoftwareSerial 引脚 ----------------------
// mySerial1: 连接舵机控制板，A0=RX, A1=TX
#define SERVO_SERIAL_RX_PIN     A0
#define SERVO_SERIAL_TX_PIN     A1

// mySerial2: 连接 ESP32-S3，12=RX, 13=TX
// 注意: 这些值与上面的 AVR_INTER_RX_PIN / AVR_INTER_TX_PIN 对应
#define ESP32_SERIAL_RX_PIN     AVR_INTER_RX_PIN
#define ESP32_SERIAL_TX_PIN     AVR_INTER_TX_PIN

// ---------------------- 功能开关 ----------------------
// 是否把视觉指令也转发给舵机控制板（0=不转发，仅调试输出；1=转发）
#define FORWARD_VISUAL_TO_SERVO 0

#endif // AVR

#endif // CONFIG_H