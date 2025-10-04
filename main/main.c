// Orchestrator main after modularization
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_sleep.h" // for esp_light_sleep_start
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "m4g_platform.h" // Platform abstraction layer
#include "m4g_logging.h"
#include "m4g_led.h"
#include "m4g_ble.h"
#include "m4g_usb.h"
#include "m4g_bridge.h"
#include "m4g_diag.h"
#include "m4g_settings.h" // Runtime settings

// Ensure boolean types are available for IntelliSense
#ifndef __cplusplus
#ifndef true
#define true 1
#endif
#ifndef false
#define false 0
#endif
#ifndef bool
#define bool _Bool
#endif
#endif

static const char *TAG = "M4G-BLE-BRIDGE";

// Fallback defaults if sdkconfig hasn't been regenerated with new Kconfig symbols yet
#ifndef CONFIG_M4G_STACK_WATERMARK_PERIOD_MS
#define CONFIG_M4G_STACK_WATERMARK_PERIOD_MS 0
#endif
#ifndef CONFIG_M4G_STACK_WATERMARK_MAX_TASKS
#define CONFIG_M4G_STACK_WATERMARK_MAX_TASKS 16
#endif
#ifndef CONFIG_M4G_IDLE_SLEEP_TIMEOUT_MS
#define CONFIG_M4G_IDLE_SLEEP_TIMEOUT_MS 0
#endif

static void log_stack_watermarks(void)
{
#if CONFIG_M4G_STACK_WATERMARK_PERIOD_MS > 0
#if (configUSE_TRACE_FACILITY == 1)
    static TaskStatus_t s_task_array[CONFIG_M4G_STACK_WATERMARK_MAX_TASKS];
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    if (task_count > CONFIG_M4G_STACK_WATERMARK_MAX_TASKS)
        task_count = CONFIG_M4G_STACK_WATERMARK_MAX_TASKS;
    UBaseType_t snapshot_count = uxTaskGetSystemState(s_task_array, task_count, NULL);
    for (UBaseType_t i = 0; i < snapshot_count; ++i)
    {
        UBaseType_t watermark = uxTaskGetStackHighWaterMark((TaskHandle_t)s_task_array[i].xHandle);
        ESP_LOGD(TAG, "STACK %s HW=%u", s_task_array[i].pcTaskName, (unsigned)watermark);
    }
#else
    // Trace facility disabled; fall back to logging just current task watermark
    ESP_LOGV(TAG, "Trace facility disabled; skipping detailed stack watermarks");
#endif
#endif
}

void app_main(void)
{
#if CONFIG_M4G_BOARD_DEVKIT
    m4g_logging_disable_persistence();
#endif
    // Enable USB and keypress logging for debugging
    m4g_log_enable_usb(true);
    m4g_log_enable_keypress(true);
    m4g_log_enable_ble(true); // Temporarily enable to debug typing issue

    ESP_LOGI(TAG, "Booting M4G BLE Bridge");
    ESP_LOGI(TAG, "Platform: %s", m4g_platform_get_name());
    LOG_AND_SAVE(true, I, TAG, "Booting M4G BLE Bridge on %s", m4g_platform_get_name());

#if !CONFIG_M4G_BOARD_DEVKIT
    // For boards with persistent logging, dump previous boot logs
    m4g_log_dump_and_clear();
#endif

    // Initialize all platform subsystems (NVS, LED, BLE, Bridge, USB)
    if (m4g_platform_init() != ESP_OK)
    {
        LOG_AND_SAVE(true, E, TAG, "Platform init failed");
        return;
    }

    // Initialize runtime settings (after NVS init in m4g_platform_init)
    ESP_LOGI(TAG, "Initializing runtime settings");
    if (m4g_settings_init() != ESP_OK)
    {
        LOG_AND_SAVE(true, W, TAG, "Settings init failed, using defaults");
    }

    vTaskDelay(pdMS_TO_TICKS(200));

    m4g_diag_run_startup_checks();
    LOG_AND_SAVE(true, I, TAG, "Initialization complete");

    TickType_t last_stack_log = xTaskGetTickCount();
    const TickType_t stack_period_ticks = (CONFIG_M4G_STACK_WATERMARK_PERIOD_MS > 0) ? pdMS_TO_TICKS(CONFIG_M4G_STACK_WATERMARK_PERIOD_MS) : 0;

#if CONFIG_M4G_IDLE_SLEEP_TIMEOUT_MS > 0
    TickType_t idle_start = 0;
    bool idle_tracking = false;
#endif

    for (;;)
    {
        // Process key repeat with shorter delay for responsive repeat
        vTaskDelay(pdMS_TO_TICKS(10)); // 10ms for responsive key repeat
        m4g_bridge_process_key_repeat();

        // Only do the following checks every 1 second
        static TickType_t last_heartbeat = 0;
        TickType_t now = xTaskGetTickCount();
        if ((now - last_heartbeat) < pdMS_TO_TICKS(1000))
        {
            continue;
        }
        last_heartbeat = now;
        if (m4g_log_persistence_enabled())
        {
            m4g_log_flush();
        }
        if (ENABLE_DEBUG_BLE_LOGGING)
        {
            ESP_LOGD(TAG, "HB BLE=%d USB=%d", (int)m4g_ble_is_connected(), (int)m4g_usb_is_connected());
        }
        if (stack_period_ticks > 0 && (xTaskGetTickCount() - last_stack_log) >= stack_period_ticks)
        {
            log_stack_watermarks();
            last_stack_log = xTaskGetTickCount();
        }

#if CONFIG_M4G_IDLE_SLEEP_TIMEOUT_MS > 0
        bool usb_active = (m4g_usb_active_hid_count() > 0);
        bool ble_active = m4g_ble_is_connected();
        if (!usb_active && !ble_active)
        {
            if (!idle_tracking)
            {
                idle_tracking = true;
                idle_start = xTaskGetTickCount();
            }
            else if ((xTaskGetTickCount() - idle_start) >= pdMS_TO_TICKS(CONFIG_M4G_IDLE_SLEEP_TIMEOUT_MS))
            {
                LOG_AND_SAVE(true, I, TAG, "Entering light sleep (idle: no USB/BLE)");
                esp_light_sleep_start();
                idle_tracking = false; // reset after wake
            }
        }
        else
        {
            if (idle_tracking && ENABLE_DEBUG_BLE_LOGGING)
            {
                ESP_LOGD(TAG, "Cancel idle-sleep: usb_active=%d ble_active=%d", (int)usb_active, (int)ble_active);
            }
            idle_tracking = false; // activity detected
        }
#endif
    }
}

// BLE host task
// (Legacy ble_host_task removed; provided by m4g_ble if required)

// Initialize BLE
// (Legacy ble_init removed; BLE initialization provided by m4g_ble_init)

// Send HID report over BLE (currently unused - for future USB integration)
/*
static void send_hid_report(uint8_t *data, size_t len)
{
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGD(TAG, "No BLE connection, dropping HID report");
        return;
    }

    // TODO: Send actual HID report via BLE
    LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, TAG, "Sending HID report (%d bytes)", len);
    ESP_LOG_BUFFER_HEX(TAG, data, len);
}
*/

// (Removed legacy duplicate app_main and USB/BLE legacy blocks)