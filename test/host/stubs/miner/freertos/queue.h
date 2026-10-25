#pragma once
#include <deque>
#include <vector>
#include <cstring>
struct Queue {
    size_t capacity, size;
    std::deque<std::vector<char>> entries;
};
using QueueHandle_t = Queue *;
inline QueueHandle_t xQueueCreate(size_t capacity, size_t size) { return new Queue{capacity, size, {}}; }
inline int xQueueReset(QueueHandle_t q) {
    q->entries.clear();
    return 1;
}
inline int xQueueSend(QueueHandle_t q, const void *p, unsigned) {
    if (q->entries.size() >= q->capacity) return 0;
    q->entries.emplace_back((const char *)p, (const char *)p + q->size);
    return 1;
}
inline int xQueueReceive(QueueHandle_t q, void *p, unsigned) {
    if (q->entries.empty()) return 0;
    memcpy(p, q->entries.front().data(), q->size);
    q->entries.pop_front();
    return 1;
}
