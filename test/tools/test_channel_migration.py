"""Repository-rename fixtures exercise real channel assembly, not physical firmware."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from make_channels import build_channels, validate_manifest


def manifest(repository, tag):
    base = f'https://github.com/{repository}/releases/download/{tag}/'
    return {'tag_name': tag, 'assets': [
        {'name': 'm5stickc-plus2-firmware.bin' + suffix,
         'browser_download_url': base + 'm5stickc-plus2-firmware.bin' + suffix}
        for suffix in ('', '.sig')]}


class ChannelMigrationTests(unittest.TestCase):
    def test_old_manifest_builds_new_links_without_changing_archived_payload(self):
        original = manifest('bdlglua/BDLMinerV1', 'v2.0.0')
        before = copy.deepcopy(original)
        def fetch(tag, folder):
            (folder / 'boards.json').write_text(json.dumps({'m5stickc-plus2': 'esp32'}))
            (folder / 'm5stickc-plus2-factory.bin').write_bytes(b'fixture')
            (folder / 'update.json').write_text(json.dumps(original))
        with tempfile.TemporaryDirectory() as temp:
            build_channels([{'tag_name': 'v2.0.0'}], fetch, temp, 'bdlglua/BDLMinerV2')
            for channel in ('stable', 'beta'):
                emitted = json.loads((Path(temp) / 'updates' / (channel + '.json')).read_text())
                self.assertEqual(emitted, manifest('bdlglua/BDLMinerV2', 'v2.0.0'))
            self.assertEqual(original, before)

    def test_alias_is_limited_to_exact_old_repository_and_pre_rename_versions(self):
        boards = {'m5stickc-plus2': 'esp32'}
        for repository, target, tag in (
            ('bdlglua/BDLMinerV1', 'bdlglua/BDLMinerV2', 'v2.0.1'),
            ('someone/BDLMinerV1', 'bdlglua/BDLMinerV2', 'v2.0.0'),
            ('bdlglua/BDLMinerV1', 'someone/BDLMinerV2', 'v2.0.0')):
            with self.assertRaises(ValueError):
                validate_manifest(manifest(repository, tag), target, tag, boards)
        for suffix in ('?redirect=elsewhere', '/other'):
            bad = manifest('bdlglua/BDLMinerV1', 'v2.0.0')
            bad['assets'][0]['browser_download_url'] += suffix
            with self.assertRaises(ValueError):
                validate_manifest(bad, 'bdlglua/BDLMinerV2', 'v2.0.0', boards)


if __name__ == '__main__':
    unittest.main()
