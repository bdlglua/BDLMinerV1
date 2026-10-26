Monocypher: https://github.com/LoupVaillant/Monocypher

Release `4.0.2`, commit `0d85f98c9d9b0227e42cf795cb527dff372b40a4`.
`src/monocypher.c`, `src/monocypher.h` and `LICENCE.md` are unmodified upstream.
BDLMiner uses the BSD-2-Clause license option. Only ChaCha20-Poly1305 IETF and
secure wiping are linked into the firmware. Each Noise record initializes a
fresh AEAD context with the Noise key and counter nonce, then wipes the context.
