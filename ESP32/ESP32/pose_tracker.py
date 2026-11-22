"""
视觉动作识别 + ESP32 通信
从 ESP32 读取 MJPEG 视频流 → MediaPipe 姿态识别 → 动作分类 → 回传 ESP32
使用前: pip install opencv-python mediapipe requests
"""

import cv2
import mediapipe as mp
import numpy as np
import time
import requests
import json
import socket
from collections import deque, Counter
from PIL import Image, ImageDraw, ImageFont


# ==================== ESP32 配置 ====================
ESP32_URL = "http://192.168.4.1"
STREAM_URL = f"{ESP32_URL}/stream"
POSE_URL = f"{ESP32_URL}/pose"

# UDP 配置（用于发送语音指令，避免和视频流冲突）
ESP32_IP = "192.168.4.1"
UDP_PORT = 8888
udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)


# ==================== 姿态识别器 ====================

class PoseRecognizer:
    """基于 MediaPipe 的姿态动作识别器"""

    def __init__(self, stability_frames=10, voice_trigger_seconds=2.0, voice_cooldown_seconds=5.0):
        self.mp_pose = mp.solutions.pose
        self.mp_drawing = mp.solutions.drawing_utils
        self.mp_drawing_styles = mp.solutions.drawing_styles

        self.pose = self.mp_pose.Pose(
            static_image_mode=False,
            model_complexity=0,
            smooth_landmarks=False,
            min_detection_confidence=0.5,
            min_tracking_confidence=0.5
        )

        self.stability_frames = stability_frames
        self.voice_trigger_seconds = voice_trigger_seconds
        self.voice_cooldown_seconds = voice_cooldown_seconds
        self.action_history = deque(maxlen=stability_frames)
        self.current_action = None
        self.action_start_time = None

        # 语音触发状态管理
        self.voice_triggered = set()  # 记录已经触发过语音的动作
        self.last_voice_time = 0      # 上次语音播报的时间戳

        self.ACTIONS = {
            "da_zi_zhan": "大字站",
            "gong_jian_bu": "弓箭步",
            "ju_shuang_shou": "举双手",
            "dun_xia": "蹲下",
            "jin_ji_du_li": "金鸡独立",
            "ao_tu": "凹凸",
            "none": "无"
        }

        # 动作对应的语音指令编号 (SU-03T 指令参数)
        self.VOICE_CMDS = {
            "da_zi_zhan": 1,
            "ju_shuang_shou": 2,
            "dun_xia": 3,
            "gong_jian_bu": 4,
            "jin_ji_du_li": 5,
            "ao_tu": 6,
        }

    def calculate_angle(self, a, b, c):
        a, b, c = np.array(a), np.array(b), np.array(c)
        ba, bc = a - b, c - b
        cosine = np.dot(ba, bc) / (np.linalg.norm(ba) * np.linalg.norm(bc) + 1e-6)
        return np.degrees(np.arccos(np.clip(cosine, -1.0, 1.0)))

    def get_landmark_point(self, landmarks, idx):
        return (landmarks.landmark[idx].x, landmarks.landmark[idx].y)

    def extract_angles(self, landmarks):
        p = lambda idx: self.get_landmark_point(landmarks, idx)

        NOSE = 0
        LEFT_SHOULDER, LEFT_ELBOW, LEFT_WRIST = 11, 13, 15
        RIGHT_SHOULDER, RIGHT_ELBOW, RIGHT_WRIST = 12, 14, 16
        LEFT_HIP, LEFT_KNEE, LEFT_ANKLE = 23, 25, 27
        RIGHT_HIP, RIGHT_KNEE, RIGHT_ANKLE = 24, 26, 28

        angles = {
            "left_shoulder":   self.calculate_angle(p(LEFT_HIP), p(LEFT_SHOULDER), p(LEFT_ELBOW)),
            "left_elbow":      self.calculate_angle(p(LEFT_SHOULDER), p(LEFT_ELBOW), p(LEFT_WRIST)),
            "right_shoulder":  self.calculate_angle(p(RIGHT_HIP), p(RIGHT_SHOULDER), p(RIGHT_ELBOW)),
            "right_elbow":     self.calculate_angle(p(RIGHT_SHOULDER), p(RIGHT_ELBOW), p(RIGHT_WRIST)),
            "left_hip":        self.calculate_angle(p(LEFT_SHOULDER), p(LEFT_HIP), p(LEFT_KNEE)),
            "left_knee":       self.calculate_angle(p(LEFT_HIP), p(LEFT_KNEE), p(LEFT_ANKLE)),
            "right_hip":       self.calculate_angle(p(RIGHT_SHOULDER), p(RIGHT_HIP), p(RIGHT_KNEE)),
            "right_knee":      self.calculate_angle(p(RIGHT_HIP), p(RIGHT_KNEE), p(RIGHT_ANKLE)),
            "left_wrist_y":    p(LEFT_WRIST)[1] - p(LEFT_SHOULDER)[1],
            "right_wrist_y":   p(RIGHT_WRIST)[1] - p(RIGHT_SHOULDER)[1],
            "left_wrist_v":    p(LEFT_WRIST)[1] - p(LEFT_SHOULDER)[1],
            "right_wrist_v":   p(RIGHT_WRIST)[1] - p(RIGHT_SHOULDER)[1],
            "ankle_dist":      abs(p(LEFT_ANKLE)[0] - p(RIGHT_ANKLE)[0]),
            "ankle_height_diff": abs(p(LEFT_ANKLE)[1] - p(RIGHT_ANKLE)[1]),
            "hip_y":           (p(LEFT_HIP)[1] + p(RIGHT_HIP)[1]) / 2,
        }
        return angles

    def recognize_action(self, angles):
        ls = angles["left_shoulder"]
        rs = angles["right_shoulder"]
        le = angles["left_elbow"]
        re = angles["right_elbow"]
        lh = angles["left_hip"]
        rh = angles["right_hip"]
        lk = angles["left_knee"]
        rk = angles["right_knee"]
        lwy = angles["left_wrist_y"]
        rwy = angles["right_wrist_y"]
        ad = angles["ankle_dist"]

        # 举双手
        if le > 150 and re > 150 and ls > 140 and rs > 140 and lwy < 0 and rwy < 0:
            return "ju_shuang_shou", 0.9

        # 蹲下
        if lk < 130 and rk < 130 and lh < 150 and rh < 150:
            return "dun_xia", 0.9

        # 大字站
        if 70 < ls < 120 and 70 < rs < 120 and lk > 150 and rk > 150 and ad > 0.12:
            return "da_zi_zhan", 0.85

        # 弓箭步
        if ((lk < 130 and rk > 140) or (rk < 130 and lk > 140)) and ad > 0.10:
            return "gong_jian_bu", 0.85

        # ========== 金鸡独立 ==========
        # 双臂水平展开（肩角度70-110，近似水平）
        # 双臂伸直（肘角>140）
        # 一膝弯曲(<130)，一膝伸直(>150)
        # 单脚抬起：脚踝高度差>0.08
        ahd = angles["ankle_height_diff"]
        if 70 < ls < 110 and 70 < rs < 110 and le > 140 and re > 140:
            if ((lk < 130 and rk > 150) or (rk < 130 and lk > 150)) and ahd > 0.08:
                return "jin_ji_du_li", 0.85

        # ========== 凹凸 ==========
        # 双脚站立（膝角>150，髋角>150）
        # 双手肘角约90度（60-130）
        # 一手腕向上，一手腕向下
        lwh = angles["left_wrist_v"]
        rwh = angles["right_wrist_v"]
        if lk > 150 and rk > 150 and lh > 150 and rh > 150:
            if (60 < le < 130) and (60 < re < 130):
                if (lwh < -0.05 and rwh > 0.05) or (lwh > 0.05 and rwh < -0.05):
                    return "ao_tu", 0.85

        return "none", 0.0

    def get_stable_action(self, action):
        self.action_history.append(action)
        if len(self.action_history) < self.stability_frames:
            return self.current_action

        counter = Counter(self.action_history)
        most_common = counter.most_common(1)[0]

        if most_common[1] >= self.stability_frames * 0.7:
            if most_common[0] != self.current_action:
                # 动作发生变化，重置语音触发状态
                if self.current_action is not None:
                    self.voice_triggered.discard(self.current_action)
                self.current_action = most_common[0]
                self.action_start_time = time.time()
            return self.current_action

        return self.current_action

    def check_voice_trigger(self):
        """检查是否需要触发语音播报
        返回: (voice_cmd_id, action_key) 或 (None, None)
        条件：
          1. 动作稳定保持超过 voice_trigger_seconds 秒
          2. 该动作未触发过语音
          3. 距离上次语音播报超过 voice_cooldown_seconds 秒（冷却时间）
        """
        if not self.current_action or self.current_action == "none":
            return None, None

        if self.current_action in self.voice_triggered:
            return None, None

        if self.action_start_time is None:
            return None, None

        # 检查冷却时间
        elapsed_since_last_voice = time.time() - self.last_voice_time
        if self.last_voice_time > 0 and elapsed_since_last_voice < self.voice_cooldown_seconds:
            return None, None

        hold_time = time.time() - self.action_start_time
        if hold_time >= self.voice_trigger_seconds:
            self.voice_triggered.add(self.current_action)
            self.last_voice_time = time.time()
            voice_cmd_id = self.VOICE_CMDS.get(self.current_action)
            if voice_cmd_id is not None:
                return voice_cmd_id, self.current_action

        return None, None

    def draw_action_info(self, image, action_key, angles, fps):
        h, w = image.shape[:2]
        # 修复 None 或空字符串显示问题
        if action_key is None or action_key == "":
            action_name = "无"
        else:
            action_name = self.ACTIONS.get(action_key, action_key)

        # 使用 PIL 绘制中文
        pil_img = Image.fromarray(cv2.cvtColor(image, cv2.COLOR_BGR2RGB))
        draw = ImageDraw.Draw(pil_img)

        # 尝试加载系统字体，失败则用默认字体
        try:
            font_large = ImageFont.truetype("simhei.ttf", 24)
            font_medium = ImageFont.truetype("simhei.ttf", 18)
            font_small = ImageFont.truetype("simhei.ttf", 14)
        except:
            try:
                font_large = ImageFont.truetype("C:/Windows/Fonts/simhei.ttf", 24)
                font_medium = ImageFont.truetype("C:/Windows/Fonts/simhei.ttf", 18)
                font_small = ImageFont.truetype("C:/Windows/Fonts/simhei.ttf", 14)
            except:
                font_large = ImageFont.load_default()
                font_medium = font_large
                font_small = font_large

        # 动作名称背景
        color = (0, 255, 0) if action_key and action_key != "none" else (128, 128, 128)
        draw.text((10, 10), f"动作: {action_name}", fill=color, font=font_large)
        draw.text((10, 40), f"FPS: {fps:.1f}", fill=(0, 255, 255), font=font_medium)

        # 角度信息
        y_offset = 70
        for name in ["left_shoulder", "right_shoulder", "left_elbow", "right_elbow",
                     "left_hip", "right_hip", "left_knee", "right_knee"]:
            if name in angles:
                draw.text((10, y_offset), f"{name}: {angles[name]:.1f}", fill=(200, 200, 200), font=font_small)
                y_offset += 18

        if self.current_action and self.action_start_time:
            hold_time = time.time() - self.action_start_time
            draw.text((w - 150, 10), f"保持: {hold_time:.1f}s", fill=(0, 255, 255), font=font_medium)

        # 转换回 OpenCV 格式
        result = cv2.cvtColor(np.array(pil_img), cv2.COLOR_RGB2BGR)
        image[:] = result[:]

    def process_frame(self, frame):
        frame = cv2.flip(frame, 1)
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        results = self.pose.process(rgb)

        action_key = "none"
        angles = {}
        landmarks_data = None

        if results.pose_landmarks:
            self.mp_drawing.draw_landmarks(
                frame, results.pose_landmarks, self.mp_pose.POSE_CONNECTIONS,
                landmark_drawing_spec=self.mp_drawing_styles.get_default_pose_landmarks_style()
            )

            angles = self.extract_angles(results.pose_landmarks)
            action_key, confidence = self.recognize_action(angles)
            action_key = self.get_stable_action(action_key)

            # 提取全身关节点（用于回传 ESP32）
            landmarks_data = []
            for i, lm in enumerate(results.pose_landmarks.landmark):
                landmarks_data.append({
                    "id": i,
                    "x": round(lm.x, 3),
                    "y": round(lm.y, 3),
                    "z": round(lm.z, 3),
                    "v": round(lm.visibility, 2)
                })

        return frame, action_key, angles, landmarks_data


