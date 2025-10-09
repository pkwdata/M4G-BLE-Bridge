/* ESP32-S3 Platform Adapter Implementation
 * Concrete platform layer wrapping components (LED -> BLE -> Bridge -> USB)
 * so future ports only reimplement this file and common header.
 */
#include "m4g_platform.h"
#include "m4g_usb.h"
#include "m4g_ble.h"
#include "m4g_led.h"
#include "m4g_logging.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *PLAT_TAG = "M4G-PLAT";

static void platform_usb_report_cb(const uint8_t *data, size_t len)
{
    // This callback is not actually used by the USB component
    // The USB component calls m4g_bridge_process_usb_report directly
    (void)data;
    (void)len;
}

esp_err_t m4g_platform_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;

    if (m4g_led_init() != ESP_OK) {
        ESP_LOGE(PLAT_TAG, "LED init failed");
        return ESP_FAIL;
    }
    if (m4g_ble_init() != ESP_OK) {
        ESP_LOGE(PLAT_TAG, "BLE init failed");
        return ESP_FAIL;
    }
    if (m4g_bridge_init() != ESP_OK) {
        ESP_LOGE(PLAT_TAG, "Bridge init failed");
        return ESP_FAIL;
    }
    if (m4g_usb_init(NULL, platform_usb_report_cb) != ESP_OK) {
        ESP_LOGE(PLAT_TAG, "USB init failed");
        return ESP_FAIL;
    }
    ESP_LOGI(PLAT_TAG, "Platform init complete");
    return ESP_OK;
}

esp_err_t m4g_platform_run(void)
{
    return ESP_OK;
}