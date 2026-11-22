#include <WiFi.h>
#include <WebServer.h>
#include "esp_camera.h"
#include "config.h"

// ==================== 与 ATmega328 通信的串口 ====================
// 使用 config.h 中定义的 ESP32_INTER_SERIAL 和 INTER_MCU_BAUDRATE

// ==================== SU-03T 语音模块 ====================
void sendVoiceCommand(uint8_t cmd) {
  if (cmd < VOICE_CMD_MIN || cmd > VOICE_CMD_MAX) return;

  uint8_t frame[5];
  frame[0] = VOICE_FRAME_HEADER_0;
  frame[1] = VOICE_FRAME_HEADER_1;
  frame[2] = cmd;
  frame[3] = VOICE_FRAME_TAIL_0;
  frame[4] = VOICE_FRAME_TAIL_1;

  SU03T_SERIAL.write(frame, 5);
  SU03T_SERIAL.flush();

  DEBUG_SERIAL.printf("[SU-03T] 发送语音指令: %02X %02X %02X %02X %02X (cmd=%d)\n",
                      frame[0], frame[1], frame[2], frame[3], frame[4], cmd);
}

// 非阻塞读取 SU-03T 返回数据，并转发到 USB 调试串口。
// 这里先保留原始字节，便于确认 SU-03T 实际使用的返回帧格式。
void pollSu03tRx() {
  uint8_t bytes[64];
  size_t count = 0;

  while (SU03T_SERIAL.available() && count < sizeof(bytes)) {
    int value = SU03T_SERIAL.read();
    if (value < 0) {
      break;
    }
    bytes[count++] = static_cast<uint8_t>(value);
  }

  if (count == 0) {
    return;
  }

  DEBUG_SERIAL.print("[SU-03T RX] HEX:");
  for (size_t i = 0; i < count; ++i) {
    DEBUG_SERIAL.printf(" %02X", bytes[i]);
  }

  DEBUG_SERIAL.print(" | ASCII:");
  for (size_t i = 0; i < count; ++i) {
    uint8_t byte = bytes[i];
    DEBUG_SERIAL.print((byte >= 0x20 && byte <= 0x7E) ? static_cast<char>(byte) : '.');
  }
  DEBUG_SERIAL.println();
}

// ==================== 向 ATmega328 发送指令 ====================
void sendToAtmega(byte cmd) {
  ESP32_INTER_SERIAL.write(cmd);
  ESP32_INTER_SERIAL.flush();
  DEBUG_SERIAL.printf("[ATmega] 发送指令: 0x%02X\n", cmd);
}

// 发送视觉播报指令到 ATmega328
void sendVisualToAtmega(const char* visualCmd) {
  ESP32_INTER_SERIAL.print(VISUAL_CMD_PREFIX);
  ESP32_INTER_SERIAL.print(visualCmd);
  ESP32_INTER_SERIAL.print(VISUAL_CMD_SUFFIX);
  ESP32_INTER_SERIAL.flush();
  DEBUG_SERIAL.printf("[ATmega] 发送视觉指令: #%s\n", visualCmd);
}

// ================================================================

WebServer server(HTTP_SERVER_PORT);
bool camera_ok = false;
volatile int fps = 0;
volatile int frame_count = 0;
unsigned long fps_timer = 0;
String pose_data = "{}";

void sendCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, OPTIONS");
}

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-S3-CAM</title>
<style>
  #fps{position:fixed;top:10px;left:10px;background:rgba(0,0,0,0.7);color:#0f0;
       font:bold 20px monospace;padding:5px 10px;border-radius:5px;z-index:99}
  #pose{position:fixed;top:45px;left:10px;background:rgba(0,0,0,0.7);color:#0ff;
        font:12px monospace;padding:5px 8px;border-radius:5px;z-index:99;max-width:200px}
  img{max-width:100%}
