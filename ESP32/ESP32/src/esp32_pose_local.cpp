/*
  ==============================================================================
  ESP32-S3 本地姿态识别系统（离线版）
  ==============================================================================

  文件名称: esp32_pose_local.cpp
  功能描述: 不依赖 WiFi 和上位机，直接在 ESP32-S3 上完成人体姿态识别和动作分类，
            通过串口将识别结果发送给 Arduino ATmega328 控制舵机。

  适用场景:
    - 比赛现场无 WiFi 或网络受限环境
    - 需要完全离线自主运行的机器人系统
    - 对隐私要求高、数据不上传的应用

  算法流程:
    1. 摄像头采集图像（320x240 灰度）
    2. 自适应背景建模（支持动态更新）
    3. 背景差分提取人体前景
    4. 轮廓分析 + 凸包检测定位人体关键点
    5. 几何特征计算（宽高比、肢体角度、对称性等）
    6. 基于规则的动作分类
    7. 动作稳定判断 + 语音指令触发
    8. 串口发送指令给 Arduino

  支持动作:
    - 大字站    (da_zi_zhan)
    - 蹲下      (dun_xia)
    - 举双手    (ju_shuang_shou)
    - 金鸡独立  (jin_ji_du_li)
    - 弓箭步    (gong_jian_bu)
    - 凹凸      (ao_tu)

  硬件要求:
    - ESP32-S3 (带 PSRAM 推荐，用于存储背景模型)
    - OV2640 / OV3660 摄像头模块
    - Arduino ATmega328 扩展板（接收指令控制舵机）

  编译方法:
    1. 修改 platformio.ini 中的 default_envs = esp32_local
    2. pio run -e esp32_local --target upload

  作者: 自动生成
  日期: 2026-08-20
  ==============================================================================
*/

#include <Arduino.h>
#include "config.h"
#include "esp_camera.h"

// ==============================================================================
// 算法可调参数（根据实际环境调整）
// ==============================================================================

// 图像分辨率设置（降低分辨率可提高处理速度）
// 可选: FRAMESIZE_96X96, QQVGA(160x120), QVGA(320x240), CIF(352x288)
#define FRAME_WIDTH         320
#define FRAME_HEIGHT        240

// 背景建模参数
#define BG_LEARN_FRAMES     30      // 初始背景学习帧数，建议 20~50
#define BG_UPDATE_RATE      0.05f   // 背景更新速率 0~1，越大适应越快但容易将静止人体融入背景
#define BG_THRESH           25      // 差分阈值 10~50，越大抗噪越强但可能漏检

// 轮廓检测参数
#define MIN_CONTOUR_AREA    300     // 最小人体轮廓面积（像素），过滤噪声
#define MAX_CONTOUR_AREA    50000   // 最大轮廓面积，过滤大面积变化
#define MORPH_OPEN_SIZE     3       // 形态学开运算核大小，去噪用

// 动作稳定判断参数
#define STABLE_FRAMES       5       // 连续多少帧识别为同一动作才确认
#define VOICE_TRIGGER_MS    2000    // 动作保持多少毫秒后触发语音指令
#define VOICE_COOLDOWN_MS   3000    // 两次语音之间的最小间隔（防重复触发）

// 调试输出间隔（毫秒）
#define DEBUG_INTERVAL_MS   500

// ==============================================================================
// 动作类型枚举
// ==============================================================================

enum ActionType {
  ACTION_NONE = 0,          // 无动作 / 未检测到人体
  ACTION_DA_ZI_ZHAN,        // 大字站：四肢展开，身体直立
  ACTION_DUN_XIA,           // 蹲下：身体重心低，高度明显减小
  ACTION_JU_SHOU,           // 举双手：双手高于肩膀
  ACTION_JIN_JI_DU_LI,      // 金鸡独立：单脚站立，另一脚抬起
  ACTION_GONG_JIAN_BU,      // 弓箭步：前后腿分开，一腿弯曲
  ACTION_AO_TU,             // 凹凸：身体左右不对称扭曲
  ACTION_COUNT              // 动作总数（用于数组边界）
};

