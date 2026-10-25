"""Offline regression tests for release packaging and usage reporting."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import make_release
import miner_stats


class ReleaseToolsTests(unittest.TestCase):
    def test_partial_unsigned_rebuild_removes_stale_signature_and_keeps_checksums(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            out = root / "release" / "1.0.8"
            out.mkdir(parents=True)
            firmware = root / "firmware.bin"
            firmware.write_bytes(b"new firmware")
            (out / "alpha-firmware.bin").write_bytes(b"old firmware")
            (out / "alpha-firmware.bin.sig").write_bytes(b"old signature")
            (out / "beta-firmware.bin").write_bytes(b"other firmware")
            (out / "beta-factory.bin").write_bytes(b"other factory")
            (out / "boards.json").write_text(json.dumps({"alpha": "esp32", "beta": "esp32c3"}))

            def merge(command, **kwargs):
                Path(command[command.index("-o") + 1]).write_bytes(b"new factory")

            layout = ("esp32", "dio", "40m", "4MB", [("0x10000", str(firmware))])
            with (patch.object(make_release, "ROOT", str(root)),
                  patch.object(make_release, "version", return_value="1.0.8"),
                  patch.object(make_release, "signing_key", return_value=None),
                  patch.object(make_release, "run"),
                  patch.object(make_release, "flash_layout", return_value=layout),
                  patch.object(make_release.subprocess, "run", side_effect=merge),
                  patch.object(sys, "argv", ["make_release.py", "alpha"]),
                  contextlib.redirect_stdout(io.StringIO())):
                make_release.main()

            with self.subTest("stale signature"):
                self.assertFalse((out / "alpha-firmware.bin.sig").exists())
            self.assertEqual((out / "alpha-firmware.bin").read_bytes(), b"new firmware")
            checksums = dict(line.split("  ")[::-1] for line in (out / "SHA256SUMS.txt").read_text().splitlines())
            binaries = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in out.glob("*.bin")}
            with self.subTest("checksums for all boards"):
                self.assertEqual(checksums, binaries)
            self.assertEqual(json.loads((out / "boards.json").read_text()), {"alpha": "esp32", "beta": "esp32c3"})

    def test_usage_report_accepts_two_and_three_component_versions(self):
        releases = [
            {"tag_name": "v1.0", "assets": [{"name": "ping-esp32.txt", "download_count": 2}]},
            {"tag_name": "v1.0.1", "assets": [{"name": "ping-esp32.txt", "download_count": 3}]},
            {"tag_name": "v1.0.10", "assets": [{"name": "ping-esp32.txt", "download_count": 4}]},
        ]
        with tempfile.TemporaryDirectory() as temp:
            state = Path(temp) / "stats.json"
            output = io.StringIO()
            with (patch.object(miner_stats, "STATE", str(state)),
                  patch.object(miner_stats, "releases", return_value=releases),
                  contextlib.redirect_stdout(output)):
                miner_stats.main()
            rows = output.getvalue().splitlines()[1:4]
            self.assertEqual([row.split()[0] for row in rows], ["1.0.10", "1.0.1", "1.0"])
            self.assertEqual(len(json.loads(state.read_text())["counts"]), 3)


if __name__ == "__main__":
    unittest.main()
