#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define M4G_BRIDGE_MAX_SLOTS 2
#define M4G_INVALID_SLOT 0xFF

// Initialize bridge subsystem
esp_err_t m4g_bridge_init(void);

// Process a raw USB HID input report. slot identifies which USB HID endpoint produced the report.
// report points to raw bytes, len is number of bytes in original USB interrupt transfer.
// is_charachorder indicates whether the originating device is a CharaChorder half.
void m4g_bridge_process_usb_report(uint8_t slot, const uint8_t *report, size_t len, bool is_charachorder);

// Notify the bridge that a USB HID slot has been disconnected/reset
void m4g_bridge_reset_slot(uint8_t slot);

// Update CharaChorder detection state (from USB layer)
void m4g_bridge_set_charachorder_status(bool detected, bool both_halves_connected);

// Optional: query last sent keyboard report (for debugging)
bool m4g_bridge_get_last_keyboard(uint8_t out[8]);

// Optional: query last sent mouse report (for debugging)
bool m4g_bridge_get_last_mouse(uint8_t out[3]);

// Process key repeat (should be called periodically from main loop)
void m4g_bridge_process_key_repeat(void);

typedef struct
{
  uint32_t keyboard_reports_sent;
  uint32_t mouse_reports_sent;
  uint32_t chord_reports_processed;
  uint32_t chord_reports_delayed;
} m4g_bridge_stats_t;

void m4g_bridge_get_stats(m4g_bridge_stats_t *out);