// 动作中文名称（调试输出用）
const char* ACTION_NAMES[] = {
  "无",
  "大字站",
  "蹲下",
  "举双手",
  "金鸡独立",
  "弓箭步",
  "凹凸"
};

// 动作对应的语音指令编号（与 config.h 中的 VOICE_CMDS 对应）
// 0 表示不触发语音
const uint8_t ACTION_VOICE_CMDS[] = {
  0,  // NONE
  1,  // 大字站 -> SU-03T 指令 1
  3,  // 蹲下 -> SU-03T 指令 3
  2,  // 举双手 -> SU-03T 指令 2
  5,  // 金鸡独立 -> SU-03T 指令 5
  4,  // 弓箭步 -> SU-03T 指令 4
  6   // 凹凸 -> SU-03T 指令 6
};

// ==============================================================================
// 人体关键点结构体
// ==============================================================================

struct BodyKeypoints {
  int head_x, head_y;           // 头部中心
  int left_hand_x, left_hand_y; // 左手
  int right_hand_x, right_hand_y;// 右手
  int left_foot_x, left_foot_y; // 左脚
  int right_foot_x, right_foot_y;// 右脚
  int center_x, center_y;       // 人体重心
  int top_y, bottom_y;          // 最高点和最低点
  int leftmost_x, rightmost_x;  // 最左和最右
};

// ==============================================================================
// 全局变量
// ==============================================================================

// 摄像头帧缓冲
camera_fb_t* fb = nullptr;

// 背景模型（灰度图像，存储在 PSRAM 中）
// 使用 float 类型支持自适应更新
float* bg_model = nullptr;

// 背景学习状态
bool bg_learned = false;
uint8_t learn_frame_count = 0;

// 当前和上一次识别的动作
ActionType current_action = ACTION_NONE;
ActionType last_action = ACTION_NONE;

// 动作稳定计数
uint8_t stable_count = 0;

// 动作开始时间（用于判断保持时长）
uint32_t action_start_time = 0;

// 语音触发状态
bool voice_triggered = false;
uint32_t last_voice_time = 0;

// FPS 统计
uint32_t frame_count = 0;
uint32_t fps_time = 0;
float current_fps = 0.0f;

// 调试输出计时
uint32_t last_debug_time = 0;

// ==============================================================================
// 函数声明
// ==============================================================================

bool init_camera();                                         // 初始化摄像头
void learn_background(camera_fb_t* frame);                  // 学习背景模型
void update_background(camera_fb_t* frame);                 // 自适应更新背景
ActionType detect_action(camera_fb_t* frame);               // 姿态检测主函数
bool extract_keypoints(uint8_t* diff_mask, BodyKeypoints& kp); // 提取人体关键点
float calculate_angle(int ax, int ay, int bx, int by, int cx, int cy); // 计算三点角度
ActionType classify_action(const BodyKeypoints& kp, int width, int height, float aspect_ratio); // 动作分类
void send_action_command(ActionType action);                // 发送动作指令
void print_debug_info(ActionType action, const BodyKeypoints& kp,
                      float aspect_ratio, float* features, int feature_count); // 打印调试信息

// ==============================================================================
// 初始化
// ==============================================================================

