#!/usr/bin/env python3
"""Cross-check production Noise against an independent Python responder.

SHA/HMAC/HKDF and wire sequencing use Python's standard library. Curve and
AEAD operations use trusted libsecp256k1/libsodium, not the client's state code.
No public pool, account, hardware or network access is used.
"""
from pathlib import Path
import ctypes as c
import ctypes.util
import hashlib
import hmac
import json
import os
import struct
import subprocess

root = Path(__file__).resolve().parents[2]
build = root / '.pio/host/sv2'
build.mkdir(parents=True, exist_ok=True)
pio = Path(os.environ.get('PLATFORMIO_CORE_DIR', Path.home() / '.platformio'))
sodium_headers = pio / 'packages/framework-arduinoespressif32/tools/sdk/esp32/include/libsodium/libsodium/src/libsodium/include'
if not sodium_headers.exists():
    subprocess.run(['pio', 'pkg', 'install', '-e', 'esp32-headless'], cwd=root, check=True, stdout=subprocess.DEVNULL)
library = ctypes.util.find_library('sodium')
if not library:
    raise SystemExit('Native SV2 tests require libsodium (Ubuntu: sudo apt-get install libsodium23).')
secp = root / 'lib/secp256k1'
flags = json.loads((secp / 'library.json').read_text())['build']['flags']
objects = []
for name in ('secp256k1', 'precomputed_ecmult', 'precomputed_ecmult_gen'):
    obj = build / (name + '.o'); objects.append(str(obj))
    source = secp / 'src' / (name + '.c')
    if not obj.exists() or max(source.stat().st_mtime, (secp / 'library.json').stat().st_mtime) > obj.stat().st_mtime:
        subprocess.run(['gcc', '-O2', '-fPIC', '-I' + str(secp / 'include'), *flags, '-c', str(source), '-o', str(obj)], check=True)
common = ['-Ilib/monocypher/src', '-I' + str(secp / 'include'), '-I' + str(sodium_headers), '-Isrc']
subprocess.run(['gcc', '-shared', '-fPIC', '-O2', *common, 'test/host/sv2_curve_test.c', *objects, '-o', str(build / 'curve.so')], cwd=root, check=True)
mono = build / 'monocypher.o'
subprocess.run(['gcc','-O2','-fPIC','-c',str(root/'lib/monocypher/src/monocypher.c'),'-o',str(mono)],check=True)
objects.append(str(mono))
driver = build / 'crypto-driver'
subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', *common, 'test/host/sv2_crypto_driver.cpp', 'src/core/sv2_noise.cpp', 'src/core/sv2_key.cpp',
                'src/core/work.cpp', 'src/core/sha256.cpp', *objects, '-l:' + library, '-o', str(driver)], cwd=root, check=True)
curve = c.CDLL(str(build / 'curve.so')); sodium = c.CDLL(library)
curve.curve_ell.argtypes = [c.c_char_p, c.c_void_p]
curve.curve_xonly.argtypes = [c.c_char_p, c.c_void_p]
curve.curve_sign.argtypes = [c.c_char_p, c.c_char_p, c.c_void_p]
curve.curve_dh.argtypes = [c.c_char_p, c.c_char_p, c.c_char_p, c.c_int, c.c_void_p]
sodium.crypto_aead_chacha20poly1305_ietf_encrypt.argtypes = [c.c_void_p, c.c_void_p, c.c_char_p, c.c_ulonglong, c.c_char_p, c.c_ulonglong, c.c_void_p, c.c_char_p, c.c_char_p]
sodium.crypto_aead_chacha20poly1305_ietf_decrypt.argtypes = [c.c_void_p, c.c_void_p, c.c_void_p, c.c_char_p, c.c_ulonglong, c.c_char_p, c.c_ulonglong, c.c_char_p, c.c_char_p]

def primitive(name, size, *args):
    out = c.create_string_buffer(size)
    assert getattr(curve, name)(*args, out) == 1
    return out.raw

