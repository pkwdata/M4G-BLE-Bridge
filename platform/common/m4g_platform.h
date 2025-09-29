#pragma once

/*
 * Platform Abstraction Layer for M4G BLE Bridge
 * 
 * This header defines a unified interface that allows the M4G bridge core
 * to run on multiple platforms (ESP32-S3, nRF52840+MAX3421E, etc.).
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations for platform-specific types
typedef struct m4g_platform_config m4g_platform_config_t;

// LED status enumeration
typedef enum {
    M4G_LED_OFF = 0,
    M4G_LED_USB_ONLY,      // Yellow - USB connected, no BLE
    M4G_LED_BLE_ONLY,      // Blue - BLE connected, no USB
    M4G_LED_FULL_BRIDGE,   // Green - Both USB and BLE connected
    M4G_LED_ERROR          // Red - Error state
} m4g_led_state_t;

// USB HID report callback
typedef void (*m4g_usb_report_callback_t)(const uint8_t *data, size_t len, bool is_charachorder);

// Platform lifecycle
void m4g_platform_init(const m4g_platform_config_t *config);
void m4g_platform_deinit(void);
uint32_t m4g_platform_millis(void);
void m4g_platform_delay_ms(uint32_t ms);

// USB Host abstraction
typedef struct {
    m4g_usb_report_callback_t report_callback;
    uint32_t max_devices;
    bool enable_hub_support;
} m4g_usb_config_t;

bool m4g_usb_init(const m4g_usb_config_t *config);
void m4g_usb_deinit(void);
void m4g_usb_poll(void);  // For polling-based platforms (MAX3421E)
bool m4g_usb_is_connected(void);
uint8_t m4g_usb_active_device_count(void);
bool m4g_usb_is_charachorder_detected(void);
bool m4g_usb_charachorder_both_halves_connected(void);

// BLE abstraction
typedef struct {
    const char *device_name;
    const char *manufacturer_name;
    bool enable_bonding;
    uint16_t appearance;
} m4g_ble_config_t;

bool m4g_ble_init(const m4g_ble_config_t *config);
void m4g_ble_deinit(void);
bool m4g_ble_is_connected(void);
bool m4g_ble_notifications_enabled(void);
bool m4g_ble_send_keyboard_report(const uint8_t *report);
bool m4g_ble_send_mouse_report(const uint8_t *report);
void m4g_ble_start_advertising(void);
void m4g_ble_stop_advertising(void);

// LED abstraction
bool m4g_led_init(void);
void m4g_led_deinit(void);
void m4g_led_set_state(m4g_led_state_t state);
void m4g_led_set_usb_connected(bool connected);
void m4g_led_set_ble_connected(bool connected);

// Logging abstraction
typedef enum {
    M4G_LOG_ERROR = 0,
    M4G_LOG_WARN  = 1,
    M4G_LOG_INFO  = 2,
    M4G_LOG_DEBUG = 3,
    M4G_LOG_VERBOSE = 4
} m4g_log_level_t;

void m4g_log(m4g_log_level_t level, const char *tag, const char *fmt, ...);
void m4g_log_flush(void);

// Power management
void m4g_power_enter_light_sleep(void);
bool m4g_power_can_sleep(void);

// Non-volatile storage abstraction
bool m4g_nvs_init(void);
bool m4g_nvs_set_blob(const char *key, const void *data, size_t length);
bool m4g_nvs_get_blob(const char *key, void *data, size_t *length);
bool m4g_nvs_erase_key(const char *key);
void m4g_nvs_commit(void);

// Platform-specific configuration structure (defined per platform)
#if defined(CONFIG_M4G_TARGET_ESP32S3) || !defined(CONFIG_M4G_TARGET_NRF52840)
#include "platform/esp32s3/m4g_platform_esp32s3.h"
#elif defined(CONFIG_M4G_TARGET_NRF52840)
#include "platform/nrf52840/m4g_platform_nrf52840.h"
#endif

#ifdef __cplusplus
}
#endif