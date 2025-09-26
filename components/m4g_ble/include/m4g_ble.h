#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Initialize BLE stack & HID service
esp_err_t m4g_ble_init(void);

// Start advertising (safe to call after init or after disconnect)
void m4g_ble_start_advertising(void);

// Send a HID keyboard report (8 bytes standard: mods, reserved, 6 keys)
bool m4g_ble_send_keyboard_report(const uint8_t report[8]);

// Send a HID mouse report (3 bytes: buttons, x, y) - optional future
bool m4g_ble_send_mouse_report(const uint8_t report[3]);

// Connection / notification status helpers
bool m4g_ble_is_connected(void);
bool m4g_ble_notifications_enabled(void);

// Process BLE host events (to be run in its own task if required)
void m4g_ble_host_task(void *param);