</style>
</head>
<body>
<img src="/stream">
<div id="fps">FPS: --</div>
<div id="pose">姿态: 等待中...</div>
<script>
setInterval(async()=>{
  try{
    let r=await fetch('/status');
    let j=await r.json();
    document.getElementById('fps').textContent='FPS: '+j.fps;
    let p=j.pose;
    if(p.landmarks){
      let l=p.landmarks;
      document.getElementById('pose').textContent=
        '鼻:('+f(l[0].x)+','+f(l[0].y)+') '+
        '左肩:('+f(l[11].x)+','+f(l[11].y)+') '+
        '右肩:('+f(l[12].x)+','+f(l[12].y)+')';
    }
  }catch(e){}
},500);
function f(v){return (v*100).toFixed(0)+'%'}
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
  sendCORS();
  server.send(200, "text/html", INDEX_HTML);
}

void handleCapture() {
  sendCORS();
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "fail");
    return;
  }
  server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
  server.send(200, "image/jpeg", "");
  server.sendContent((const char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void handleStream() {
  sendCORS();
  WiFiClient client = server.client();

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
  client.println("Access-Control-Allow-Origin: *");
  client.println();

  while (client.connected()) {
    pollSu03tRx();

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      delay(1);
      continue;
    }

    client.print("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ");
    client.print(fb->len);
    client.print("\r\n\r\n");
    client.write((const char*)fb->buf, fb->len);
    client.print("\r\n");

    frame_count++;
    esp_camera_fb_return(fb);
    pollSu03tRx();
  }
}

void handleStatus() {
  sendCORS();
  String json = "{\"fps\":" + String(fps) + ",\"pose\":" + pose_data + "}";
  server.send(200, "application/json", json);
}

void handlePose() {
  sendCORS();
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    pose_data = body;

    // 快速解析语音指令 (voice_cmd 字段)
    int voiceIdx = body.indexOf("\"voice_cmd\"");
    if (voiceIdx != -1) {
      int colonIdx = body.indexOf(':', voiceIdx);
      if (colonIdx != -1) {
        int valueStart = colonIdx + 1;
        while (valueStart < body.length() &&
               (body[valueStart] == ' ' || body[valueStart] == '\t')) {
          valueStart++;
        }
        int valueEnd = valueStart;
        while (valueEnd < body.length() && body[valueEnd] >= '0' && body[valueEnd] <= '9') {
          valueEnd++;
        }
        if (valueEnd > valueStart) {
          int voiceCmd = body.substring(valueStart, valueEnd).toInt();
          if (voiceCmd >= VOICE_CMD_MIN && voiceCmd <= VOICE_CMD_MAX) {
            DEBUG_SERIAL.printf("[语音] 收到指令 %d\n", voiceCmd);
            sendVoiceCommand((uint8_t)voiceCmd);
          }
        }
      }
    }

    // 解析并转发视觉播报指令到 ATmega328
    int visualIdx = body.indexOf("\"visual\"");
    if (visualIdx != -1) {
      int colonIdx = body.indexOf(':', visualIdx);
      if (colonIdx != -1) {
        int quoteStart = body.indexOf('"', colonIdx);
        int quoteEnd = body.indexOf('"', quoteStart + 1);
        if (quoteStart != -1 && quoteEnd != -1 && quoteEnd > quoteStart) {
          String visualCmd = body.substring(quoteStart + 1, quoteEnd);
          sendVisualToAtmega(visualCmd.c_str());
        }
      }
    }

    server.send(200, "application/json", "{\"ok\":1}");
  } else {
    server.send(400, "text/plain", "no body");
  }
}

void handleOptions() {
  sendCORS();
  server.send(200, "text/plain", "");
}