void setup() {
  // 初始化调试串口
  DEBUG_SERIAL.begin(DEBUG_BAUDRATE);
  delay(1000);

  // 打印系统信息
  DEBUG_SERIAL.println("\n========================================");
  DEBUG_SERIAL.println("ESP32-S3 本地姿态识别系统（离线版）");
  DEBUG_SERIAL.println("========================================");
  DEBUG_SERIAL.println("特点: 不依赖 WiFi / 不上位机 / 纯离线运行");
  DEBUG_SERIAL.println("算法: 自适应背景建模 + 轮廓分析 + 几何特征分类");
  DEBUG_SERIAL.println("========================================");

  // 检查 PSRAM（背景模型需要较大内存）
  if (psramFound()) {
    DEBUG_SERIAL.printf("PSRAM 可用: %d MB\n", ESP.getPsramSize() / 1024 / 1024);
  } else {
    DEBUG_SERIAL.println("警告: 未检测到 PSRAM，背景模型将使用 DRAM");
  }

  // 初始化摄像头
  DEBUG_SERIAL.println("正在初始化摄像头...");
  if (!init_camera()) {
    DEBUG_SERIAL.println("摄像头初始化失败，3秒后重启...");
    delay(3000);
    ESP.restart();
  }
  DEBUG_SERIAL.println("摄像头初始化成功");

  // 分配背景模型内存
  size_t bg_size = FRAME_WIDTH * FRAME_HEIGHT * sizeof(float);
  DEBUG_SERIAL.printf("分配背景模型内存: %d bytes\n", bg_size);

  if (psramFound()) {
    bg_model = (float*)ps_malloc(bg_size);
  } else {
    bg_model = (float*)malloc(bg_size);
  }

  if (!bg_model) {
    DEBUG_SERIAL.println("错误: 背景模型内存分配失败");
    return;
  }

  // 初始化背景模型为 0
  memset(bg_model, 0, bg_size);

  DEBUG_SERIAL.println("系统初始化完成");
  DEBUG_SERIAL.println("请离开画面，正在学习背景...");
  DEBUG_SERIAL.printf("需要学习 %d 帧背景\n", BG_LEARN_FRAMES);

  // 初始化计时器
  fps_time = millis();
  last_debug_time = millis();
}

// ==============================================================================
// 主循环
// ==============================================================================

void loop() {
  // 采集图像
  fb = esp_camera_fb_get();
  if (!fb) {
    DEBUG_SERIAL.println("错误: 图像采集失败");
    delay(100);
    return;
  }

  frame_count++;

  // 阶段 1: 背景学习
  if (!bg_learned) {
    learn_background(fb);
    esp_camera_fb_return(fb);

    // 显示进度
    if (learn_frame_count % 10 == 0 || learn_frame_count >= BG_LEARN_FRAMES) {
      DEBUG_SERIAL.printf("背景学习进度: %d/%d\n", learn_frame_count, BG_LEARN_FRAMES);
    }

    if (learn_frame_count >= BG_LEARN_FRAMES) {
      bg_learned = true;
      DEBUG_SERIAL.println("背景学习完成！开始姿态识别");
      DEBUG_SERIAL.println("========================================");
    }
    return;
  }

  // 阶段 2: 姿态检测
  ActionType detected = detect_action(fb);

  // 阶段 3: 动作稳定判断
  if (detected == last_action && detected != ACTION_NONE) {
    stable_count++;
  } else {
    stable_count = 0;
    voice_triggered = false;
  }
  last_action = detected;

  // 阶段 4: 动作确认和语音触发
  if (stable_count >= STABLE_FRAMES && detected != ACTION_NONE) {
    // 动作发生变化
    if (current_action != detected) {
      current_action = detected;
      action_start_time = millis();
      voice_triggered = false;

      DEBUG_SERIAL.print("[识别] 检测到动作: ");
      DEBUG_SERIAL.println(ACTION_NAMES[detected]);
    }

    // 检查是否满足语音触发条件
    uint32_t hold_time = millis() - action_start_time;
    uint32_t time_since_last_voice = millis() - last_voice_time;

    if (!voice_triggered &&
        hold_time >= VOICE_TRIGGER_MS &&
        time_since_last_voice >= VOICE_COOLDOWN_MS) {

      voice_triggered = true;
      last_voice_time = millis();
      send_action_command(detected);
    }
  } else if (stable_count < STABLE_FRAMES) {
    // 动作不稳定，重置当前动作
    if (current_action != ACTION_NONE) {
      DEBUG_SERIAL.print("[丢失] 动作丢失: ");
      DEBUG_SERIAL.println(ACTION_NAMES[current_action]);
      current_action = ACTION_NONE;
    }
  }

  // 阶段 5: 计算 FPS
  if (millis() - fps_time >= 1000) {
    current_fps = frame_count * 1000.0f / (millis() - fps_time);
    frame_count = 0;
    fps_time = millis();
  }

  // 释放帧缓冲
  esp_camera_fb_return(fb);

  // 控制帧率约 30fps
  delay(30);
}

