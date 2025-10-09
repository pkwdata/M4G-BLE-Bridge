#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// Initialize LED subsystem (auto-detects GPIO on first boot)
esp_err_t m4g_led_init(void);

// Set connection states (updates LED color automatically)
void m4g_led_set_usb_connected(bool connected);
void m4g_led_set_ble_connected(bool connected);

// Manually set LED color (bypasses auto-state)
void m4g_led_force_color(uint8_t r, uint8_t g, uint8_t b);

// Query connection states
bool m4g_led_is_usb_connected(void);
bool m4g_led_is_ble_connected(void);

// Clear stored LED GPIO from NVS (forces re-detection on next boot)
esp_err_t m4g_led_clear_stored_gpio(void);
