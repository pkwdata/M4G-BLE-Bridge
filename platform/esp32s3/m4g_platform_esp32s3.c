/*
 * ESP32-S3 Platform Adapter Implementation
 * 
 * Wraps existing ESP32-S3 specific components (m4g_usb, m4g_ble, m4g_led)
 * to provide the unified platform abstraction interface.
 */

#include "platform/common/m4g_platform.h"
#include "m4g_usb.h"
#include "m4g_ble.h"
#include "m4g_led.h"
#include "m4g_logging.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdarg.h>

static const char *TAG = "M4G-PLATFORM-ESP32S3";
static m4g_usb_report_callback_t s_usb_callback = NULL;

// USB report callback adapter
static void esp32s3_usb_report_cb(const uint8_t *data, size_t len)
{
    if (s_usb_callback) {
        // Check if this is from a CharaChorder device
        bool is_charachorder = m4g_usb_is_charachorder_detected();
        s_usb_callback(data, len, is_charachorder);
    }
}

// Platform lifecycle
void m4g_platform_init(const m4g_platform_config_t *config)
{
    (void)config; // Use sdkconfig values for now
    ESP_LOGI(TAG, "Initializing ESP32-S3 platform");
    
    // NVS is initialized in main.c, so we just log here
    ESP_LOGI(TAG, "ESP32-S3 platform initialization complete");
}

void m4g_platform_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing ESP32-S3 platform");
    // Cleanup would go here
}

uint32_t m4g_platform_millis(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void m4g_platform_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

// USB Host abstraction
bool m4g_platform_usb_init(const m4g_usb_config_t *config)
{
    if (config && config->report_callback) {
        s_usb_callback = config->report_callback;
    }
    
    m4g_usb_config_t esp_config = {0};
    return m4g_usb_init(&esp_config, esp32s3_usb_report_cb) == ESP_OK;
}

void m4g_platform_usb_deinit(void)
{
    // Not implemented in current ESP32-S3 component
}

void m4g_platform_usb_poll(void)
{
    // ESP32-S3 USB is interrupt-driven, no polling needed
}

bool m4g_platform_usb_is_connected(void)
{
    return m4g_usb_is_connected();
}

uint8_t m4g_platform_usb_active_device_count(void)
{
    return m4g_usb_active_hid_count();
}

bool m4g_platform_usb_is_charachorder_detected(void)
{
    return m4g_usb_is_charachorder_detected();
}

bool m4g_platform_usb_charachorder_both_halves_connected(void)
{
    return m4g_usb_charachorder_both_halves_connected();
}

// BLE abstraction
bool m4g_platform_ble_init(const m4g_ble_config_t *config)
{
    (void)config; // Use existing initialization
    return m4g_ble_init() == ESP_OK;
}

void m4g_platform_ble_deinit(void)
{
    // Not implemented in current ESP32-S3 component
}

bool m4g_platform_ble_is_connected(void)
{
    return m4g_ble_is_connected();
}

bool m4g_platform_ble_notifications_enabled(void)
{
    return m4g_ble_notifications_enabled();
}

bool m4g_platform_ble_send_keyboard_report(const uint8_t *report)
{
    return m4g_ble_send_keyboard_report(report);
}

bool m4g_platform_ble_send_mouse_report(const uint8_t *report)
{
    return m4g_ble_send_mouse_report(report);
}

void m4g_platform_ble_start_advertising(void)
{
    // Handled automatically by m4g_ble component
}

void m4g_platform_ble_stop_advertising(void)
{
    // Not implemented in current ESP32-S3 component
}

// LED abstraction
bool m4g_platform_led_init(void)
{
    return m4g_led_init() == ESP_OK;
}

void m4g_platform_led_deinit(void)
{
    // Not implemented in current ESP32-S3 component
}

void m4g_platform_led_set_state(m4g_led_state_t state)
{
    // Map platform states to ESP32-S3 LED states
    switch (state) {
        case M4G_LED_OFF:
            m4g_led_set_usb_connected(false);
            m4g_led_set_ble_connected(false);
            break;
        case M4G_LED_USB_ONLY:
            m4g_led_set_usb_connected(true);
            m4g_led_set_ble_connected(false);
            break;
        case M4G_LED_BLE_ONLY:
            m4g_led_set_usb_connected(false);
            m4g_led_set_ble_connected(true);
            break;
        case M4G_LED_FULL_BRIDGE:
            m4g_led_set_usb_connected(true);
            m4g_led_set_ble_connected(true);
            break;
        case M4G_LED_ERROR:
            // Flash red or similar error indication
            m4g_led_set_usb_connected(false);
            m4g_led_set_ble_connected(false);
            break;
    }
}

void m4g_platform_led_set_usb_connected(bool connected)
{
    m4g_led_set_usb_connected(connected);
}

void m4g_platform_led_set_ble_connected(bool connected)
{
    m4g_led_set_ble_connected(connected);
}

// Logging abstraction
void m4g_log(m4g_log_level_t level, const char *tag, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    
    esp_log_level_t esp_level;
    switch (level) {
        case M4G_LOG_ERROR:   esp_level = ESP_LOG_ERROR; break;
        case M4G_LOG_WARN:    esp_level = ESP_LOG_WARN; break;
        case M4G_LOG_INFO:    esp_level = ESP_LOG_INFO; break;
        case M4G_LOG_DEBUG:   esp_level = ESP_LOG_DEBUG; break;
        case M4G_LOG_VERBOSE: esp_level = ESP_LOG_VERBOSE; break;
        default:              esp_level = ESP_LOG_INFO; break;
    }
    
    esp_log_writev(esp_level, tag, fmt, args);
    va_end(args);
}

void m4g_log_flush(void)
{
    // Use existing logging system
    if (m4g_log_persistence_enabled()) {
        m4g_log_flush();
    }
}

// Power management
void m4g_power_enter_light_sleep(void)
{
    esp_light_sleep_start();
}

bool m4g_power_can_sleep(void)
{
    // Check if any wake sources would prevent sleep
    return !m4g_usb_is_connected() && !m4g_ble_is_connected();
}

// NVS abstraction
bool m4g_nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret == ESP_OK;
}

bool m4g_nvs_set_blob(const char *key, const void *data, size_t length)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("m4g", NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;
    
    err = nvs_set_blob(handle, key, data, length);
    nvs_close(handle);
    return err == ESP_OK;
}

bool m4g_nvs_get_blob(const char *key, void *data, size_t *length)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("m4g", NVS_READONLY, &handle);
    if (err != ESP_OK) return false;
    
    err = nvs_get_blob(handle, key, data, length);
    nvs_close(handle);
    return err == ESP_OK;
}

bool m4g_nvs_erase_key(const char *key)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("m4g", NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;
    
    err = nvs_erase_key(handle, key);
    nvs_close(handle);
    return err == ESP_OK;
}

void m4g_nvs_commit(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("m4g", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_commit(handle);
        nvs_close(handle);
    }
}