def aead(key, counter, message, aad=b'', decrypt=False):
    out = c.create_string_buffer(len(message) + 16)
    nonce = bytes(4) + struct.pack('<Q', counter)
    if decrypt:
        ret = sodium.crypto_aead_chacha20poly1305_ietf_decrypt(out, None, None, message, len(message), aad, len(aad), nonce, key)
        assert ret == 0
        return out.raw[:len(message) - 16]
    assert sodium.crypto_aead_chacha20poly1305_ietf_encrypt(out, None, message, len(message), aad, len(aad), None, nonce, key) == 0
    return out.raw[:len(message) + 16]

def sha(data): return hashlib.sha256(data).digest()
def hkdf(ck, data):
    key = hmac.digest(ck, data, 'sha256')
    one = hmac.digest(key, b'\x01', 'sha256')
    return one, hmac.digest(key, one + b'\x02', 'sha256')

authority_secret = (1).to_bytes(32, 'big')
authority = primitive('curve_xonly', 32, authority_secret)
now = 1800000000

def respond(hello, *, version=0, start=now-60, end=now+60, signer=authority_secret, damage=None):
    ephemeral_secret = (3).to_bytes(32, 'big'); static_secret = (2).to_bytes(32, 'big')
    ephemeral = primitive('curve_ell', 64, ephemeral_secret)
    static = primitive('curve_ell', 64, static_secret)
    h = ck = sha(b'Noise_NX_Secp256k1+EllSwift_ChaChaPoly_SHA256')
    h = sha(h); h = sha(h + hello); h = sha(h); h = sha(h + ephemeral)
    shared = primitive('curve_dh', 32, ephemeral_secret, hello, ephemeral, 1)
    ck, key = hkdf(ck, shared)
    encrypted_static = aead(key, 0, static, h); h = sha(h + encrypted_static)
    shared = primitive('curve_dh', 32, static_secret, hello, static, 1)
    ck, key = hkdf(ck, shared)
    fields = struct.pack('<HII', version, start, end)
    cert_hash = sha(fields + primitive('curve_xonly', 32, static_secret))
    cert = fields + primitive('curve_sign', 64, signer, cert_hash)
    response = bytearray(ephemeral + encrypted_static + aead(key, 0, cert, h))
    if damage is not None: response[damage] ^= 1
    return bytes(response), hkdf(ck, b'')

