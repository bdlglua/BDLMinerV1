#include "gamma601.h"
#ifdef BOARD_GAMMA601
#include <Arduino.h>
#include <M5GFX.h>
#include <cmath>
#include <algorithm>
#include <atomic>
#include "core/bm1370.h"
#include "miner.h"
#include "settings.h"
#include "log.h"
#include "runtime.h"

namespace gamma601 {
namespace {
constexpr uint8_t kTps = 0x24, kEmc = 0x4c;
constexpr int kReset = 1;
HardwareSerial uart(1);
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
Status snapshot{NAN, NAN, NAN, NAN, NAN, 0, 0, 0, false, "Starting", 525, false, "", -1};
Status live = snapshot;
bool configured = false, latched = false, powered = false;
bool logSetup = true;  // the setup's failures are logged once, not at every retry
const char kNotFound[] = "Regulator or sensors not found: plug in the 5V supply";
uint8_t voltageMode = 0;
// The board's own temperature (EMC2101 internal sensor), for the thermal policy
// while the ASIC diode has no reading: before the ASIC first runs, or paused.
std::atomic<float> boardTemperature{NAN};
uint32_t powerEnabledAt = 0, lastHealthSample = 0;
bm1370::Receiver receiver;
bm1370::Counter counter;
bm1370::ErrorMonitor errors;
bool forceStock = false, errorSeen = false;
uint32_t errorValue = 0, lastErrorReply = 0;
float targetFrequency() {
    unsigned requested = bm1370::validProfile(settings.asicFrequency) ? settings.asicFrequency : bm1370::kStockMHz;
    return std::max(50.0f, float(forceStock ? bm1370::kStockMHz : requested)*minerPowerPercent()/100);
}
struct Slot {
    AsicWork work;
    uint32_t sent = 0;
    bool valid = false;
    // Suppress duplicate UART results, without conflating two rolled versions.
    uint64_t proofs[32]{};
    uint8_t proofHead = 0, proofCount = 0;
};
Slot slots[bm1370::kSlots];
uint8_t nextSlot = 0;
uint32_t activeGeneration = 0, activeMask = 0, lastHashReply = 0, lastHashProgress = 0;

void publish() { portENTER_CRITICAL(&mux); snapshot = live; portEXIT_CRITICAL(&mux); }
bool read(uint8_t addr, uint8_t reg, uint8_t *out, size_t size) {
    return lgfx::i2c::transactionWriteRead(0, addr, &reg, 1, out, size, 400000).has_value();
}
bool write8(uint8_t addr, uint8_t reg, uint8_t value) {
    uint8_t bytes[]{reg, value};
    return lgfx::i2c::transactionWrite(0, addr, bytes, sizeof(bytes), 400000).has_value();
}
bool write16(uint8_t reg, uint16_t value) {
    uint8_t bytes[]{reg, uint8_t(value), uint8_t(value >> 8)};
    return lgfx::i2c::transactionWrite(0, kTps, bytes, sizeof(bytes), 400000).has_value();
}
bool read16(uint8_t addr, uint8_t reg, uint16_t &value) {
    uint8_t bytes[2]; if (!read(addr, reg, bytes, 2)) return false;
    value = uint16_t(bytes[0]) | uint16_t(bytes[1]) << 8; return true;
}
// Every setting is read back; one that does not hold stops the setup, and
// the log says which.
bool unset(uint8_t addr, uint8_t reg, const char *why) {
    if (logSetup) logf("[gamma] device 0x%02x register 0x%02x: %s\n", addr, reg, why);
    return false;
}
bool verify8(uint8_t addr, uint8_t reg, uint8_t value) {
    uint8_t actual;
    if (!write8(addr, reg, value) || !read(addr, reg, &actual, 1)) return unset(addr, reg, "no answer");
    if (actual != value) return unset(addr, reg, "reads back another value");
    return true;
}
bool verify16(uint8_t reg, uint16_t value) {
    uint16_t actual;
    if (!write16(reg, value) || !read16(kTps, reg, actual)) return unset(kTps, reg, "no answer");
    if (actual != value) return unset(kTps, reg, "reads back another value");
    return true;
}
// A number may come back in the regulator's own format (another exponent),
// or rounded to its resolution (VIN_ON: 0.25 V steps): compared as values.
bool linearWord(uint8_t reg, uint16_t word, float value, float tolerance = NAN) {
    uint16_t actual;
    if (!write16(reg, word) || !read16(kTps, reg, actual)) return unset(kTps, reg, "no answer");
    if (!std::isfinite(tolerance)) tolerance = std::max(0.01f, std::fabs(value) * 0.03f);
    if (std::fabs(bm1370::linear11(actual) - value) > tolerance) return unset(kTps, reg, "reads back another value");
    return true;
}
bool linear(uint8_t reg, float value, float tolerance = NAN) {
    return linearWord(reg, bm1370::encodeLinear11(value), value, tolerance);
}
// Temperatures and timings: whole numbers, written as ESP-Miner does.
bool linearInt(uint8_t reg, unsigned value) { return linearWord(reg, bm1370::encodeLinear11Int(value), value); }
bool voltage(uint8_t reg, float value, float tolerance = 0.004f) {
    uint16_t word, actual;
    if (!bm1370::voltageWord(value, voltageMode, word)) return unset(kTps, reg, "unsupported VOUT_MODE");
    if (!write16(reg, word) || !read16(kTps, reg, actual)) return unset(kTps, reg, "no answer");
    int e = voltageMode & 31; if (e & 16) e -= 32;
    if (std::fabs(std::ldexp(float(actual) - float(word), e)) > tolerance)
        return unset(kTps, reg, "reads back another value");
    return true;
}
constexpr float kVout = 1.15f;
// The margin and OV/UV limits, as a fraction of VOUT_COMMAND. The TPS546
// keeps them relative by default (VOUT_MODE bit 7, as on the Bitaxe), else
// in volts. Its hardware rounds them to steps of 1 to 10% (the OV fault
// limit: 10% while the output is off): compared within 6%.
bool limit(uint8_t reg, float fraction) {
    bool relative = voltageMode & 0x80;
    return relative ? voltage(reg, fraction, 0.06f) : voltage(reg, fraction * kVout, 0.06f * kVout);
}
// PIN_DETECT_OVERRIDE (EEh) only matters when the regulator restores itself at
// power-on; written as ESP-Miner does (FFFFh). Some of its bits are reserved:
// a different read-back is logged, not a failure.
bool pinDetectOverride() {
    uint16_t actual;
    if (!write16(0xee, 0xffff) || !read16(kTps, 0xee, actual)) return unset(kTps, 0xee, "no answer");
    if (actual != 0xffff && logSetup) logf("[gamma] PIN_DETECT_OVERRIDE reads 0x%04x\n", actual);
    return true;
}
bool clearFaults() {
    uint8_t clear = 0x03;
    return lgfx::i2c::transactionWrite(0, kTps, &clear, 1, 400000).has_value();
}
void invalidate() {
    for (auto &slot : slots) slot.valid = false;
    receiver.clear(); counter.reset(); errors.reset(); errorSeen=false; nextSlot = 0;
}
void off(const char *reason, bool fault = false) {
    // Assert reset first: even a broken I2C bus must stop the ASIC immediately.
    digitalWrite(kReset, LOW);
    bool disabled = write8(kTps, 0x01, 0);
    bool fan = write8(kEmc, 0x4c, 63);
    powered = false; live.running = false; live.frequency = 0;
    if (fault || !disabled || !fan) latched = true;
    live.state = !disabled || !fan ? "I2C shutdown fault; restart required" : reason;
    invalidate(); publish();
}
void instability(const char *reason) {
    if (!forceStock && live.frequency > bm1370::kStockMHz) {
        logf("[gamma] %s at %.2f MHz; falling back to 525 MHz\n", reason, live.frequency);
        forceStock=true; live.stockFallback=true; live.fallbackReason=reason;
        off("Restarting at 525 MHz (fallback)");
    } else off(reason, true);
}
void registerResult(const bm1370::Response &r) {
    if (!powered || r.address) return;
    uint32_t now=millis();
    if (r.reg==0x4c) { errorValue=r.value; lastErrorReply=now; errorSeen=true; return; }
    if (r.reg!=0x8c) return;
    uint64_t delta=counter.update(r.value,now);
    minerAsicHashes(delta); lastHashReply=now;
    if (delta) lastHashProgress=now;
    if (errorSeen && now-lastErrorReply<=2000) {
        bool unstable=errors.update(errorValue,r.value,now);
        live.hardwareErrorPercent=errors.percent; publish();
        if (unstable && live.frequency>bm1370::kStockMHz) instability("High ASIC hardware error rate");
    } else { errors.reset(); live.hardwareErrorPercent=-1; }
}
void checkWatchdogs(uint32_t now) {
    if (!powered || !live.running) return;
    if (now-lastHashReply>3000) instability("ASIC not responding; restart required");
    else if (now-lastHashProgress>5000) instability("ASIC counter stalled; restart required");
    else if (live.frequency>bm1370::kStockMHz && now-lastErrorReply>5000)
        instability("ASIC error telemetry unavailable");
}
bool send(const bm1370::Packet &p) {
    return p.size && uart.write(p.bytes.data(), p.size) == p.size;
}
bool reg(uint8_t address, uint32_t value, bool all = true) { return send(bm1370::reg(address, value, all)); }
bool request(uint8_t address, bool broadcast = true) {
    uint8_t payload[]{0, address}; return send(bm1370::command(broadcast ? 0x52 : 0x42, payload, 2));
}
bm1370::Health readings() {
    return {live.temperature, live.regulatorTemperature, live.inputVoltage,
            live.coreVoltage, live.current, live.rpm, live.regulatorStatus};
}
bool collect(bool requireDiode) {
    lastHealthSample = millis();
    uint8_t internal, msb, lsb, tachLow, tachHigh; uint16_t vin, vout, current, vr, status;
    bool ok = read(kEmc, 0x00, &internal, 1) && read(kEmc, 0x01, &msb, 1) && read(kEmc, 0x10, &lsb, 1) &&
        read(kEmc, 0x46, &tachLow, 1) && read(kEmc, 0x47, &tachHigh, 1) && read16(kTps, 0x88, vin) && read16(kTps, 0x8b, vout) &&
        read16(kTps, 0x8c, current) && read16(kTps, 0x8d, vr) && read16(kTps, 0x79, status);
    if (!ok) {
        logf("[gamma] sensors not answering on I2C\n");
        live.temperature = NAN; off("Sensor I2C fault; restart required", true); return false;
    }
    int e = voltageMode & 31; if (e & 16) e -= 32;
    live.regulatorStatus = status;
    boardTemperature = internal < 0x80 ? float(internal) : NAN;  // two's complement; below 0 C is not a reading here
    live.temperature = bm1370::diodeTemperature(msb, lsb);
    live.regulatorTemperature = bm1370::linear11(vr);
    live.inputVoltage = bm1370::linear11(vin);
    live.coreVoltage = std::ldexp(float(vout), e);
    live.current = bm1370::linear11(current); live.rpm = bm1370::fanRpm(uint16_t(tachLow) | uint16_t(tachHigh) << 8);
    const char *fault = readings().fault(powered, requireDiode);
    if (fault) {
        // What the first boards will need to tell: every reading at the fault.
        logf("[gamma] %s: regulator status 0x%04x, input %.2f V, core %.3f V, %.1f A, ASIC %.1f C, "
             "regulator %.1f C, fan %u rpm\n", fault, live.regulatorStatus, live.inputVoltage, live.coreVoltage,
             live.current, live.temperature, live.regulatorTemperature, live.rpm);
        off(fault, true); return false;
    }
    publish(); return true;
}
// All startup waits are short and keep the main-loop/OTA safety lease checked.
bool waitSafe(uint32_t ms, bool requireDiode = false) {
    uint32_t since = millis();
    do {
        uint8_t power = minerPowerPercent();
        if (power < 10 || !minerAsicCurrent(activeGeneration)) {
            off(power && power < 10 ? "Gamma power limit must be 10-100% or paused" : "Paused / waiting for pool");
            return false;
        }
        // Respect TPS546 power-good settling; continue monitoring during long PLL ramps.
        uint32_t now = millis();
        if (powered && now - powerEnabledAt >= 250 && now - lastHealthSample >= 100 && !collect(requireDiode)) return false;
        delay(5);
    } while (millis() - since < ms);
    return true;
}
bool initAsic(const AsicWork &first) {
    activeGeneration = first.generation; activeMask = first.versionMask;
    live.requestedMHz = bm1370::validProfile(settings.asicFrequency) ? settings.asicFrequency : bm1370::kStockMHz;
    live.hardwareErrorPercent=-1; errors.reset(); errorSeen=false;
    if (!collect(false)) return false;
    // On USB alone there is no 5 V for the ASIC and its fan: wait for it.
    if (const char *wait = readings().notReady()) { live.state = wait; publish(); return false; }
    if (minerPowerPercent() < 10 || !minerAsicCurrent(activeGeneration)) return false;
    // An undervoltage latched while the supply was missing must not stop this start.
    if (!clearFaults() || !write8(kTps, 0x01, 0x80)) { off("ASIC power enable failed", true); return false; }
    powered = true; powerEnabledAt = lastHealthSample = millis();
    live.state = "Initializing BM1370"; publish();
    if (!waitSafe(500)) return false;
    digitalWrite(kReset, HIGH);
    if (!waitSafe(100)) return false;
    uart.end(); uart.setRxBufferSize(4096); uart.begin(115200, SERIAL_8N1, 18, 17);
    while (uart.available()) uart.read();
    receiver.clear();
    for (int i = 0; i < 3; ++i) if (!send(bm1370::versionMask(activeMask))) return false;
    if (!request(0)) return false;
    uint32_t since = millis(); int chips = 0; bm1370::Response response;
    do {
        while (uart.available()) if (receiver.push(uart.read(), response) && !response.job && response.reg == 0) {
            if ((response.value >> 16) != 0x1370) return false;
            ++chips;
        }
        if (!waitSafe(5)) return false;
    } while (millis() - since < 1000);
    if (chips != 1) return false;
    uint8_t zero[]{0, 0};
    if (!send(bm1370::versionMask(activeMask)) || !reg(0xa8, 0x00070000) || !reg(0x18, 0xf000c100) ||
        !send(bm1370::command(0x53, zero, 2)) || !send(bm1370::command(0x40, zero, 2))) return false;
    const struct { uint8_t address; uint32_t value; bool all; } setup[]{
        {0x3c,0x80008b00,true},{0x3c,0x80008010,true},{0x14,0x000000ff,true},
        {0x54,3,true},{0x58,0x00011111,true},{0x68,0x5aa55aa5,true},
        {0xa8,0x000701f0,false},{0x18,0xf000c100,false},{0x3c,0x80008b00,false},
        {0x3c,0x80008010,false},{0x3c,0x800082aa,false},{0xb9,0x00004480,true},
        {0x54,2,true},{0xb9,0x00004480,true},{0x3c,0x80008dee,true}
    };
    for (auto &step : setup) if (!reg(step.address, step.value, step.all) || !waitSafe(10)) return false;
    // Minimum supported ASIC difficulty 16: do not lose valid pool shares by using a higher hardware filter.
    if (!send(bm1370::ticketMask(4))) return false;
    if (!reg(0x28, 0x11300200)) return false;
    uart.flush(); uart.updateBaudRate(1000000);
    // Let the external-diode conversion settle after configuring the ASIC mux.
    if (!waitSafe(100) || !collect(true)) return false;
    // Match ESP-Miner's 6.25 MHz / 100 ms steps, with live safety checks.
    float target = targetFrequency();
    for (float f = 50; f < target; f += 6.25f) {
        if (!send(bm1370::frequency(f, &live.frequency)) || !waitSafe(100, true)) return false;
        target = targetFrequency();
    }
    if (!send(bm1370::frequency(target, &live.frequency))) return false;
    uint32_t hcn = uint32_t((4294967296.0 / 128 * 25 / live.frequency * 0.5) - 268);
    if (!reg(0x10, hcn) || !collect(true)) return false;
    counter.reset(); lastHashReply = lastHashProgress = lastErrorReply = millis();
    live.running = true; live.state = forceStock ? "Mining (525 MHz fallback profile)" : "Mining (experimental)";
    logf("[gamma] BM1370 %.2f MHz, requested profile %u MHz%s\n", live.frequency, live.requestedMHz, forceStock ? " (stock fallback)" : "");
    publish(); return true;
}
// The regulator and sensor setup, volatile and verified; false if anything
// does not answer or hold (a regulator without its 5 V supply does not answer).
bool configure() {
    // The regulator is kept off throughout configuration and WiFi provisioning.
    bool disabled = write8(kTps, 0x01, 0);
    uint8_t id, device[7];
    if (!read(kEmc, 0xfd, &id, 1) || (id != 0x16 && id != 0x28)) {
        if (logSetup) logf("[gamma] EMC2101 fan/temperature sensor not found\n");
        return logSetup = false;
    }
    bool emc = verify8(kEmc, 0x4a, 0x23) && verify8(kEmc, 0x4c, 63) &&
        verify8(kEmc, 0x03, 0x04) && verify8(kEmc, 0x17, 0x24) && verify8(kEmc, 0x18, 0);
    const uint8_t prefix[]{6, 0x54, 0x49, 0x54, 0x6d, 0x24};
    if (!emc || !disabled || !read(kTps, 0xad, device, 7) || memcmp(device, prefix, 6) ||
        (device[6] != 0x41 && device[6] != 0x62) || !read(kTps, 0x20, &voltageMode, 1)) {
        if (logSetup && emc) logf("[gamma] TPS546 regulator not found (is the 5V supply plugged in?)\n");
        return logSetup = false;
    }
    // Volatile PMBus writes only, the values of ESP-Miner. Never issue
    // STORE_USER_ALL or any NVM command.
    bool tps = verify8(kTps,0x02,0x1b) && verify16(0xec,0) && verify8(kTps,0xe4,0x10) &&
        verify8(kTps,0x04,0) && linearInt(0x33,650) && linear(0x35,4.8) && linear(0x36,4.5) &&
        linear(0x55,6.5) && verify8(kTps,0x56,0xb7) && linear(0x29,0.25) &&
        voltage(0x24,2.0) && voltage(0x2b,1.0) && voltage(0x21,kVout) &&
        limit(0x40,1.25) && limit(0x42,1.16) && limit(0x25,1.1) &&
        limit(0x26,0.9) && limit(0x43,0.9) && limit(0x44,0.75) &&
        // Current limits: the hardware rounds them up to 2 A steps.
        linear(0x4a,25,2) && linear(0x46,30,2) && verify8(kTps,0x47,0xc0) &&
        linearInt(0x51,105) && linearInt(0x4f,145) && verify8(kTps,0x50,0xff) &&
        linearInt(0x60,0) && linearInt(0x61,3) && linearInt(0x62,0) && verify8(kTps,0x63,0x3b) &&
        // TOFF_FALL (65h) accepts 0.5 to 31.75 ms only: the 0 ESP-Miner writes is
        // refused, so the regulator keeps its own value; left as it is here too.
        linearInt(0x64,0) && pinDetectOverride() && clearFaults();
    if (tps) logf("[gamma] regulator and sensors configured\n");
    return logSetup = tps;
}
void worker(void *) {
    delay(2000); // fan spin-up, ASIC held in reset and unpowered
    uint32_t sampled = millis(), sent = 0, requested = 0, setupAt = millis();
    AsicWork next;
    for (;;) {
        uint32_t now = millis();
        if (latched) { delay(100); continue; }
        // A regulator without its 5 V does not answer: try again until it does.
        if (!configured) {
            if (now - setupAt >= 5000) {
                setupAt = now;
                configured = configure();
                live.state = configured ? "Ready / waiting for pool" : kNotFound; publish();
            }
            delay(100); continue;
        }
        if (minerPowerPercent() > 0 && minerPowerPercent() < 10) {
            if (powered) off("Gamma power limit must be 10-100% or paused");
            live.state = "Gamma power limit must be 10-100% or paused"; publish(); delay(10); continue;
        }
        // Keep sampling while paused so thermal hysteresis can observe cooldown.
        if (now - sampled >= 250) { sampled = now; if (!collect(powered)) continue; }
        if (!minerPowerPercent() || (powered && !minerAsicCurrent(activeGeneration))) {
            if (powered) off("Paused / waiting for pool");
            delay(10); continue;
        }
        if (!powered || now - sent >= 100) {
            if (!minerAsicWork(next)) { if (powered) off("Waiting for pool"); delay(10); continue; }
            // Without enough negotiated rolling bits the chip exhausts its nonce space.
            // Refuse that pool instead of displaying a fictitious stock hashrate.
            if (__builtin_popcount(next.versionMask) < 8 || next.shareDifficulty < 16.0) {
                if (powered) off("Pool needs version rolling and difficulty >= 16");
                live.state = "Pool needs version rolling and difficulty >= 16"; publish(); delay(100); continue;
            }
            if (powered && activeMask != next.versionMask) off("Changing version mask");
            if (!powered) {
                if (!initAsic(next)) {
                    if (!latched && powered) off("BM1370 initialization failed", true);
                    else if (!powered) delay(500);  // waiting for the supply, the fan or the pool
                    continue;
                }
                now = millis(); sampled = now;
            }
            float target = targetFrequency();
            if (std::fabs(live.frequency - target) > 3) {
                // Power changes reset the chip: no mixing of in-flight results/PLL state.
                off("Applying power limit"); continue;
            }
            Slot &slot = slots[nextSlot];
            slot.work = next; slot.sent = now; slot.valid = true; slot.proofHead = slot.proofCount = 0;
            if (!send(bm1370::job(nextSlot, slot.work.header))) { off("ASIC UART write failed", true); continue; }
            nextSlot = (nextSlot + 1) % bm1370::kSlots; sent = now;
        }
        bm1370::Response response;
        size_t budget = 2048;
        while (powered && budget-- && uart.available()) if (receiver.push(uart.read(), response)) {
            if (!response.job) {
                registerResult(response);
                continue;
            }
            Slot &slot = slots[response.slot];
            if (!slot.valid || now - slot.sent > 1500 || !minerAsicCurrent(slot.work.generation)) continue;
            uint64_t proof = uint64_t(response.versionBits) << 32 | response.nonce;
            bool duplicate = false;
            for (int i = 0; i < slot.proofCount; ++i) if (slot.proofs[i] == proof) duplicate = true;
            if (duplicate) continue;
            slot.proofs[slot.proofHead] = proof; slot.proofHead = (slot.proofHead + 1) % 32;
            if (slot.proofCount < 32) ++slot.proofCount;
            minerAsicCandidate(slot.work, response.nonce, response.versionBits);
        }
        if (!powered) continue;
        if (now - requested >= 1000) {
            requested = now;
            if (!request(0x4c, false) || !request(0x8c, false)) { off("ASIC counter request failed", true); continue; }
        }
        checkWatchdogs(millis());
        delay(2);
    }
}
} // namespace

void begin() {
    pinMode(kReset, OUTPUT); digitalWrite(kReset, LOW);
    configured = configure();
    live.state = configured ? "Ready / waiting for pool" : kNotFound; publish();
    // A first reading at once: the thermal policy then has a temperature from
    // the start, without a "temperature unavailable" notification at boot.
    if (configured) collect(false);
}
bool start() {
    live.requestedMHz = bm1370::validProfile(settings.asicFrequency) ? settings.asicFrequency : bm1370::kStockMHz;
    publish();
    // Started even when the setup failed: the task tries it again.
    bool ok = runtime::started(xTaskCreatePinnedToCore(worker, "bm1370", 8192, nullptr, 3, nullptr, 1) == pdPASS,
        runtime::Miner, "BM1370 task");
    if (!ok) off("ASIC task allocation failed", true);
    return ok;
}
Status status() { portENTER_CRITICAL(&mux); Status out = snapshot; portEXIT_CRITICAL(&mux); return out; }
// The ASIC diode when it reads; else the board's sensor, so that a pause
// (the ASIC off, its diode silent) never looks like a lost sensor.
float temperature() {
    float asic = status().temperature;
    return std::isfinite(asic) ? asic : boardTemperature.load();
}
} // namespace gamma601
#endif