# ==================== ESP32 通信 ====================

class ESP32Client:
    """与 ESP32 通信，发送姿态数据和语音指令"""

    def __init__(self):
        self.last_pose_send = 0
        self.pose_send_interval = 1.0  # 姿态数据每1秒发送一次

    def _send_udp_voice(self, voice_cmd):
        """使用 UDP 发送语音指令，避免和视频流冲突"""
        try:
            # 构建 HEX 帧: AA 55 <cmd> 55 AA
            frame = bytes([0xAA, 0x55, voice_cmd, 0x55, 0xAA])
            udp_socket.sendto(frame, (ESP32_IP, UDP_PORT))
            print(f"[UDP发送] 语音指令 {voice_cmd}: AA 55 {voice_cmd:02X} 55 AA")
            return True
        except Exception as e:
            print(f"[UDP错误] {e}")
            return False

    def _send(self, data, is_voice=False):
        try:
            response = requests.post(POSE_URL, json=data, timeout=3)
            return True
        except Exception as e:
            return False

    def send_pose(self, landmarks_data, action_key, angles, voice_cmd=None):
        """发送姿态数据，可选附带语音指令"""
        # 姿态数据发送频率控制
        should_send_pose = time.time() - self.last_pose_send >= self.pose_send_interval
        
        if voice_cmd is not None:
            # 有语音指令时，使用 UDP 发送（不阻塞，不冲突）
            print(f"[语音指令] 发送 voice_cmd={voice_cmd}")
            success = self._send_udp_voice(voice_cmd)
            if success:
                print(f"[语音指令] UDP 发送成功!")
            
            # 同时更新姿态发送时间，避免紧接着又发姿态数据
            self.last_pose_send = time.time()
            return True
        
        # 普通姿态数据，按间隔发送（异步，不阻塞主循环）
        if not should_send_pose:
            return False
            
        data = {
            "action": action_key,
            "angles": {k: round(v, 1) for k, v in angles.items()}
        }
        # 姿态数据用线程发送，避免阻塞视频流
        import threading
        threading.Thread(target=self._send, args=(data, False), daemon=True).start()
        self.last_pose_send = time.time()
        return True