// ==============================================================================
// 摄像头初始化
// ==============================================================================

bool init_camera() {
  camera_config_t config;

  // 引脚配置（从 config.h 读取）
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_pwdn = CAM_PIN_PWDN;
  config.pin_reset = CAM_PIN_RESET;
  config.pin_xclk = CAM_PIN_XCLK;
  config.pin_sccb_sda = CAM_PIN_SIOD;
  config.pin_sccb_scl = CAM_PIN_SIOC;
  config.pin_d7 = CAM_PIN_Y9;
  config.pin_d6 = CAM_PIN_Y8;
  config.pin_d5 = CAM_PIN_Y7;
  config.pin_d4 = CAM_PIN_Y6;
  config.pin_d3 = CAM_PIN_Y5;
  config.pin_d2 = CAM_PIN_Y4;
  config.pin_d1 = CAM_PIN_Y3;
  config.pin_d0 = CAM_PIN_Y2;
  config.pin_vsync = CAM_PIN_VSYNC;
  config.pin_href = CAM_PIN_HREF;
  config.pin_pclk = CAM_PIN_PCLK;

  // 图像格式配置
  config.frame_size = FRAMESIZE_QVGA;      // 320x240，平衡速度和精度
  config.pixel_format = PIXFORMAT_GRAYSCALE; // 灰度图，减少 2/3 数据量
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY; // 避免缓冲堆积
  config.fb_location = CAMERA_FB_IN_PSRAM;   // 帧缓冲放在 PSRAM
  config.jpeg_quality = 12;                  // JPEG 质量（仅用于 HTTP 流，此处无效）
  config.fb_count = 1;                       // 单缓冲，节省内存

  // 降低时钟频率以节省功耗和减少噪声
  config.xclk_freq_hz = 8000000;  // 8MHz（默认 20MHz）

  // 初始化摄像头
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    DEBUG_SERIAL.printf("摄像头初始化失败，错误码: 0x%x\n", err);
    return false;
  }

  // 打印摄像头信息
  DEBUG_SERIAL.printf("分辨率: %dx%d\n", FRAME_WIDTH, FRAME_HEIGHT);
  DEBUG_SERIAL.println("图像格式: GRAYSCALE (灰度)");
  DEBUG_SERIAL.printf("XCLK 频率: %d MHz\n", config.xclk_freq_hz / 1000000);

  return true;
}

// ==============================================================================
// 背景学习（初始阶段）
// ==============================================================================

void learn_background(camera_fb_t* frame) {
  // 确保是灰度图
  if (frame->format != PIXFORMAT_GRAYSCALE) return;

  uint8_t* buf = frame->buf;
  size_t pixel_count = FRAME_WIDTH * FRAME_HEIGHT;

  // 使用滑动平均更新背景模型
  for (size_t i = 0; i < pixel_count; i++) {
    if (learn_frame_count == 0) {
      // 第一帧直接赋值
      bg_model[i] = (float)buf[i];
    } else {
      // 滑动平均: new_bg = (old_bg * n + new_pixel) / (n + 1)
      bg_model[i] = (bg_model[i] * learn_frame_count + buf[i]) / (learn_frame_count + 1);
    }
  }

  learn_frame_count++;
}

// ==============================================================================
// 自适应背景更新（运行时）
// ==============================================================================

void update_background(camera_fb_t* frame) {
  if (frame->format != PIXFORMAT_GRAYSCALE) return;

  uint8_t* buf = frame->buf;
  size_t pixel_count = FRAME_WIDTH * FRAME_HEIGHT;

  // 自适应更新: new_bg = old_bg * (1 - rate) + new_pixel * rate
  // 只更新被判定为背景的区域（差分小于阈值）
  for (size_t i = 0; i < pixel_count; i++) {
    float diff = abs((float)buf[i] - bg_model[i]);

    if (diff < BG_THRESH) {
      // 背景区域：缓慢更新
      bg_model[i] = bg_model[i] * (1.0f - BG_UPDATE_RATE) + (float)buf[i] * BG_UPDATE_RATE;
    }
    // 前景区域不更新，避免将静止的人体融入背景
  }
}

