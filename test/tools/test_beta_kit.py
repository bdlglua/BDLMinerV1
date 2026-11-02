"""Offline test-kit integrity and read-only fleet readiness checks."""
import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from bdlhost.client import Device,DeviceError,sanitize
from build_output import staged_directory
import check_fleet
from make_beta_bundle import build_kit,archive_kit
from test_v2_host import reading,partial
import make_release


class BetaKitTests(unittest.TestCase):
    def test_readiness_does_not_pass_old_firmware_pending_trials_or_stopped_miners(self):
        device=Device('gamma','Gamma','http://127.0.0.1')
        for case in ('ready','old','trial','pool','stopped','paused','unreachable'):
            sample=reading()
            if case=='old':sample['firmware']='1.9.15'
            elif case=='trial':sample['firmware_trial']=True
            elif case=='pool':sample['pool']['connected']=False
            elif case=='stopped':sample['hashrate']=0
            elif case=='paused':sample['power_percent']=0
            with patch.object(check_fleet.Client,'stats',return_value=sanitize(copy.deepcopy(sample)),
                              side_effect=DeviceError('offline') if case=='unreachable' else None):
                result=check_fleet.collect([device],'2.0.0-beta.1')
            self.assertEqual(result['status'],'ready' if case=='ready' else 'review',case)
            self.assertNotIn('HIDDEN',json.dumps(result))
            self.assertTrue(result['read_only'])

    def test_complete_kit_checksums_launchers_archive_and_corrupt_input_recovery(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);release=root/'2.0.0-beta.1'
            partial(release,'m5stickc-plus2','2.0.0-beta.1')
            out=root/'kit'
            with staged_directory(out) as staged:metadata=build_kit(release,staged,['m5stickc-plus2'])
            self.assertFalse(metadata['automatic_install'])
            self.assertFalse(metadata['boards']['m5stickc-plus2']['signed'])
            self.assertTrue((out/'src/update_key.h').is_file())
            self.assertTrue((out/'tools/monitor_network.py').is_file())
            self.assertTrue((out/'tools/reset_serial.py').is_file())
            self.assertTrue((out/'docs/v2/hardware-validation.fr.md').is_file())
            for line in (out/'SHA256SUMS.txt').read_text().splitlines():
                digest,name=line.split('  ')
                self.assertEqual(hashlib.sha256((out/name).read_bytes()).hexdigest(),digest)
            names={p.relative_to(out).as_posix() for p in out.rglob('*') if p.is_file()}
            self.assertFalse(any('__pycache__' in n or n.startswith('keys/') or '.sqlite' in n for n in names))
            # The distributed launchers are runnable after extraction, even from
            # a different working directory. --help must not contact a miner.
            for launcher in ('start-hub.sh','check-miners.sh'):
                result=subprocess.run(['sh',str(out/launcher),'--help'],cwd=root,capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)
            for tool in ('monitor_network.py','reset_serial.py'):
                result=subprocess.run([sys.executable,str(out/'tools'/tool),'--help'],
                                      cwd=root,capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr)
            archive=root/'kit.zip';archive_kit(out,archive)
            first=archive.read_bytes();archive_kit(out,archive);self.assertEqual(archive.read_bytes(),first)
            with zipfile.ZipFile(archive) as zipped:
                self.assertIsNone(zipped.testzip());self.assertEqual(set(zipped.namelist()),names)
                self.assertTrue(zipped.getinfo('start-hub.sh').external_attr>>16&0o111)
            (release/'m5stickc-plus2-firmware.bin').write_bytes(b'broken')
            with self.assertRaisesRegex(ValueError,'checksum'):
                with staged_directory(out) as staged:build_kit(release,staged,['m5stickc-plus2'])
            self.assertEqual((out/'kit.json').read_text(),json.dumps(metadata,indent=2)+'\n')

    def test_forged_checksums_cannot_hide_a_different_factory_application(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);release=root/'2.0.0-beta.1'
            partial(release,'m5stickc-plus2','2.0.0-beta.1')
            factory=release/'m5stickc-plus2-factory.bin';data=bytearray(factory.read_bytes())
            data[0x10000]^=1;factory.write_bytes(data);make_release.write_checksums(str(release))
            with self.assertRaisesRegex(ValueError,'verified application'):
                with staged_directory(root/'kit') as staging:build_kit(release,staging,['m5stickc-plus2'])


if __name__=='__main__':unittest.main()