def connect(**options):
    proc = subprocess.Popen([str(driver), authority.hex(), str(now)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    hello = bytes.fromhex(proc.stdout.readline().strip())
    response, keys = respond(hello, **options)
    proc.stdin.write(response.hex() + '\n'); proc.stdin.flush()
    result = proc.stdout.readline().strip()
    return proc, result, keys

def command(proc, kind, data):
    proc.stdin.write(kind + data.hex() + '\n'); proc.stdin.flush()
    return proc.stdout.readline().strip()

def close(proc):
    proc.stdin.close(); assert proc.wait(timeout=5) == 0

proc, result, (client_tx, client_rx) = connect(); assert result == 'READY', result
for counter, payload in enumerate((b'header', b'payload', bytes(65519))):
    encrypted = bytes.fromhex(command(proc, 'E', payload))
    assert aead(client_tx, counter, encrypted, decrypt=True) == payload
    server = aead(client_rx, counter, payload)
    assert bytes.fromhex(command(proc, 'D', server)) == payload
assert command(proc, 'D', server) == 'ERROR' # replay uses the wrong transport nonce
close(proc)
for case in ({'version':1}, {'start':now+1}, {'end':now-1}, {'start':now+2,'end':now+1},
             {'signer':(4).to_bytes(32,'big')}, {'damage':70}, {'damage':150}, {'damage':233}):
    proc, result, _ = connect(**case)
    assert result.startswith('ERROR '), (case, result)
    close(proc)
proc, result, _ = connect(); assert result == 'READY'
assert command(proc, 'E', bytes(65520)) == 'ERROR'; close(proc)
vector = '9bXiEd8boQVhq7WddEcERUL5tyyJVFYdU8th3HfbNXK3Yw6GRXh'
expected = bytes([118,99,112,0,151,156,28,17,175,12,48,11,205,140,127,228,134,16,252,233,185,193,30,61,174,227,90,224,176,138,116,85]).hex()
def decode(key): return subprocess.check_output([str(driver),key,str(now),'decode'],text=True).strip()
assert decode(vector) == expected
assert decode(vector[:-1] + ('1' if vector[-1] != '1' else '2')) == 'INVALID_KEY'
assert decode('') == 'INVALID_KEY'
assert len(decode('9c4zpyJ2ndm4e8sP2uNc1VNCGxYjqaxWS6wUCjk8zFj6njFquH6')) == 64
print('SV2 Noise: independent responder, EllSwift ECDH, BIP340 authority, certificate time/version, AEAD, replay rejection, chunk bounds and official authority-key vector passed')

protocol = build / 'protocol-test'
subprocess.run(['g++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Isrc',
                'test/host/test_sv2_protocol.cpp','src/core/sv2_protocol.cpp','src/core/work.cpp','src/core/sha256.cpp',
                '-o',str(protocol)],cwd=root,check=True)
subprocess.run([str(protocol)],check=True)
header, submission = subprocess.check_output([str(protocol),'export'],text=True).splitlines()
header, submission = bytes.fromhex(header), bytes.fromhex(submission)
coinbase = bytes.fromhex('0102aabb000000000000000a03')
merkle = sha(sha(coinbase));merkle = sha(sha(merkle + bytes([0x11])*32))
expected_header = struct.pack('<I',0x20000000) + bytes([0x22])*32 + merkle + struct.pack('<III',1800000000,0x1d00ffff,0)
assert header == expected_header
channel_id, sequence, job_id, nonce, ntime, version = struct.unpack('<6I',submission[:24])
assert (channel_id,sequence,job_id,nonce,ntime,version)==(7,0,42,123,1800000000,0x20000000)
assert submission[24]==8 and submission[25:]==bytes.fromhex('000000000000000a')
print('SV2 work: independent coinbase/merkle/header and SubmitSharesExtended bytes verified with Python SHA-256d')

# Run the actual embedded session/transport against an independent socket peer.
# The fake WiFiClient intentionally reads <=7 and writes <=3 bytes per call.
import socket
import time
session_driver=build/'session-driver'
subprocess.run(['g++','-std=c++17','-O2','-Wall','-Itest/host/stubs/sv2',*common,
                'test/host/sv2_session_driver.cpp','src/net_sv2.cpp','src/core/sv2_noise.cpp', 'src/core/sv2_key.cpp','src/core/sv2_protocol.cpp',
                'src/core/work.cpp','src/core/sha256.cpp',*objects,'-l:'+library,'-o',str(session_driver)],cwd=root,check=True)

class Peer:
    def __init__(self,**options):
        self.sock,child=socket.socketpair();self.sock.settimeout(10)
        self.proc=subprocess.Popen([str(session_driver),str(child.fileno()),authority.hex()],pass_fds=[child.fileno()],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        child.close();hello=self.read(64)
        current=int(time.time());response,(self.rx_key,self.tx_key)=respond(hello,start=current-60,end=current+60,**options)
        # Fragment the handshake as well as all subsequent frames.
        for offset in range(0,len(response),11):self.sock.sendall(response[offset:offset+11])
        self.rx=self.tx=0
    def read(self,size):
        out=b''
        while len(out)<size:
            chunk=self.sock.recv(size-len(out))
            assert chunk,'unexpected peer disconnect'
            out+=chunk
        return out
    def receive(self):
        header=aead(self.rx_key,self.rx,self.read(22),decrypt=True);self.rx+=1
        extension,kind=struct.unpack('<HB',header[:3]);size=int.from_bytes(header[3:],'little')
        payload=aead(self.rx_key,self.rx,self.read(size+16),decrypt=True) if size else b''
        if size:self.rx+=1
        return extension,kind,payload
    def send(self,kind,payload,channel=False,corrupt=False):
        header=struct.pack('<HB',0x8000 if channel else 0,kind)+len(payload).to_bytes(3,'little')
        data=aead(self.tx_key,self.tx,header);self.tx+=1
        if payload:data+=aead(self.tx_key,self.tx,payload);self.tx+=1
        if corrupt:data=data[:-1]+bytes([data[-1]^1])
        for offset in range(0,len(data),13):self.sock.sendall(data[offset:offset+13])
    def finish(self):
        self.sock.close();out,err=self.proc.communicate(timeout=10)
        assert self.proc.returncode==0,(out,err)
        return out.strip().split(' ',5)

peer=Peer();extension,kind,payload=peer.receive();assert (extension,kind)==(0,0)
assert b'ManualMiner/test' in payload
peer.send(1,struct.pack('<HI',2,0));extension,kind,payload=peer.receive();assert kind==0x13
assert payload[4]==14 and payload[5:19]==b'address.worker'
share_target=bytes([255])*30+bytes(2) # a genuine 16-leading-zero-bit proof, not an all-accepting target
peer.send(0x14,struct.pack('<II',1,7)+share_target+struct.pack('<H',8)+b'\x02\xaa\xbb'+struct.pack('<I',11))
peer.send(0x1f,struct.pack('<IIBIBB',11,42,0,0x20000000,0,1)+bytes([0x11])*32+struct.pack('<H',2)+b'\x01\x02'+struct.pack('<H',1)+b'\x03',channel=True)
current=int(time.time())
peer.send(0x20,struct.pack('<II',11,42)+bytes([0x22])*32+struct.pack('<II',current,0x1d00ffff),channel=True)
seen=[]
for seq in (0,1):
    extension,kind,payload=peer.receive();assert (extension,kind)==(0x8000,0x1b)
    channel_id,sequence,job_id,nonce,ntime,version=struct.unpack('<6I',payload[:24])
    assert (channel_id,sequence,job_id,ntime,version)==(7,seq,42,current,0x20000000)
    extra=payload[25:];assert payload[24]==len(extra)==8;seen.append(extra)
    # Reconstruct a pool-verifiable proof from exactly the submitted extranonce.
    merkle=sha(sha(b'\x01\x02\xaa\xbb'+extra+b'\x03'));merkle=sha(sha(merkle+bytes([0x11])*32))
    candidate=struct.pack('<I',version)+bytes([0x22])*32+merkle+struct.pack('<III',ntime,0x1d00ffff,nonce)
    assert len(candidate)==80 and int.from_bytes(sha(sha(candidate)),'little')<=int.from_bytes(share_target,'little')
assert seen[0]!=seen[1]
rejection=struct.pack('<II',7,0)+b'\x03bad'
acknowledgment=struct.pack('<IIIQ',7,1,1,1)
peer.send(0x1d,rejection,channel=True);peer.send(0x1c,acknowledgment,channel=True)
peer.send(0x1d,rejection,channel=True);peer.send(0x1c,acknowledgment,channel=True)
# Closing the write side leaves queued frames readable before EOF.
result=peer.finish();assert result[:5]==['1','1','1','1','0'],result

peer=Peer(signer=(4).to_bytes(32,'big'))
assert peer.sock.recv(1)==b''
result=peer.finish();assert result[:4]==['0','0','0','0'] and 'verification failed' in result[-1],result
peer=Peer();peer.receive();peer.send(1,struct.pack('<HI',2,0),corrupt=True)
assert peer.sock.recv(1)==b''
result=peer.finish();assert result[:4]==['0','0','0','0'],result
peer=Peer();peer.receive();reason=b'unsupported-user-agent';peer.send(2,struct.pack('<I',0)+bytes([len(reason)])+reason)
assert peer.sock.recv(1)==b''
result=peer.finish();assert result[:4]==['0','0','0','0'] and 'protocol refused' in result[-1],result
print('Production SV2 session: fragmented TCP, partial writes, Noise, setup, channel, unique mining work, submitted shares, batch/reject accounting and no unverified/unauthenticated work passed')