void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAM_PIN_Y2;
  config.pin_d1 = CAM_PIN_Y3;
  config.pin_d2 = CAM_PIN_Y4;
  config.pin_d3 = CAM_PIN_Y5;
  config.pin_d4 = CAM_PIN_Y6;
  config.pin_d5 = CAM_PIN_Y7;
  config.pin_d6 = CAM_PIN_Y8;
  config.pin_d7 = CAM_PIN_Y9;
  config.pin_xclk = CAM_PIN_XCLK;
  config.pin_pclk = CAM_PIN_PCLK;
  config.pin_vsync = CAM_PIN_VSYNC;
  config.pin_href = CAM_PIN_HREF;
  config.pin_sccb_sda = CAM_PIN_SIOD;
  config.pin_sccb_scl = CAM_PIN_SIOC;
  config.pin_pwdn = CAM_PIN_PWDN;
  config.pin_reset = CAM_PIN_RESET;
  config.xclk_freq_hz = CAM_XCLK_FREQ_HZ;
  config.pixel_format = CAM_PIXEL_FORMAT;
  config.jpeg_quality = CAM_JPEG_QUALITY;
  config.fb_count = CAM_FB_COUNT;

  config.frame_size = CAM_FRAME_SIZE;
  config.fb_location = CAM_FB_LOCATION;

  config.grab_mode = CAM_GRAB_MODE;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    DEBUG_SERIAL.printf("摄像头初始化失败: 0x%x\n", err);
    camera_ok = false;
    return;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_framesize(s, CAM_FRAME_SIZE);
    s->set_vflip(s, CAM_SENSOR_VFLIP);
    s->set_hmirror(s, CAM_SENSOR_HMIRROR);
    s->set_brightness(s, CAM_SENSOR_BRIGHTNESS);
    s->set_contrast(s, CAM_SENSOR_CONTRAST);
    s->set_saturation(s, CAM_SENSOR_SATURATION);
    s->set_quality(s, CAM_SENSOR_QUALITY);
    s->set_gainceiling(s, CAM_SENSOR_GAINCEILING);
    s->set_lenc(s, CAM_SENSOR_LENC);
    s->set_raw_gma(s, CAM_SENSOR_RAW_GMA);
    s->set_dcw(s, CAM_SENSOR_DCW);
    s->set_colorbar(s, CAM_SENSOR_COLORBAR);
    s->set_special_effect(s, CAM_SENSOR_SPECIAL_EFFECT);
  }

  camera_ok = true;
  DEBUG_SERIAL.println("摄像头 OK");
}

void setup() {
  DEBUG_SERIAL.begin(DEBUG_BAUDRATE);
  delay(200);
  DEBUG_SERIAL.println("\n--- ESP32-S3-CAM ---");

  // 初始化与 ATmega328 的通信串口
  ESP32_INTER_SERIAL.begin(INTER_MCU_BAUDRATE, SERIAL_8N1, ESP32_INTER_RX_PIN, ESP32_INTER_TX_PIN);
  DEBUG_SERIAL.printf("ATmega 通信串口已初始化: RX=GPIO%d, TX=GPIO%d, 波特率=%d\n",
                      ESP32_INTER_RX_PIN, ESP32_INTER_TX_PIN, INTER_MCU_BAUDRATE);

  // 初始化 SU-03T 语音模块串口
  SU03T_SERIAL.begin(SU03T_BAUDRATE, SERIAL_8N1, SU03T_RX_PIN, SU03T_TX_PIN);
  DEBUG_SERIAL.printf("SU-03T 串口已初始化: RX=GPIO%d, TX=GPIO%d, 波特率=%d\n",
                      SU03T_RX_PIN, SU03T_TX_PIN, SU03T_BAUDRATE);

  DEBUG_SERIAL.printf("PSRAM: %s\n", psramFound() ? "有" : "无");
  initCamera();

  WiFi.softAP(AP_SSID, AP_PASS);
  DEBUG_SERIAL.printf("AP 已启动 | IP: %s\n", WiFi.softAPIP().toString().c_str());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/capture", HTTP_GET, handleCapture);
  server.on("/stream", HTTP_GET, handleStream);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/pose", HTTP_POST, handlePose);
  server.on("/", HTTP_OPTIONS, handleOptions);
  server.on("/capture", HTTP_OPTIONS, handleOptions);
  server.on("/stream", HTTP_OPTIONS, handleOptions);
  server.on("/status", HTTP_OPTIONS, handleOptions);
  server.on("/pose", HTTP_OPTIONS, handleOptions);
  server.begin();
  DEBUG_SERIAL.printf("HTTP Server:%d 已启动\n", HTTP_SERVER_PORT);
}

void loop() {
  server.handleClient();
  pollSu03tRx();

  if (millis() - fps_timer >= FPS_INTERVAL_MS) {
    fps = frame_count;
    frame_count = 0;
    fps_timer = millis();
  }
}
