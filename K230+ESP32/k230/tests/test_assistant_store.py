import sys
import unittest

sys.path.insert(0, "k230")

from assistant_page import AssistantStore, STATE_LISTENING


class AssistantStoreTest(unittest.TestCase):
    def test_state_snapshot_preserves_session_and_error(self):
        store = AssistantStore()

        store.apply_state(STATE_LISTENING, 0x1234, 7)

        state = store.snapshot()
        self.assertEqual(state["state"], STATE_LISTENING)
        self.assertEqual(state["session_id"], 0x1234)
        self.assertEqual(state["error_code"], 7)

    def test_answer_is_bounded_and_marks_truncation(self):
        store = AssistantStore()

        store.append_answer("A" * 500)

        state = store.snapshot()
        self.assertLessEqual(len(state["answer"].encode("utf-8")), 384)
        self.assertTrue(state["answer"].endswith("..."))

    def test_link_expires_after_three_seconds(self):
        store = AssistantStore()

        store.set_link_online(True, 1000)

        self.assertTrue(store.is_link_online(3999))
        self.assertFalse(store.is_link_online(4001))


if __name__ == "__main__":
    unittest.main()
