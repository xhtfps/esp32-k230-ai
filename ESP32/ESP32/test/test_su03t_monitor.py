import pathlib
import unittest


PROJECT_ROOT = pathlib.Path(__file__).resolve().parents[1]
MAIN_CPP = (PROJECT_ROOT / "src" / "main.cpp").read_text(encoding="utf-8")


class Su03tMonitorContractTest(unittest.TestCase):
    def test_main_has_non_blocking_su03t_receive_bridge(self):
        """The ESP32 must drain and print bytes returned by SU-03T."""
        self.assertIn("void pollSu03tRx()", MAIN_CPP)
        self.assertIn("SU03T_SERIAL.available()", MAIN_CPP)
        self.assertIn("SU03T_SERIAL.read()", MAIN_CPP)
        self.assertIn("[SU-03T RX]", MAIN_CPP)

    def test_receive_bridge_is_serviced_during_streaming_and_idle(self):
        """Streaming must not prevent the UART monitor from seeing responses."""
        stream_start = MAIN_CPP.index("void handleStream()")
        stream_end = MAIN_CPP.index("void handleStatus()")
        loop_start = MAIN_CPP.rindex("void loop()")

        self.assertIn("pollSu03tRx();", MAIN_CPP[stream_start:stream_end])
        self.assertIn("pollSu03tRx();", MAIN_CPP[loop_start:])


if __name__ == "__main__":
    unittest.main()