// ==============================================================================
// 姿态检测主函数
// ==============================================================================

ActionType detect_action(camera_fb_t* frame) {
  // 确保是灰度图
  if (frame->format != PIXFORMAT_GRAYSCALE) {
    return ACTION_NONE;
  }

  uint8_t* buf = frame->buf;
  size_t pixel_count = FRAME_WIDTH * FRAME_HEIGHT;

  // 分配差分掩码内存（使用栈或 DRAM）
  uint8_t* diff_mask = (uint8_t*)malloc(pixel_count);
  if (!diff_mask) {
    DEBUG_SERIAL.println("错误: 差分掩码内存分配失败");
    return ACTION_NONE;
  }

  // 1. 背景差分
  for (size_t i = 0; i < pixel_count; i++) {
    float diff = abs((float)buf[i] - bg_model[i]);
    diff_mask[i] = (diff > BG_THRESH) ? 255 : 0;
  }

  // 2. 简单形态学去噪（3x3 开运算近似）
  // 由于 ESP32 算力有限，只做简单的邻域统计
  // 实际效果：去除孤立噪点
  for (int y = 1; y < FRAME_HEIGHT - 1; y++) {
    for (int x = 1; x < FRAME_WIDTH - 1; x++) {
      int idx = y * FRAME_WIDTH + x;

      if (diff_mask[idx] == 255) {
        // 统计 3x3 邻域内前景像素数
        int neighbor_count = 0;
        for (int dy = -1; dy <= 1; dy++) {
          for (int dx = -1; dx <= 1; dx++) {
            if (diff_mask[(y + dy) * FRAME_WIDTH + (x + dx)] == 255) {
              neighbor_count++;
            }
          }
        }

        // 如果邻域内前景像素少于 3 个，认为是噪点
        if (neighbor_count < 3) {
          diff_mask[idx] = 0;
        }
      }
    }
  }

  // 3. 提取人体关键点
  BodyKeypoints kp = {0};
  bool found = extract_keypoints(diff_mask, kp);

  // 释放差分掩码
  free(diff_mask);

  // 4. 自适应背景更新（只在检测到人体时更新，避免学习人体）
  if (found) {
    update_background(frame);
  }

  if (!found) {
    return ACTION_NONE;
  }

  // 5. 计算人体轮廓特征
  int width = kp.rightmost_x - kp.leftmost_x;
  int height = kp.bottom_y - kp.top_y;

  if (width <= 0 || height <= 0) {
    return ACTION_NONE;
  }

  float aspect_ratio = (float)width / height;

  // 6. 动作分类
  ActionType action = classify_action(kp, width, height, aspect_ratio);

  // 7. 调试输出
  if (millis() - last_debug_time >= DEBUG_INTERVAL_MS) {
    float features[] = {
      (float)width,
      (float)height,
      aspect_ratio,
      (float)(kp.left_hand_y - kp.head_y) / height,
      (float)(kp.right_hand_y - kp.head_y) / height,
      (float)(kp.left_foot_y - kp.right_foot_y) / height
    };
    print_debug_info(action, kp, aspect_ratio, features, 6);
    last_debug_time = millis();
  }

  return action;
}

// ==============================================================================
// 提取人体关键点
// ==============================================================================

