#pragma once
#include <stdbool.h>
#include <stdio.h>

// Exposed debug flags (defined in m4g_logging.c)
extern bool ENABLE_DEBUG_LED_LOGGING;
extern bool ENABLE_DEBUG_USB_LOGGING;
extern bool ENABLE_DEBUG_BLE_LOGGING;
extern bool ENABLE_DEBUG_KEYPRESS_LOGGING;

void m4g_log_dump_and_clear(void);
void m4g_logging_set_nvs_ready(void);
void m4g_logging_disable_persistence(void);
void m4g_log_enable_led(bool en);
void m4g_log_enable_usb(bool en);
void m4g_log_enable_ble(bool en);
void m4g_log_enable_keypress(bool en);

bool m4g_log_is_led_enabled(void);
bool m4g_log_is_usb_enabled(void);
bool m4g_log_is_ble_enabled(void);
bool m4g_log_is_keypress_enabled(void);

void m4g_log_append_line(const char *line);
void m4g_log_flush(void); // flush staged logs to NVS (if persistence enabled)
bool m4g_log_persistence_enabled(void);

// Unified logging macro (should_log decides gating)
#include "esp_log.h"

#define LOG_AND_SAVE(should_log, level, tag, fmt, ...)        \
  do                                                          \
  {                                                           \
    if (should_log)                                           \
    {                                                         \
      char _logbuf[256];                                      \
      snprintf(_logbuf, sizeof(_logbuf), fmt, ##__VA_ARGS__); \
      ESP_LOG##level(tag, "%s", _logbuf);                     \
      m4g_log_append_line(_logbuf);                           \
    }                                                         \
  } while (0)
