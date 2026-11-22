from pathlib import Path
import unittest


PROJECT_ROOT = Path(__file__).resolve().parents[2]
LLM_SOURCE = PROJECT_ROOT / "ESP32" / "components" / "BSP" / "LLM" / "llm.c"
LLM_CONFIG = PROJECT_ROOT / "ESP32" / "components" / "BSP" / "LLM" / "llm_config.h"
WEB_SOURCE = PROJECT_ROOT / "ESP32" / "components" / "BSP" / "WEB" / "web.c"


class LlmVisionProviderContractTest(unittest.TestCase):
    def test_config_targets_shared_beijing_dashscope_qwen_vision_api(self):
        config = LLM_CONFIG.read_text(encoding="utf-8")

        self.assertIn('"https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions"', config)
        self.assertIn('"qwen-vl-plus"', config)

    def test_request_and_response_use_chat_vision_fields(self):
        source = LLM_SOURCE.read_text(encoding="utf-8")

        self.assertIn('\\"messages\\"', source)
        self.assertIn('\\"image_url\\"', source)
        self.assertIn('"content"', source)
        self.assertIn('\\"stream\\":false', source)
        self.assertIn('DashScope request body length', source)
        self.assertIn('API key configured', source)

    def test_success_response_is_captured_during_perform_and_logged(self):
        source = LLM_SOURCE.read_text(encoding="utf-8")

        self.assertIn('HTTP_EVENT_ON_DATA', source)
        self.assertIn('event_handler = llm_http_event_handler', source)
        self.assertIn('模型回答：', source)
        self.assertNotIn('esp_http_client_read_response(client', source)

    def test_web_page_labels_and_renders_model_answer(self):
        source = WEB_SOURCE.read_text(encoding="utf-8")

        self.assertIn('大模型回答', source)
        self.assertIn('id=\\"answer\\"', source)
        self.assertIn('answer.textContent=text', source)


if __name__ == "__main__":
    unittest.main()
