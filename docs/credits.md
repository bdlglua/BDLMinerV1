# Credits

* Native Gamma 601 board configuration, BM1370 register/packet sequences and
  TPS546/EMC2101 references derive from [ESP-Miner / AxeOS](https://github.com/bitaxeorg/ESP-Miner)
  (GPL-3.0), revision `bb4fd0c7e589858bbeef00ec89342216c1510671`, and
  [bitaxeGamma hardware](https://github.com/bitaxeorg/bitaxeGamma). The Gamma's core
  voltage options and the 690 MHz highest preset (since 2.4.2) follow ESP-Miner
  revision `a591f0d` (2026-11-02), and so do the 75 °C ASIC and 105 °C regulator stops
  (since 2.5.3, `THROTTLE_TEMP` and `TPS546_THROTTLE_TEMP` of its power management task).
* NerdNOS support: BM1397 packets, PLL calculation and start-up sequence, and the
  board's pins, derive from ESP-Miner (above) and the NerdNOS firmware in
  [WantClue/NerdMiner_v2](https://github.com/WantClue/NerdMiner_v2/tree/nerdnos)
  (branch `nerdnos`, revision `22588347597d24969f41ed56fad2641678ad59c1`, GPL-3.0); the board is
  [benjamin-wilson/NerdNOS](https://github.com/benjamin-wilson/NerdNOS).

* The earlier bitmap screen design was inspired by
  [NerdMinerV2](https://github.com/BitMaker-hub/NerdMiner_v2).
* The original-ESP32 hardware SHA loop (`src/hw_sha_classic.S`) and the assembly
  software kernel (`src/sw_sha_prefilter.S`) come from
  [NerdMiner Modded](https://github.com/HenrysCat/NerdMiner-Modded) by HenrysCat,
  MIT License, Copyright (c) 2023 Bitmaker, (c) 2026 HenrysCat. The ESP32-S3 overlapped
  transfers in `src/hw_sha.cpp` (since 2.4.2) follow its `PIPELINED_S3_MINING` loop
  (revision `522764d`, V1.6.2): the next header block is written while the engine
  computes, and only its changed words.
* Libraries: [M5Unified / M5GFX](https://github.com/m5stack/M5Unified) (LovyanGFX),
  [WiFiManager](https://github.com/tzapu/WiFiManager),
  [ArduinoJson](https://arduinojson.org/).
* V2 cryptography: [Bitcoin Core libsecp256k1](https://github.com/bitcoin-core/secp256k1)
  (MIT, pinned source in `lib/secp256k1`) and
  [Monocypher](https://github.com/LoupVaillant/Monocypher) (BSD-2-Clause, pinned source
  in `lib/monocypher`). Their upstream notices and exact revisions are retained.
* Stratum V2 protocol: [official specification](https://stratumprotocol.org/).
* Market data: [mempool.space](https://mempool.space) public API.

BDLMiner V2 is released under the [GNU GPL v3.0](../LICENSE).
[Back to the project overview](../README.md).
