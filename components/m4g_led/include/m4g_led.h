#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t m4g_led_init(void);
void m4g_led_set_usb_connected(bool connected);
void m4g_led_set_ble_connected(bool connected);
void m4g_led_force_color(uint8_t r, uint8_t g, uint8_t b);
bool m4g_led_is_usb_connected(void);
bool m4g_led_is_ble_connected(void);
