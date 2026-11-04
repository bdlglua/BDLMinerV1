"""The HTTPS trust anchors: generated bundle, lookup, expiry and coverage."""
from pathlib import Path
import datetime
import re
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import make_tls_roots  # noqa: E402


def bundle_bytes():
    text = (ROOT / 'src/tls_roots.h').read_text()
    body = text[text.index('{') + 1:text.rindex('}')]
    return bytes(int(value, 16) for value in re.findall(r'0x([0-9a-f]{2})', body))


def entries(data):
    count, at, out = struct.unpack('>H', data[:2])[0], 2, []
    for _ in range(count):
        name_length, key_length = struct.unpack('>HH', data[at:at + 4])
        name = data[at + 4:at + 4 + name_length]
        out.append((name, data[at + 4 + name_length:at + 4 + name_length + key_length]))
        at += 4 + name_length + key_length
    assert at == len(data), 'trailing bytes in the bundle'
    return out


def lookup(crts, issuer):
    """The binary search of esp_crt_verify_callback (arduino-esp32 2.0.17), byte for byte."""
    start, end = 0, len(crts) - 1
    middle = (end - start) // 2
    while start <= end:
        name = crts[middle][0]
        candidate = issuer[:len(name)]
        if candidate == name:
            return crts[middle][1]
        if candidate < name:
            end = middle - 1
        else:
            start = middle + 1
        middle = (start + end) // 2
    return None


class TlsRoots(unittest.TestCase):
    def setUp(self):
        self.text = (ROOT / 'assets/tls/roots.pem').read_text()
        self.roots = [(label, *make_tls_roots.parse(der)) for label, der in make_tls_roots.certificates(self.text)]

    def test_header_is_generated_from_the_pem(self):
        self.assertEqual((ROOT / 'src/tls_roots.h').read_text(), make_tls_roots.build(self.text),
                         'run python3 tools/make_tls_roots.py')

    def test_every_root_is_found_by_the_firmware_lookup(self):
        crts = entries(bundle_bytes())
        self.assertEqual(len(crts), len(self.roots))
        self.assertEqual([name for name, _ in crts], sorted(name for name, _ in crts))
        for label, name, key, _ in self.roots:
            self.assertEqual(lookup(crts, name), key, label)
        # A name absent from the bundle (a self-signed server) finds nothing.
        self.assertIsNone(lookup(crts, b'\x30\x13\x31\x11\x30\x0f\x06\x03\x55\x04\x03\x0c\x08selfsign'))

    def test_roots_outlive_the_firmware(self):
        soon = datetime.datetime(2027, 12, 31, tzinfo=datetime.timezone.utc)
        for label, _, _, expires in self.roots:
            self.assertGreater(expires, soon, label + ': replace it in assets/tls/roots.pem')

    def test_known_services_are_covered(self):
        labels = {label.split(' (current')[0].split(' (legacy')[0] for label, *_ in self.roots}
        needed = {
            'ntfy.sh, mempool.space, Let\'s Encrypt sites': 'ISRG Root X1',
            'github.com and its API (Sectigo)': 'USERTrust ECC Certification Authority',
            'Sectigo chains cross-signed by Comodo': 'AAA Certificate Services',
            'GitHub release downloads (DigiCert)': 'DigiCert Global Root G2',
            'Discord, Google Trust Services': 'GTS Root R4',
            'GTS chains cross-signed by GlobalSign': 'GlobalSign Root CA',
            'api.telegram.org': 'Go Daddy Root Certificate Authority - G2',
            'Slack and AWS endpoints': 'Amazon Root CA 1',
            'Amazon chains cross-signed by Starfield': 'Starfield Services Root Certificate Authority - G2',
        }
        for service, root in needed.items():
            self.assertIn(root, labels, service)

    def test_bundle_fits_the_flash_budget(self):
        # m5stack-esp32 had 66 KB left in its update slot before the bundle.
        self.assertLess(len(bundle_bytes()), 20 * 1024)

    def test_parser_rejects_damaged_input(self):
        _, der = make_tls_roots.certificates(self.text)[0]
        with self.assertRaises(ValueError):
            make_tls_roots.parse(der[:200])
        with self.assertRaises(ValueError):
            make_tls_roots.bundle([(b'\x30\x00', b'a'), (b'\x30\x00', b'b')])


if __name__ == '__main__':
    unittest.main()
