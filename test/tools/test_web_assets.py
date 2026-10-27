"""Verify the actual firmware asset bytes, compression and cache invalidation."""
import gzip
from pathlib import Path
import re
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import make_web


class WebAssetsTests(unittest.TestCase):
    def test_packed_assets_round_trip_and_change_cache_version(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            shutil.copytree(make_web.ROOT / 'web', root / 'web')
            (root / 'src').mkdir()
            output = make_web.generate(root)
            first = output.read_text()
            for name in (*make_web.SCRIPTS, 'style'):
                symbol = 'kWeb' + name.capitalize()
                raw = re.search(r'const uint8_t ' + symbol + r'\[\] PROGMEM = \{(.*?)\};', first, re.S)[1]
                packed = bytes(int(n) for n in re.findall(r'\d+', raw))
                source = (root / 'web' / (name + ('.css' if name == 'style' else '.js'))).read_bytes()
                self.assertEqual(gzip.decompress(packed), source)
                self.assertLess(len(packed), len(source))
            make_web.generate(root)
            self.assertEqual(first, output.read_text())
            style = root / 'web/style.css'
            style.write_text(style.read_text() + '\n/* cache invalidation */\n')
            make_web.generate(root)
            version = lambda text: re.search(r'kWebVersion\[\] = "([^"]+)"', text)[1]
            self.assertNotEqual(version(first), version(output.read_text()))

    def test_failed_asset_publication_preserves_complete_previous_header(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            shutil.copytree(make_web.ROOT / 'web', root / 'web')
            (root / 'src').mkdir()
            output = make_web.generate(root)
            original = output.read_bytes()
            with (root / 'web/style.css').open('a') as stream:
                stream.write('\n/* changed source */\n')
            with patch.object(make_web.os, 'replace', side_effect=OSError('disk error')):
                with self.assertRaisesRegex(OSError, 'disk error'):
                    make_web.generate(root)
            self.assertEqual(output.read_bytes(), original)
            self.assertEqual(list(output.parent.iterdir()), [output])


if __name__ == '__main__':
    unittest.main()
