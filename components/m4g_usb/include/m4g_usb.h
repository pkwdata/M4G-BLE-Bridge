#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

// Callback signature for delivering raw HID input reports (up to 64 bytes)
typedef void (*m4g_usb_hid_report_cb_t)(const uint8_t *data, size_t len);

typedef struct
{
  const char *device_name; // optional descriptive name
} m4g_usb_config_t;

// Initialize USB host and (optionally) register HID callback
esp_err_t m4g_usb_init(const m4g_usb_config_t *cfg, m4g_usb_hid_report_cb_t cb);

// Poll / process events (if using cooperative model). Generally a task is spawned internally.
void m4g_usb_task(void *param);

// Query number of active HID devices
uint8_t m4g_usb_active_hid_count(void);

// Force rescan / re-enumeration (placeholder)
void m4g_usb_request_rescan(void);

// Whether the USB side is logically "connected" (>=1 active device)
bool m4g_usb_is_connected(void);
