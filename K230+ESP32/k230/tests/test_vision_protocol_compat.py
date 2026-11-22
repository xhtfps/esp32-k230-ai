import sys
import unittest

sys.path.insert(0, "k230")

from vision_protocol import FrameDecoder, encode_frame


class NoItemDeletionBytearray(bytearray):
    """Model the K230 MicroPython bytearray limitation."""

    def __delitem__(self, key):
        raise TypeError("'bytearray' object doesn't support item deletion")


class VisionProtocolCompatibilityTest(unittest.TestCase):
    def decoder_with_k230_buffer(self):
        decoder = FrameDecoder()
        decoder.buffer = NoItemDeletionBytearray()
        return decoder

    def test_complete_frame_does_not_delete_from_bytearray(self):
        decoder = self.decoder_with_k230_buffer()

        frames = decoder.feed(encode_frame(0x01, 7, b"{}"))

        self.assertEqual(frames, [(0x01, 7, b"{}")])

    def test_invalid_version_does_not_delete_from_bytearray(self):
        decoder = self.decoder_with_k230_buffer()
        invalid = bytearray(b"\xA5\x5A\xFF\x01\x00\x00\x00\x00")

        self.assertEqual(decoder.feed(invalid), [])

    def test_prefix_before_magic_does_not_delete_from_bytearray(self):
        decoder = self.decoder_with_k230_buffer()

        frames = decoder.feed(b"noise" + encode_frame(0x02, 8, b"ok"))

        self.assertEqual(frames, [(0x02, 8, b"ok")])


if __name__ == "__main__":
    unittest.main()
