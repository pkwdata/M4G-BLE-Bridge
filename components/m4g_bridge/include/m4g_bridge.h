#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define M4G_BRIDGE_MAX_SLOTS 2
#define M4G_INVALID_SLOT 0xFF
#include "esp_err.h"

// Initialize bridge subsystem (currently stateless)
esp_err_t m4g_bridge_init(void);

// Process a raw USB HID input report (from CharaChorder halves). Will emit 0..N BLE reports.
// slot identifies which USB HID endpoint produced the report (0..N-1).
// report points to raw bytes, len is number of bytes in original USB interrupt transfer.
void m4g_bridge_process_usb_report(uint8_t slot, const uint8_t *report, size_t len);

// Notify the bridge that a USB HID slot has been disconnected/reset
void m4g_bridge_reset_slot(uint8_t slot);

// Optional: query last sent keyboard report (for debugging)
bool m4g_bridge_get_last_keyboard(uint8_t out[8]);

// Optional: query last sent mouse report (for debugging)
bool m4g_bridge_get_last_mouse(uint8_t out[3]);

typedef struct
{
  uint32_t keyboard_reports_sent;
  uint32_t mouse_reports_sent;
} m4g_bridge_stats_t;

void m4g_bridge_get_stats(m4g_bridge_stats_t *out);
