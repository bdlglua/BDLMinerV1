// Host stub: no data partition, so the hashrate history is not persisted.
#pragma once
#include <cstddef>
#include <cstdint>
typedef int esp_err_t;
#ifndef ESP_OK
#define ESP_OK 0
#endif
enum { ESP_PARTITION_TYPE_DATA = 1, ESP_PARTITION_SUBTYPE_DATA_SPIFFS = 0x82 };
struct esp_partition_t { uint32_t size; };
inline const esp_partition_t *esp_partition_find_first(int, int, const char *) { return nullptr; }
inline esp_err_t esp_partition_read(const esp_partition_t *, size_t, void *, size_t) { return -1; }
inline esp_err_t esp_partition_write(const esp_partition_t *, size_t, const void *, size_t) { return -1; }
inline esp_err_t esp_partition_erase_range(const esp_partition_t *, size_t, size_t) { return -1; }
