/**
 * @file m4g_platform_esp32s3.c
 * @brief ESP32-S3 Platform implementation
 *
 * Concrete platform layer wrapping components (LED -> BLE -> Bridge -> USB)
 * for ESP32-S3 based boards (DevKit and QT Py variants).
 */

#include "m4g_platform.h"
#include "m4g_usb.h"
#ifndef CONFIG_M4G_SPLIT_ROLE_RIGHT
#include "m4g_ble.h"
#include "m4g_bridge.h"
#else
// RIGHT side stubs - these functions are not available without bridge/ble
static inline esp_err_t m4g_ble_init(void) { return ESP_OK; }
static inline esp_err_t m4g_bridge_init(void) { return ESP_OK; }
#endif
#include "m4g_led.h"
#include "m4g_logging.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "M4G-PLAT";

esp_err_t m4g_platform_init(void)
{
  ESP_LOGI(TAG, "Initializing platform: %s", m4g_platform_get_name());

  // Initialize NVS flash for BLE bonding and persistent logs
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_LOGW(TAG, "NVS needs erase, erasing...");
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
    return err;
  }

  // Notify logging subsystem that NVS is ready
  m4g_logging_set_nvs_ready();

  // Initialize LED subsystem first for status indication during init
  if ((err = m4g_led_init()) != ESP_OK)
  {
    ESP_LOGE(TAG, "LED init failed: %s", esp_err_to_name(err));
    return err;
  }

  // Initialize BLE stack before bridge (bridge registers with BLE)
  if ((err = m4g_ble_init()) != ESP_OK)
  {
    ESP_LOGE(TAG, "BLE init failed: %s", esp_err_to_name(err));
    return err;
  }

  // Initialize bridge layer (registers callbacks with BLE)
  if ((err = m4g_bridge_init()) != ESP_OK)
  {
    ESP_LOGE(TAG, "Bridge init failed: %s", esp_err_to_name(err));
    return err;
  }

  // Initialize USB host last (starts enumeration, calls bridge directly)
  // Note: USB component calls m4g_bridge_process_usb_report() directly,
  // so we pass NULL for the optional notification callback
  if ((err = m4g_usb_init(NULL, NULL)) != ESP_OK)
  {
    ESP_LOGE(TAG, "USB init failed: %s", esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG, "Platform initialization complete");
  return ESP_OK;
}

esp_err_t m4g_platform_run(void)
{
  // ESP32-S3: No-op - components run via FreeRTOS tasks
  // For future nRF52840 port, this would implement main event loop
  return ESP_OK;
}

m4g_platform_type_t m4g_platform_get_type(void)
{
#if defined(CONFIG_M4G_BOARD_DEVKIT)
  return M4G_PLATFORM_ESP32S3_DEVKIT;
#elif defined(CONFIG_M4G_BOARD_QTPY)
  return M4G_PLATFORM_ESP32S3_QTPY;
#else
  // Fallback: assume DevKit if no board specified
  return M4G_PLATFORM_ESP32S3_DEVKIT;
#endif
}

const char *m4g_platform_get_name(void)
{
  switch (m4g_platform_get_type())
  {
  case M4G_PLATFORM_ESP32S3_DEVKIT:
    return "ESP32-S3 DevKit";
  case M4G_PLATFORM_ESP32S3_QTPY:
    return "Adafruit QT Py ESP32-S3";
  case M4G_PLATFORM_NRF52840_MAX3421E:
    return "nRF52840 + MAX3421E";
  default:
    return "Unknown Platform";
  }
}
