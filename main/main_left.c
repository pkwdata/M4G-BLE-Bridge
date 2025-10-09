// Left-side main: USB receiver + ESP-NOW receiver + BLE transmitter
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_err.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "m4g_platform.h"
#include "m4g_logging.h"
#include "m4g_led.h"
#include "m4g_ble.h"
#include "m4g_usb.h"
#include "m4g_bridge.h"
#include "m4g_espnow.h"
#include "m4g_diag.h"
#include "m4g_settings.h"

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

static const char *TAG = "M4G-LEFT";

// Fallback defaults if sdkconfig hasn't been regenerated
#ifndef CONFIG_M4G_STACK_WATERMARK_PERIOD_MS
#define CONFIG_M4G_STACK_WATERMARK_PERIOD_MS 0
#endif
#ifndef CONFIG_M4G_STACK_WATERMARK_MAX_TASKS
#define CONFIG_M4G_STACK_WATERMARK_MAX_TASKS 16
#endif
#ifndef CONFIG_M4G_IDLE_SLEEP_TIMEOUT_MS
#define CONFIG_M4G_IDLE_SLEEP_TIMEOUT_MS 0
#endif

// Local USB report callback (unused - USB component calls m4g_bridge directly)
static void local_usb_report_cb(const uint8_t *data, size_t len)
{
    // This callback is not actually used by the USB component
    // The USB component calls m4g_bridge_process_usb_report directly
    (void)data;
    (void)len;
}

// ESP-NOW receive callback - reports from right side
static void espnow_rx_cb(uint8_t slot, const uint8_t *report, size_t len, bool is_charachorder)
{
    if (ENABLE_DEBUG_KEYPRESS_LOGGING)
    {
        LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, TAG,
                     "Remote (right) report via ESP-NOW: slot=%d len=%zu charachorder=%d",
                     slot, len, is_charachorder);
    }
    
    // Process through bridge (use slot 1 for right side to distinguish from left)
    m4g_bridge_process_usb_report(1, report, len, is_charachorder);
}

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
    m4g_log_enable_ble(true);

    ESP_LOGI(TAG, "Booting M4G BLE Bridge - LEFT SIDE (Split Keyboard)");
    LOG_AND_SAVE(true, I, TAG, "Booting M4G BLE Bridge - LEFT SIDE on %s", m4g_platform_get_name());

#if !CONFIG_M4G_BOARD_DEVKIT
    // For boards with persistent logging, dump previous boot logs
    m4g_log_dump_and_clear();
#endif

    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK)
    {
        LOG_AND_SAVE(true, E, TAG, "NVS init failed");
        return;
    }

    // Initialize LED subsystem
    if (m4g_led_init() != ESP_OK)
    {
        LOG_AND_SAVE(true, E, TAG, "LED init failed");
        return;
    }

    // Initialize BLE subsystem
    if (m4g_ble_init() != ESP_OK)
    {
        LOG_AND_SAVE(true, E, TAG, "BLE init failed");
        return;
    }

    // Initialize bridge subsystem
    if (m4g_bridge_init() != ESP_OK)
    {
        LOG_AND_SAVE(true, E, TAG, "Bridge init failed");
        return;
    }

    // Initialize ESP-NOW for receiving from right side
    m4g_espnow_config_t espnow_cfg = {0};
    espnow_cfg.role = M4G_ESPNOW_ROLE_LEFT;
    espnow_cfg.rx_callback = espnow_rx_cb;
    espnow_cfg.channel = CONFIG_M4G_ESPNOW_CHANNEL;
    
#ifdef CONFIG_M4G_ESPNOW_USE_ENCRYPTION
    espnow_cfg.use_pmk = true;
    const char *pmk_str = CONFIG_M4G_ESPNOW_PMK;
    memcpy(espnow_cfg.pmk, pmk_str, 16);
#else
    espnow_cfg.use_pmk = false;
#endif

    // Peer MAC is broadcast initially (right side will be discovered)
    memset(espnow_cfg.peer_mac, 0xFF, 6);

    if (m4g_espnow_init(&espnow_cfg) != ESP_OK)
    {
        LOG_AND_SAVE(true, E, TAG, "ESP-NOW init failed");
        return;
    }

    // Initialize USB subsystem for left keyboard
    if (m4g_usb_init(NULL, local_usb_report_cb) != ESP_OK)
    {
        LOG_AND_SAVE(true, E, TAG, "USB init failed");
        return;
    }

    // Initialize runtime settings (after NVS init)
    ESP_LOGI(TAG, "Initializing runtime settings");
    if (m4g_settings_init() != ESP_OK)
    {
        LOG_AND_SAVE(true, W, TAG, "Settings init failed, using defaults");
    }

    vTaskDelay(pdMS_TO_TICKS(200));

    m4g_diag_run_startup_checks();
    LOG_AND_SAVE(true, I, TAG, "Left side initialization complete");

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
            bool espnow_peer = m4g_espnow_is_peer_connected();
            ESP_LOGD(TAG, "HB BLE=%d USB=%d ESP-NOW_peer=%d", 
                     (int)m4g_ble_is_connected(), (int)m4g_usb_is_connected(), (int)espnow_peer);
        }
        
        if (stack_period_ticks > 0 && (xTaskGetTickCount() - last_stack_log) >= stack_period_ticks)
        {
            log_stack_watermarks();
            last_stack_log = xTaskGetTickCount();
        }

        // Periodic stats logging
        static uint32_t stats_counter = 0;
        if (++stats_counter >= 10) // Every 10 seconds
        {
            stats_counter = 0;
            m4g_espnow_stats_t espnow_stats;
            m4g_espnow_get_stats(&espnow_stats);
            LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, TAG,
                         "ESP-NOW stats: TX=%lu RX=%lu failures=%lu lost=%lu RSSI=%d",
                         espnow_stats.packets_sent, espnow_stats.packets_received, 
                         espnow_stats.send_failures, espnow_stats.packets_lost, espnow_stats.last_rssi);
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
                idle_tracking = false;
            }
        }
        else
        {
            if (idle_tracking && ENABLE_DEBUG_BLE_LOGGING)
            {
                ESP_LOGD(TAG, "Cancel idle-sleep: usb_active=%d ble_active=%d", (int)usb_active, (int)ble_active);
            }
            idle_tracking = false;
        }
#endif
    }
}