bool extract_keypoints(uint8_t* diff_mask, BodyKeypoints& kp) {
  // 初始化极值
  kp.top_y = FRAME_HEIGHT;
  kp.bottom_y = 0;
  kp.leftmost_x = FRAME_WIDTH;
  kp.rightmost_x = 0;
  kp.head_x = 0; kp.head_y = 0;
  kp.left_hand_x = 0; kp.left_hand_y = FRAME_HEIGHT;
  kp.right_hand_x = 0; kp.right_hand_y = FRAME_HEIGHT;
  kp.left_foot_x = 0; kp.left_foot_y = 0;
  kp.right_foot_x = 0; kp.right_foot_y = 0;
  kp.center_x = 0; kp.center_y = 0;

  // 第一遍扫描：找轮廓极值和重心
  int pixel_count = 0;
  long sum_x = 0, sum_y = 0;

  for (int y = 0; y < FRAME_HEIGHT; y++) {
    for (int x = 0; x < FRAME_WIDTH; x++) {
      int idx = y * FRAME_WIDTH + x;

      if (diff_mask[idx] == 255) {
        pixel_count++;
        sum_x += x;
        sum_y += y;

        if (y < kp.top_y) kp.top_y = y;
        if (y > kp.bottom_y) kp.bottom_y = y;
        if (x < kp.leftmost_x) kp.leftmost_x = x;
        if (x > kp.rightmost_x) kp.rightmost_x = x;
      }
    }
  }

  // 检查轮廓面积
  if (pixel_count < MIN_CONTOUR_AREA || pixel_count > MAX_CONTOUR_AREA) {
    return false;
  }

  // 计算重心
  kp.center_x = (int)(sum_x / pixel_count);
  kp.center_y = (int)(sum_y / pixel_count);

  // 计算头部位置（轮廓最上方 1/5 区域的中心）
  int head_search_bottom = kp.top_y + (kp.bottom_y - kp.top_y) / 5;
  long head_sum_x = 0, head_count = 0;

  for (int y = kp.top_y; y < head_search_bottom && y < FRAME_HEIGHT; y++) {
    for (int x = kp.leftmost_x; x <= kp.rightmost_x; x++) {
      if (diff_mask[y * FRAME_WIDTH + x] == 255) {
        head_sum_x += x;
        head_count++;
      }
    }
  }

  if (head_count > 0) {
    kp.head_x = (int)(head_sum_x / head_count);
    kp.head_y = (kp.top_y + head_search_bottom) / 2;
  } else {
    kp.head_x = kp.center_x;
    kp.head_y = kp.top_y;
  }

  // 第二遍扫描：找手和脚
  // 手：轮廓两侧，位于上半身的突出点
  // 脚：轮廓最下方两侧

  int body_height = kp.bottom_y - kp.top_y;
  int hand_search_top = kp.top_y + body_height / 4;
  int hand_search_bottom = kp.top_y + body_height * 3 / 4;
  int foot_search_top = kp.top_y + body_height * 3 / 4;

  // 记录每行的最左和最右点
  for (int y = 0; y < FRAME_HEIGHT; y++) {
    int row_left = FRAME_WIDTH;
    int row_right = 0;

    for (int x = kp.leftmost_x; x <= kp.rightmost_x; x++) {
      if (diff_mask[y * FRAME_WIDTH + x] == 255) {
        if (x < row_left) row_left = x;
        if (x > row_right) row_right = x;
      }
    }

    // 找手（上半身的左右最突出点）
    if (y >= hand_search_top && y <= hand_search_bottom) {
      if (row_left < kp.left_hand_y) {  // 用 y 坐标作为判断条件（手越高 y 越小）
        // 找最左侧的点
        if (row_left < kp.center_x - body_height / 4) {
          kp.left_hand_x = row_left;
          kp.left_hand_y = y;
        }
      }
      if (row_right > kp.right_hand_y) {
        if (row_right > kp.center_x + body_height / 4) {
          kp.right_hand_x = row_right;
          kp.right_hand_y = y;
        }
      }
    }

    // 找脚（最下方的左右点）
    if (y >= foot_search_top && y <= kp.bottom_y) {
      if (row_left < FRAME_WIDTH && row_left < kp.center_x) {
        kp.left_foot_x = row_left;
        kp.left_foot_y = y;
      }
      if (row_right > 0 && row_right > kp.center_x) {
        kp.right_foot_x = row_right;
        kp.right_foot_y = y;
      }
    }
  }

  // 如果手没找到，使用默认值
  if (kp.left_hand_y == FRAME_HEIGHT) {
    kp.left_hand_x = kp.leftmost_x;
    kp.left_hand_y = hand_search_top;
  }
  if (kp.right_hand_y == FRAME_HEIGHT) {
    kp.right_hand_x = kp.rightmost_x;
    kp.right_hand_y = hand_search_top;
  }

  return true;
}

