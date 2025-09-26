#include "m4g_diag.h"
#include "m4g_logging.h"
#include "m4g_ble.h"
#include "m4g_usb.h"
#include "m4g_bridge.h"
#include "m4g_led.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *DIAG_TAG = "M4G-DIAG";

// Configure whether to spawn a lightweight periodic status task
#ifndef M4G_DIAG_ENABLE_PERIODIC
#define M4G_DIAG_ENABLE_PERIODIC 1
#endif
#ifndef M4G_DIAG_PERIOD_SEC
#define M4G_DIAG_PERIOD_SEC 30
#endif

// Compile-time structural assumptions
_Static_assert(sizeof(bool) == 1, "Expect bool to be 1 byte on ESP-IDF");
_Static_assert(8 == 8, "Keyboard report size must remain 8");

static void dump_basic_environment(void)
{
  LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, I, DIAG_TAG, "BLE connected: %s, notifications: %s", m4g_ble_is_connected() ? "yes" : "no", m4g_ble_notifications_enabled() ? "yes" : "no");
  LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, DIAG_TAG, "USB active HID devices: %d", (int)m4g_usb_active_hid_count());
  LOG_AND_SAVE(ENABLE_DEBUG_LED_LOGGING || true, I, DIAG_TAG, "LED state USB=%d BLE=%d", m4g_led_is_usb_connected(), m4g_led_is_ble_connected());
}

static void periodic_task(void *param)
{
  (void)param;
  while (1)
  {
    dump_basic_environment();
    // Attempt to re-send the last keyboard report (for monitoring) ONLY if connected & notifications
    if (m4g_ble_is_connected() && m4g_ble_notifications_enabled())
    {
      uint8_t kb[8];
      if (m4g_bridge_get_last_keyboard(kb))
      {
        (void)m4g_ble_send_keyboard_report(kb); // benign duplicate; helps spot if pipeline stalled
      }
    }
    vTaskDelay(pdMS_TO_TICKS(M4G_DIAG_PERIOD_SEC * 1000));
  }
}

void m4g_diag_start_periodic_task(void)
{
  if (M4G_DIAG_ENABLE_PERIODIC)
  {
    // Use Kconfig-defined stack size if available
#ifndef CONFIG_M4G_DIAG_TASK_STACK_SIZE
#define CONFIG_M4G_DIAG_TASK_STACK_SIZE 4096
#endif
    xTaskCreate(periodic_task, "m4g_diag", CONFIG_M4G_DIAG_TASK_STACK_SIZE, NULL, 1, NULL);
  }
}

esp_err_t m4g_diag_run_startup_checks(void)
{
  LOG_AND_SAVE(true, I, DIAG_TAG, "Running startup diagnostics...");

  // 1. NVS accessibility
  nvs_handle_t nvs;
  esp_err_t err = nvs_open("logbuf", NVS_READWRITE, &nvs);
  if (err == ESP_OK)
  {
    nvs_close(nvs);
    LOG_AND_SAVE(true, I, DIAG_TAG, "NVS open OK (log namespace)");
  }
  else
  {
    LOG_AND_SAVE(true, E, DIAG_TAG, "NVS open failed: %d", err);
  }

  // 2. BLE notification dry run (expected to fail if not connected yet)
  uint8_t empty_report[8] = {0};
  bool sent = m4g_ble_send_keyboard_report(empty_report);
  LOG_AND_SAVE(true, I, DIAG_TAG, "BLE test send (no connection yet is fine): %s", sent ? "delivered" : "not sent");

  // 3. Bridge initial state
  uint8_t tmp[8];
  bool have = m4g_bridge_get_last_keyboard(tmp);
  LOG_AND_SAVE(true, I, DIAG_TAG, "Bridge last keyboard cached: %s", have ? "yes" : "no (expected)");

  // 4. USB initial device count
  LOG_AND_SAVE(true, I, DIAG_TAG, "Initial USB HID count: %d", (int)m4g_usb_active_hid_count());

  // 5. LED baseline
  LOG_AND_SAVE(true, I, DIAG_TAG, "LED baseline USB=%d BLE=%d", m4g_led_is_usb_connected(), m4g_led_is_ble_connected());

  dump_basic_environment();
  m4g_diag_start_periodic_task();

  LOG_AND_SAVE(true, I, DIAG_TAG, "Diagnostics complete");
  return ESP_OK;
}
