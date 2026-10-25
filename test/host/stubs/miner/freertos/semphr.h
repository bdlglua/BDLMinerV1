#pragma once
using SemaphoreHandle_t = int;
inline int xSemaphoreCreateMutex() { return 1; }
inline int xSemaphoreTake(int, unsigned) { return 1; }
inline int xSemaphoreGive(int) { return 1; }
