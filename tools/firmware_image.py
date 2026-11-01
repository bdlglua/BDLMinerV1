"""Independent ESP application/factory integrity checks shared by release and LAN tools."""
import hashlib
from pathlib import Path
import re
import struct
import subprocess
import tempfile

CHIPS = {'esp32': 0, 'esp32s2': 2, 'esp32s3': 9, 'esp32c3': 5}
ENV_CHIPS = {**dict.fromkeys(('m5stickc-plus2', 'm5stack-esp32', 'lilygo-t-display',
                             'esp32-2432s028r', 'esp32-headless'), 'esp32'),
             **dict.fromkeys(('m5stack-s3', 'lilygo-t-display-s3', 'esp32s3-headless',
                             'bitaxe-gamma-601', 'nerdnos'), 'esp32s3'),
             'esp32s2-headless': 'esp32s2', 'esp32c3-headless': 'esp32c3'}


def inspect_app(data, env, version, chip=None):
    if env not in ENV_CHIPS or chip is not None and chip != ENV_CHIPS[env]:
        raise ValueError('Unknown firmware target or inconsistent board chip')
    chip = ENV_CHIPS[env]
    if (len(data) < 64 or len(data) > 8 * 1024 * 1024 or data[0] != 0xe9 or
            not 1 <= data[1] <= 16 or data[23] != 1 or struct.unpack_from('<I', data, 32)[0] != 0xabcd5432):
        raise ValueError('Invalid ESP application header')
    actual_chip = struct.unpack_from('<H', data, 12)[0]
    if actual_chip not in CHIPS.values() or chip and actual_chip != CHIPS[chip]:
        raise ValueError('Firmware chip mismatch')
    at, checksum = 24, 0xef
    for _ in range(data[1]):
        if at + 8 > len(data):
            raise ValueError('Truncated ESP segment')
        length = struct.unpack_from('<I', data, at + 4)[0]
        at += 8
        if length > len(data) - at:
            raise ValueError('Truncated ESP segment data')
        for value in data[at:at + length]:
            checksum ^= value
        at += length
    checksum_at = at | 15
    if (len(data) != checksum_at + 33 or data[checksum_at] != checksum or
            data[checksum_at + 1:] != hashlib.sha256(data[:checksum_at + 1]).digest()):
        raise ValueError('Firmware checksum/digest mismatch')
    for marker in (env.encode() + b'\0', ('BDLMinerV1/' + version).encode() + b'\0'):
        if marker not in data:
            raise ValueError('Firmware target/version marker mismatch')
    return actual_chip


def inspect_factory(factory, firmware, env, version, chip):
    inspect_app(firmware, env, version, chip)
    if len(factory) < 0x10000 + len(firmware) or factory[0x10000:0x10000 + len(firmware)] != firmware:
        raise ValueError('Factory image does not contain the verified application')
    slots = []
    for at in range(0x8000, min(0x9000, len(factory)) - 31, 32):
        magic, kind, subtype, start, size = struct.unpack_from('<HBBII', factory, at)
        if magic != 0x50aa:
            break
        if kind == 0 and 0x10 <= subtype <= 0x1f:
            slots.append((start, size))
    if len(slots) != 2 or slots[0][0] != 0x10000 or min(size for _, size in slots) < len(firmware):
        raise ValueError('Firmware does not fit two valid OTA slots')
    if slots[0][0] + slots[0][1] > slots[1][0]:
        raise ValueError('OTA slots overlap')
    return min(size for _, size in slots)


def verify_signature(firmware, signature):
    header = (Path(__file__).resolve().parents[1] / 'src/update_key.h').read_text()
    public = '\n'.join(re.findall(r'"([^"\n]*)\\n"', header)) + '\n'
    with tempfile.TemporaryDirectory(prefix='bdl-public-') as tmp:
        key = Path(tmp) / 'public.pem'
        key.write_text(public)
        checked = subprocess.run(['openssl', 'dgst', '-sha256', '-verify', str(key),
                                  '-signature', str(signature), str(firmware)], capture_output=True)
    if checked.returncode:
        raise ValueError('Firmware release signature verification failed')
