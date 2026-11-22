/*
  Arduino 接收端 (ATmega328)
  - mySerial1(A0, A1): 连接舵机控制板
  - mySerial2(12, 13): 连接 ESP32-S3
  - Serial: USB 调试串口

  接收数据类型：
  1. 语音按键指令：单字节 ASCII（通过 config.h 中的 CMD_TABLE 查表转发）
  2. 视觉播报指令：以 '#' 开头的字符串，如 #FACE、#OBJ1、#NONE

  所有通信参数均从 config.h 读取，确保与 ESP32 同步
  添加新动作只需在 config.h 的 CMD_TABLE 中增加一行
*/

#include <Arduino.h>
#include <SoftwareSerial.h>
#include "config.h"

// SoftwareSerial 实例化
SoftwareSerial mySerial1(SERVO_SERIAL_RX_PIN, SERVO_SERIAL_TX_PIN);  // 舵机控制板
SoftwareSerial mySerial2(ESP32_SERIAL_RX_PIN, ESP32_SERIAL_TX_PIN);  // ESP32-S3

// 函数前置声明
void handleVisualCommand();
void handleVoiceCommand(byte cmd);

void setup() {
  DEBUG_SERIAL.begin(DEBUG_BAUDRATE);
  mySerial1.begin(INTER_MCU_BAUDRATE);
  mySerial2.begin(INTER_MCU_BAUDRATE);

  DEBUG_SERIAL.println("Arduino 接收端已启动");
  DEBUG_SERIAL.println("mySerial1(A0,A1) -> 舵机控制板 @ " + String(INTER_MCU_BAUDRATE));
  DEBUG_SERIAL.println("mySerial2(12,13) <- ESP32-S3 @ " + String(INTER_MCU_BAUDRATE));
}

void loop() {
  // 处理 ESP32-S3 发来的数据
  while (mySerial2.available()) {
    byte cmd = mySerial2.read();

    if (cmd == VISUAL_CMD_PREFIX) {
      handleVisualCommand();
    } else {
      handleVoiceCommand(cmd);
    }
  }
}

// 处理以 '#' 开头的视觉播报字符串
void handleVisualCommand() {
  String visualCmd = String(VISUAL_CMD_PREFIX);
  unsigned long startTime = millis();

  // 读取整行，直到换行或超时
  while ((mySerial2.available() || (millis() - startTime < VISUAL_CMD_TIMEOUT_MS))) {
    if (mySerial2.available()) {
      char c = mySerial2.read();
      visualCmd += c;
      if (c == VISUAL_CMD_SUFFIX) {
        break;
      }
    }
  }

  DEBUG_SERIAL.print("收到视觉指令: ");
  DEBUG_SERIAL.println(visualCmd);

#if FORWARD_VISUAL_TO_SERVO
  mySerial1.println(visualCmd);
#endif
}

// 处理单字节语音按键指令，通过查表转发给舵机控制板
// 如需添加新动作，只需在 config.h 的 CMD_TABLE 中增加一行
void handleVoiceCommand(byte cmd) {
  for (size_t i = 0; i < CMD_TABLE_SIZE; i++) {
    if (CMD_TABLE[i].cmd == cmd) {
      mySerial1.println(CMD_TABLE[i].servoChar);
      DEBUG_SERIAL.print("收到指令 ");
      DEBUG_SERIAL.print(CMD_TABLE[i].name);
      DEBUG_SERIAL.print(" (0x");
      DEBUG_SERIAL.print(cmd, HEX);
      DEBUG_SERIAL.println(")");
      return;
    }
  }
  // 未在表中找到匹配项
  DEBUG_SERIAL.print("收到未知指令 (0x");
  DEBUG_SERIAL.print(cmd, HEX);
  DEBUG_SERIAL.println(")");
}