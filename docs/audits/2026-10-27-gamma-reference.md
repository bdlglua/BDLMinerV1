# Gamma 601 comparison with ESP-Miner

Reference: [ESP-Miner revision bb4fd0c7e589858bbeef00ec89342216c1510671](https://github.com/bitaxeorg/ESP-Miner/tree/bb4fd0c7e589858bbeef00ec89342216c1510671), the upstream master observed during this review. This is a focused comparison of BDLMiner's native Gamma 601 integration, not a full audit of AxeOS or every supported Bitaxe model. The published v1.6.0 tag is unchanged; the fixes below are subsequent source changes.

## Confirmed configuration defect

`main/power/TPS546.h` defines the six VOUT over/undervoltage and margin settings as multipliers of `VOUT_COMMAND`. `TPS546_write_vout_limit_ratios()` in `main/power/TPS546.c` encodes those ratios directly in ULINEAR16. BDLMiner v1.6.0 incorrectly multiplied them by the 1.15 V command before encoding them.

| Register | Correct ratio | Incorrect v1.6.0 value |
|---|---:|---:|
| 0x40 OV fault | 1.25 | 1.4375 |
| 0x42 OV warning | 1.16 | 1.334 |
| 0x25 high margin | 1.10 | 1.265 |
| 0x26 low margin | 0.90 | 1.035 |
| 0x43 UV warning | 0.90 | 1.035 |
| 0x44 UV fault | 0.75 | 0.8625 |

This misprograms the hardware thresholds; notably, the UV warning ratio becomes greater than 1, potentially producing a regulator warning at nominal core voltage and stopping startup. The correction writes the six ratios directly. Command/minimum/maximum remain absolute voltages; the stock maximum stays 525 MHz / 1150 mV. A regression with independent expected register words fails on the original implementation and passes after correction. These are source/register-semantic findings; no real board was used to observe the fault.

## Startup and frequency differences corrected

`components/asic/frequency_transition_bmXX.c` uses 6.25 MHz steps separated by 100 ms. The initial BDLMiner integration used 25 MHz steps separated by 10 ms. The new ramp follows the reference cadence, refreshes the requested ceiling during ramping, and keeps the existing safety lease active.

Because this ramp lasts several seconds, startup now samples fan RPM, input/core voltage, current and regulator temperature at least every 100 ms during waits after a 250 ms power-good grace period. The initial power-stabilization wait is 500 ms, consistent with `main/tasks/power_management_task.c`. The external diode is required after the ASIC mux has been configured and given conversion time. Overtemperature, failed fan/sensor, stale job and lost permission stop the ramp and assert reset before cutting power. Software tests inject these faults during the actual startup path.

Periodic ASIC counter polling now uses a single-chip read (0x42), consistent with `BM1370_read_registers()`; identity discovery remains broadcast (0x52).

## Confirmed matching behavior

- Board 601 configuration: one BM1370, 525 MHz / 1150 mV, EMC2101 ideality 0x24/beta 0, TPS546 and the original board pin map. UART TX17/RX18 and reset GPIO1 match the hardware references.
- BM1370 initialization registers, packet sizes, CRC conventions, TX slot shift 3 / RX small-core shift 4 and header word ordering match `bm1370.c`, `bm_job_packet.c` and their tests.
- EMC2101 external diode format, fan setting 63 at 100%, and RPM conversion match `main/thermal/EMC2101.c`. Sensor error sentinels remain faults in BDLMiner.
- The ASIC **0x8c cumulative counter uses 2^32 hashes per unit**, confirmed by `components/asic/bm_hashrate.c`. The separate 2^20 constant in `hashrate_monitor_task.c` applies to the direct hashrate register, not this cumulative counter; the existing BDLMiner 64-bit conversion is retained.
- The ASIC minimum difficulty exponent is 4 (difficulty 16). Pool target checks and independent returned-nonce verification remain enabled.

AxeOS additionally provides automatic fan regulation, ASIC hardware-error/domain diagnostics and broader device support. Those are optional future features rather than evidence that the conservative fixed-fan Gamma target is validated on hardware.

## Validation limits

Host tests verify relative-register words, full startup sequencing, 77 PLL writes from 50 to 525 MHz with >=100 ms gaps, single-chip counter reads, power-limit changes, thermal/fan faults during ramping, lost permission and stale work. They execute production code with simulated peripherals. Gamma 601 and existing ESP32 firmware builds and the native regression suite are checked separately. Actual regulator acceptance, PLL stability, temperature/fan behavior, accepted shares and sustained hashrate still require a physical Gamma 601.
