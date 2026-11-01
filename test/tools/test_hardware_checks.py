"""False success must be impossible for reset counters or unobserved trials."""
import importlib.util
import unittest
from unittest import mock
import json
import tempfile
from pathlib import Path
spec = importlib.util.spec_from_file_location('hardware_checks', Path(__file__).resolve().parents[2] / 'tools/check_hardware.py')
checks = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checks)


class HardwareChecks(unittest.TestCase):
    def test_measured_hashes_and_reset_detection(self):
        first = {'board':'test','firmware':'1','boot_id':'a','uptime_s':100,'total_hashes':100000000,'hashrate':999999,'temp_c':40}
        last = dict(first, uptime_s=280, total_hashes=280000000, temp_c=45)
        self.assertEqual(checks.summarize([first,last])['measured_hashrate_hs'],1000000)
        for bad in (dict(last,boot_id='b'),dict(last,total_hashes=0),dict(last,firmware='2'),dict(last,uptime_s=90)):
            with self.assertRaises(ValueError): checks.summarize([first,bad])
        with self.assertRaises(ValueError): checks.summarize([first,first])
        with self.assertRaises(ValueError): checks.summarize([first,last,dict(last,uptime_s=200)])

    def test_real_trial_must_be_observed(self):
        current={'firmware':'1.9.14','firmware_trial':False}
        self.assertIsNone(checks.ota_result(current,'1.9.14','1.9.13','confirm',False))
        self.assertIsNone(checks.ota_result(current,'1.9.14','1.9.13','confirm',True,False))
        self.assertEqual(checks.ota_result(current,'1.9.14','1.9.13','confirm',True),'confirmed')
        self.assertEqual(checks.ota_result(current,'1.9.14','1.9.13','rollback',True),'confirmed_before_rollback')
        old={'firmware':'1.9.13','firmware_trial':False}
        self.assertIsNone(checks.ota_result(old,'1.9.14','1.9.13','rollback',False))
        self.assertEqual(checks.ota_result(old,'1.9.14','1.9.13','rollback',True),'rollback')

    def test_health_and_urls(self):
        sample={'temp_c':75,'asic':{'running':True,'model':'BM1370','fan_rpm':0,'regulator_temp_c':95}}
        self.assertEqual(len(checks.health(sample)),3)
        self.assertEqual(checks.device_url('miner.local'),'http://miner.local')
        for url in ('file:///etc/passwd','http://admin:secret@miner/','http://miner/?password=x'):
            with self.assertRaises(Exception): checks.device_url(url)

    def test_failed_measurement_replaces_an_old_report(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'report.json';path.write_text('{"status":"completed"}')
            with mock.patch('sys.argv',['check','measure','http://miner','--output',str(path)]), mock.patch.object(checks,'measure',side_effect=ValueError('counters reset')):
                self.assertEqual(checks.main(),1)
            self.assertEqual(json.loads(path.read_text())['status'],'failed')


if __name__ == '__main__': unittest.main()
