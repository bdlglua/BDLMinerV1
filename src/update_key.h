// Public key that release firmware is signed with (ECDSA P-256 over the
// SHA-256 of <env>-firmware.bin, written by tools/make_release.py). Updates
// from GitHub are only installed with a valid signature. The private key
// stays out of the repository (keys/, or the RELEASE_SIGNING_KEY secret).
#pragma once

static const char kUpdatePublicKey[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEWv+4oobPCMCgVUBdUYRAhHhmNjsi\n"
    "wfWxmV0O5mdT504aIF3CUZpxqfqV8s8X0o2ONMaV75fRvgx+0guyL4ZYOw==\n"
    "-----END PUBLIC KEY-----\n";
