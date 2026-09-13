#pragma once
#include <Arduino.h>
#include "config.h"

// Lightweight logging macros for ESP8266. Use compile-time LOG_LEVEL to reduce code size.
// Levels: 1=ERROR,2=WARN,3=INFO,4=DEBUG,5=TRACE
#ifndef LOG_LEVEL
#define LOG_LEVEL 3
#endif

#define LOG_ERROR 1
#define LOG_WARN  2
#define LOG_INFO  3
#define LOG_DEBUG 4
#define LOG_TRACE 5

static inline const char* _logFileName(const char* f) {
  const char* s = strrchr(f, '/');
  return s ? s + 1 : f;
}

#define LOG_LVL(lvl, tag, fmt, ...) do { \
  if ((lvl) <= LOG_LEVEL) { \
    uint32_t _h = ESP.getFreeHeap(); \
    Serial.printf("[%lu][%s][%s:%d][h=%u] ", millis(), (tag), _logFileName(__FILE__), __LINE__, _h); \
    Serial.printf((fmt), ##__VA_ARGS__); \
    Serial.println(); \
  } \
} while (0)

#define LOGE(tag, fmt, ...) LOG_LVL(LOG_ERROR, tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) LOG_LVL(LOG_WARN,  tag, fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) LOG_LVL(LOG_INFO,  tag, fmt, ##__VA_ARGS__)
#define LOGD(tag, fmt, ...) LOG_LVL(LOG_DEBUG, tag, fmt, ##__VA_ARGS__)
#define LOGT(tag, fmt, ...) LOG_LVL(LOG_TRACE, tag, fmt, ##__VA_ARGS__)

// Shortcut to print heap with a tag
#define LOG_HEAP(tag) do { if (LOG_DEBUG <= LOG_LEVEL) { Serial.printf("[%lu][%s][heap=%u]\n", millis(), (tag), ESP.getFreeHeap()); } } while (0)
