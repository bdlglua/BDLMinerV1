import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from release_version import Version,select_release

class ReleaseVersions(unittest.TestCase):
    def test_order_and_channel_boundaries(self):
        versions=['2.0.0-alpha','2.0.0-alpha.1','2.0.0-alpha.beta','2.0.0-beta','2.0.0-beta.2','2.0.0-beta.11','2.0.0-rc.1','2.0.0']
        self.assertEqual(sorted(versions,key=Version),versions)
        self.assertEqual(Version('1.0'),Version('1.0.0'))
        releases=[{'tag_name':'v1.9.15'},{'tag_name':'v2.0.0-beta.1','prerelease':True},{'tag_name':'v9.0.0','draft':True}]
        self.assertEqual(select_release(releases,'stable')['tag_name'],'v1.9.15')
        self.assertEqual(select_release(releases,'beta')['tag_name'],'v2.0.0-beta.1')
        releases.append({'tag_name':'v2.0.0'})
        self.assertEqual(select_release(releases,'beta')['tag_name'],'v2.0.0')
    def test_invalid_versions_cannot_enter_manifests(self):
        for value in ('2.0.0-beta.01','2.0.0-','2.0.0+','2.0.0.1','2.0.0-beta..1','2.0.0/'+'x'*30):
            with self.subTest(value=value),self.assertRaises(ValueError):Version(value)