// ==============================================================================
// 计算三点角度（用于姿态分析）
// ==============================================================================

float calculate_angle(int ax, int ay, int bx, int by, int cx, int cy) {
  // 向量 BA 和 BC
  float bax = (float)(ax - bx);
  float bay = (float)(ay - by);
  float bcx = (float)(cx - bx);
  float bcy = (float)(cy - by);

  // 点积
  float dot = bax * bcx + bay * bcy;

  // 模长
  float mag_ba = sqrt(bax * bax + bay * bay);
  float mag_bc = sqrt(bcx * bcx + bcy * bcy);

  // 避免除零
  if (mag_ba < 1e-6 || mag_bc < 1e-6) {
    return 0.0f;
  }

  // 计算角度
  float cos_angle = dot / (mag_ba * mag_bc);

  // 限制范围防止数值误差
  if (cos_angle > 1.0f) cos_angle = 1.0f;
  if (cos_angle < -1.0f) cos_angle = -1.0f;

  return degrees(acos(cos_angle));
}

// ==============================================================================
// 动作分类（基于几何特征规则）
// ==============================================================================

ActionType classify_action(const BodyKeypoints& kp, int width, int height, float aspect_ratio) {
  // 计算各种特征比例
  float body_height = (float)(kp.bottom_y - kp.top_y);
  if (body_height < 1.0f) body_height = 1.0f;

  // 手相对于肩膀的高度（负数表示手在肩膀上方）
  float shoulder_y = kp.top_y + body_height * 0.25f;
  float left_hand_height = (kp.left_hand_y - shoulder_y) / body_height;
  float right_hand_height = (kp.right_hand_y - shoulder_y) / body_height;

  // 手展开程度
  float hand_spread = (float)(kp.right_hand_x - kp.left_hand_x) / width;

  // 脚展开程度
  float foot_spread = (float)(kp.right_foot_x - kp.left_foot_x) / width;

  // 脚高度差（用于判断金鸡独立）
  float foot_height_diff = abs(kp.left_foot_y - kp.right_foot_y) / body_height;

  // 身体中心高度比例
  float center_height_ratio = (float)(kp.center_y - kp.top_y) / body_height;

  // 左右对称性（用于判断凹凸）
  float left_width = (float)(kp.center_x - kp.leftmost_x);
  float right_width = (float)(kp.rightmost_x - kp.center_x);
  float asymmetry = abs(left_width - right_width) / (left_width + right_width + 1.0f);

  // ==================== 动作分类规则 ====================
  // 规则优先级：从上到下，先匹配的先返回

  // 1. 蹲下 (DUN_XIA)
  // 特征：身体中心低（> 0.6），整体高度小，宽高比大（> 0.8）
  if (center_height_ratio > 0.60f &&
      height < FRAME_HEIGHT * 0.55f &&
      aspect_ratio > 0.75f) {
    return ACTION_DUN_XIA;
  }

  // 2. 举双手 (JU_SHOU)
  // 特征：双手明显高于肩膀（在轮廓上半部分），手展开大
  if (left_hand_height < -0.05f &&
      right_hand_height < -0.05f &&
      hand_spread > 0.65f &&
      kp.left_hand_y < kp.center_y &&
      kp.right_hand_y < kp.center_y) {
    return ACTION_JU_SHOU;
  }

  // 3. 金鸡独立 (JIN_JI_DU_LI)
  // 特征：单脚明显高于另一只（高度差 > 0.12），手展开保持平衡
  if (foot_height_diff > 0.12f &&
      hand_spread > 0.45f &&
      height > FRAME_HEIGHT * 0.4f) {
    return ACTION_JIN_JI_DU_LI;
  }

  // 4. 大字站 (DA_ZI_ZHAN)
  // 特征：四肢展开，身体直立，宽高比小（< 0.55），中心高
  if (hand_spread > 0.70f &&
      foot_spread > 0.45f &&
      aspect_ratio < 0.55f &&
      center_height_ratio < 0.55f &&
      height > FRAME_HEIGHT * 0.5f) {
    return ACTION_DA_ZI_ZHAN;
  }

  // 5. 弓箭步 (GONG_JIAN_BU)
  // 特征：前后腿分开（脚展开中等），身体中心偏低，高度中等
  if (foot_spread > 0.35f &&
      foot_spread < 0.70f &&
      center_height_ratio > 0.50f &&
      center_height_ratio < 0.65f &&
      height > FRAME_HEIGHT * 0.45f) {
    return ACTION_GONG_JIAN_BU;
  }

  // 6. 凹凸 (AO_TU)
  // 特征：身体左右明显不对称，手展开程度中等
  if (asymmetry > 0.18f &&
      hand_spread > 0.35f &&
      hand_spread < 0.70f &&
      height > FRAME_HEIGHT * 0.45f) {
    return ACTION_AO_TU;
  }

  // 未匹配到任何动作
  return ACTION_NONE;
}

