#include "crashlog.h"

#include <esp_core_dump.h>
#include <esp_ota_ops.h>
#include <esp_spi_flash.h>

#include "log.h"

// ESP-IDF stops the chip after abort(), a failed assert or a task watchdog
// through panic_abort(), which writes to address 0 on purpose: its "cause 29"
// is not the problem.
extern "C" void panic_abort(const char *details);

namespace {

bool present = false;
String summary;
size_t imageAddress = 0, imageSize = 0;

#if CONFIG_IDF_TARGET_ARCH_XTENSA
// "LoadProhibited (cause 28) at 0x00000010": the usual Xtensa exception causes.
String exceptionName(uint32_t cause, uint32_t address) {
    static const struct { uint8_t cause; const char *name; } kNames[] = {
        {0, "IllegalInstruction"},    {2, "InstructionFetchError"}, {3, "LoadStoreError"},
        {6, "IntegerDivideByZero"},   {9, "LoadStoreAlignment"},    {20, "InstFetchProhibited"},
        {28, "LoadProhibited"},       {29, "StoreProhibited"},
    };
    char out[64];
    const char *name = "exception";
    for (const auto &k : kNames)
        if (k.cause == cause) name = k.name;
    if (cause == 3 || cause == 9 || cause >= 20)
        snprintf(out, sizeof(out), "%s (cause %u) at 0x%08x", name, (unsigned)cause, (unsigned)address);
    else
        snprintf(out, sizeof(out), "%s (cause %u)", name, (unsigned)cause);
    return out;
}
#endif

}  // namespace

void crashBegin() {
    if (esp_core_dump_image_check() != ESP_OK || esp_core_dump_image_get(&imageAddress, &imageSize) != ESP_OK) return;
    present = true;
    esp_core_dump_summary_t s;
    if (esp_core_dump_get_summary(&s) != ESP_OK) {
        summary = "crash report present (no summary)";
    } else {
        char line[220];
        // The dump survives updates: addresses only mean something to its own build.
        char running[65];
        bool earlier = esp_ota_get_app_elf_sha256(running, sizeof(running)) > 0 &&
                       strncmp(running, (const char *)s.app_elf_sha256, 16) != 0;
        uintptr_t abortAt = (uintptr_t)&panic_abort;
        bool aborted = !earlier && s.exc_pc >= abortAt && s.exc_pc < abortAt + 0x40;
#if CONFIG_IDF_TARGET_ARCH_XTENSA
        // Another build: panic_abort's own write to address 0 is the best sign left.
        bool likelyAbort = earlier && s.ex_info.exc_cause == 29 && s.ex_info.exc_vaddr == 0;
#else
        bool likelyAbort = false;
#endif
        int n = aborted       ? snprintf(line, sizeof(line), "abort() called in task %.16s", s.exc_task)
                : likelyAbort ? snprintf(line, sizeof(line), "abort() or a null pointer write in task %.16s", s.exc_task)
                              : snprintf(line, sizeof(line), "task %.16s, PC 0x%08x", s.exc_task, (unsigned)s.exc_pc);
#if CONFIG_IDF_TARGET_ARCH_XTENSA
        if (!aborted && !likelyAbort) n += snprintf(line + n, sizeof(line) - n, ", %s", exceptionName(s.ex_info.exc_cause, s.ex_info.exc_vaddr).c_str());
        if (s.exc_bt_info.depth) {
            n += snprintf(line + n, sizeof(line) - n, ", backtrace");
            for (uint32_t i = 0; i < s.exc_bt_info.depth && i < 8 && n < (int)sizeof(line) - 12; ++i)
                n += snprintf(line + n, sizeof(line) - n, " 0x%08x", (unsigned)s.exc_bt_info.bt[i]);
            if (s.exc_bt_info.corrupted && n < (int)sizeof(line) - 12) snprintf(line + n, sizeof(line) - n, " (corrupted)");
        }
#endif
        summary = line;
        if (earlier) summary += " (earlier firmware)";
    }
    logf("[crash] last crash: %s; report %u bytes at /api/coredump\n", summary.c_str(), (unsigned)imageSize);
}

bool crashPresent() { return present; }

const String &crashSummary() { return summary; }

size_t crashSize() { return present ? imageSize : 0; }

bool crashRead(size_t offset, uint8_t *out, size_t length) {
    if (!present || offset > imageSize || length > imageSize - offset) return false;
    return spi_flash_read(imageAddress + offset, out, length) == ESP_OK;
}

bool crashErase() {
    if (!present) return true;
    if (esp_core_dump_image_erase() != ESP_OK) return false;
    present = false;
    summary = "";
    imageAddress = imageSize = 0;
    logf("[crash] report erased\n");
    return true;
}