# ==================== 主程序 ====================

def main():
    recognizer = PoseRecognizer(stability_frames=10, voice_trigger_seconds=2.0, voice_cooldown_seconds=5.0)
    esp32 = ESP32Client()

    print(f"连接 ESP32: {STREAM_URL}")
    cap = cv2.VideoCapture(STREAM_URL)

    if not cap.isOpened():
        print("无法连接 ESP32 视频流!")
        print("请确认: 1.电脑已连 ESP32-S3-CAM 热点  2.浏览器能打开 192.168.4.1")
        return

    print("已连接, 开始姿态识别...")
    print("支持动作: 大字站/弓箭步/举双手/蹲下/金鸡独立/凹凸")
    print("Q/ESC = 退出")

    frame_count = 0
    fps_time = time.time()
    fps = 0
    stream_fps = 0
    stream_frame_count = 0
    stream_fps_time = time.time()
    last_frame_size = 0

    while cap.isOpened():
        ret, frame = cap.read()
        if not ret:
            time.sleep(0.1)
            continue

        # 统计视频流实际帧率和帧大小
        stream_frame_count += 1
        if stream_frame_count % 30 == 0:
            stream_fps = 30 / (time.time() - stream_fps_time)
            stream_fps_time = time.time()
            # 估算帧大小 (JPEG 压缩后的近似大小)
            last_frame_size = frame.size // 1024  # KB (原始 BGR 大小，实际 JPEG 会更小)

        frame, action_key, angles, landmarks_data = recognizer.process_frame(frame)

        # 检查是否需要触发语音播报
        voice_cmd_id, voice_action = recognizer.check_voice_trigger()
        if voice_cmd_id is not None:
            action_name = recognizer.ACTIONS.get(voice_action, voice_action)
            print(f"[语音触发] 动作 '{action_name}' 已稳定 {recognizer.voice_trigger_seconds} 秒，发送语音指令 {voice_cmd_id}")
            
            # 方案: 关闭摄像头 → 发送语音指令 → 等待播报 → 重新打开
            print("[语音] 关闭视频流，准备发送语音指令...")
            cap.release()
            time.sleep(0.5)  # 等待 ESP32 处理完当前流
            
            # 发送语音指令（HTTP POST）
            voice_data = {"voice_cmd": voice_cmd_id}
            try:
                response = requests.post(POSE_URL, json=voice_data, timeout=5)
                print(f"[语音] HTTP 发送成功: status={response.status_code}")
            except Exception as e:
                print(f"[语音] HTTP 发送失败: {e}")
            
            # 等待语音播报完成（约 2 秒）
            print("[语音] 等待播报完成...")
            time.sleep(2.5)
            
            # 重新打开视频流
            print("[语音] 重新打开视频流...")
            cap = cv2.VideoCapture(STREAM_URL)
            if not cap.isOpened():
                print("[错误] 无法重新连接视频流!")
                break
            print("[语音] 视频流已恢复")

        frame_count += 1
        if frame_count % 30 == 0:
            fps = 30 / (time.time() - fps_time)
            fps_time = time.time()

        # 在画面上显示流统计信息
        h, w = frame.shape[:2]
        cv2.putText(frame, f"Stream: {stream_fps:.1f}fps {last_frame_size}KB", (10, h - 10),
                   cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 255), 1)

        recognizer.draw_action_info(frame, action_key, angles, fps)

        # 回传 ESP32 姿态数据（不包含语音指令）
        if landmarks_data:
            esp32.send_pose(landmarks_data, action_key, angles, voice_cmd=None)

        cv2.imshow("ESP32 Pose Recognition", frame)

        key = cv2.waitKey(1) & 0xFF
        if key in [ord('q'), 27]:
            break

    cap.release()
    cv2.destroyAllWindows()
    recognizer.pose.close()
    print("\n已退出")


if __name__ == "__main__":
    main()