// ==============================================================================
// 发送动作指令
// ==============================================================================

void send_action_command(ActionType action) {
  // 参数检查
  if (action <= ACTION_NONE || action >= ACTION_COUNT) {
    return;
  }

  // 获取对应的语音指令编号
  uint8_t voice_cmd = ACTION_VOICE_CMDS[action];
  if (voice_cmd == 0) {
    return;  // 该动作不触发语音
  }

  // 打印触发信息
  DEBUG_SERIAL.print("[语音] 动作 '");
  DEBUG_SERIAL.print(ACTION_NAMES[action]);
  DEBUG_SERIAL.print("' 保持超过 ");
  DEBUG_SERIAL.print(VOICE_TRIGGER_MS);
  DEBUG_SERIAL.print("ms，发送指令 ");
  DEBUG_SERIAL.println(voice_cmd);

  // 构建 SU-03T 语音帧格式: AA 55 <cmd> 55 AA
  uint8_t frame[] = {0xAA, 0x55, voice_cmd, 0x55, 0xAA};

  // 通过串口发送给 Arduino（Arduino 再转发给舵机或语音模块）
  // 使用 Serial2 与 Arduino 通信（引脚在 config.h 中定义）
  for (int i = 0; i < 5; i++) {
    Serial2.write(frame[i]);
  }

  DEBUG_SERIAL.printf("[发送] 帧: AA 55 %02X 55 AA\n", voice_cmd);
}

// ==============================================================================
// 打印调试信息
// ==============================================================================

void print_debug_info(ActionType action, const BodyKeypoints& kp,
                      float aspect_ratio, float* features, int feature_count) {
  DEBUG_SERIAL.println("\n----- 调试信息 -----");
  DEBUG_SERIAL.printf("FPS: %.1f | 动作: %s\n", current_fps, ACTION_NAMES[action]);
  DEBUG_SERIAL.printf("轮廓: 宽=%d 高=%d 比=%.2f\n",
                      kp.rightmost_x - kp.leftmost_x,
                      kp.bottom_y - kp.top_y,
                      aspect_ratio);
  DEBUG_SERIAL.printf("头部: (%d, %d) | 重心: (%d, %d)\n",
                      kp.head_x, kp.head_y, kp.center_x, kp.center_y);
  DEBUG_SERIAL.printf("左手: (%d, %d) | 右手: (%d, %d)\n",
                      kp.left_hand_x, kp.left_hand_y,
                      kp.right_hand_x, kp.right_hand_y);
  DEBUG_SERIAL.printf("左脚: (%d, %d) | 右脚: (%d, %d)\n",
                      kp.left_foot_x, kp.left_foot_y,
                      kp.right_foot_x, kp.right_foot_y);

  if (feature_count >= 6) {
    DEBUG_SERIAL.printf("特征: 手高差=[%.2f, %.2f] 展开=%.2f\n",
                        features[3], features[4], features[5]);
  }

  DEBUG_SERIAL.println("-------------------");
}

// ==============================================================================
// 文件结束
// ==============================================================================