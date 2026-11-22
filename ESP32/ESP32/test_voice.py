"""
测试语音指令发送
直接发送 HTTP POST 到 ESP32，测试 SU-03T 是否能播报
"""
import requests
import time

ESP32_URL = "http://192.168.4.1"
POSE_URL = f"{ESP32_URL}/pose"

def test_voice(cmd_id, description):
    """发送语音指令测试"""
    data = {"voice_cmd": cmd_id}
    print(f"\n[测试] 发送 '{description}' (cmd={cmd_id})")
    try:
        response = requests.post(POSE_URL, json=data, timeout=5)
        print(f"[测试] 响应: {response.status_code} - {response.text}")
    except Exception as e:
        print(f"[测试] 错误: {e}")

if __name__ == "__main__":
    print("=" * 50)
    print("SU-03T 语音指令测试工具")
    print("=" * 50)
    print("\n请确保:")
    print("1. 电脑已连接 ESP32-S3-CAM WiFi 热点")
    print("2. ESP32 已上电并运行")
    print("3. SU-03T 已正确接线并上电")
    print()
    
    # 测试所有语音指令
    test_voice(1, "已完成大字站")
    time.sleep(3)
    
    test_voice(2, "已举双手")
    time.sleep(3)
    
    test_voice(3, "已蹲下")
    time.sleep(3)
    
    test_voice(4, "已弓箭步")
    time.sleep(3)

    test_voice(5, "已金鸡独立")
    time.sleep(3)

    test_voice(6, "已凹凸")
    time.sleep(3)

    print("\n" + "=" * 50)
    print("测试完成！")
    print("=" * 50)