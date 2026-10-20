import pathlib
import tempfile
import unittest
from unittest.mock import patch

import unwrap


class ImageRepairTest(unittest.TestCase):
    def test_data1_rva_and_run_crossing_iat_boundary(self):
        overlay = bytes(0x2D0000)
        stub = bytearray(overlay)
        working = bytearray(0x619000)
        working[0x256484:0x25648C] = b"XXXXXXXX"
        working[0x614070:0x614074] = b"GOOD"
        stub[0x2CC000:0x2CC004] = b"RSRC"
        working[0x615000:0x615004] = b"RSRC"
        with tempfile.TemporaryDirectory() as directory:
            paths = [pathlib.Path(directory) / name for name in ("overlay", "stub", "working")]
            for path, data in zip(paths, (overlay, stub, working)):
                path.write_bytes(data)
            with patch.multiple(unwrap, OVERLAY=paths[0], STUB=paths[1], WORKING=paths[2]):
                image = unwrap.build()
        self.assertEqual(image[0x256484:0x25648C], b"\0\0\0\0XXXX")
        self.assertEqual(image[0x2CB070:0x2CB074], b"GOOD")
        self.assertEqual(image[0x2CC000:0x2CC004], b"RSRC")


if __name__ == "__main__":
    unittest.main()
