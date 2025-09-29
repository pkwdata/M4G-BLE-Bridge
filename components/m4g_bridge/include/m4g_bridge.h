#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Initialize bridge subsystem (currently stateless)
esp_err_t m4g_bridge_init(void);

// Process a raw USB HID input report (from CharaChorder halves). Will emit 0..N BLE reports.
// report points to raw bytes, len is number of bytes in original USB interrupt transfer.
// is_charachorder indicates if this report is from a CharaChorder device
void m4g_bridge_process_usb_report(const uint8_t *report, size_t len, bool is_charachorder);

// Set CharaChorder device status for chord processing logic
void m4g_bridge_set_charachorder_status(bool detected, bool both_halves_connected);

// Optional: query last sent keyboard report (for debugging)
bool m4g_bridge_get_last_keyboard(uint8_t out[8]);

// Optional: query last sent mouse report (for debugging)
bool m4g_bridge_get_last_mouse(uint8_t out[3]);

typedef struct
{
  uint32_t keyboard_reports_sent;
  uint32_t mouse_reports_sent;
  uint32_t chord_reports_processed;
  uint32_t chord_reports_delayed;
} m4g_bridge_stats_t;

void m4g_bridge_get_stats(m4g_bridge_stats_t *out);
