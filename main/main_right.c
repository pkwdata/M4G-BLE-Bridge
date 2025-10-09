// Right-side main: USB receiver + ESP-NOW transmitter (no BLE)
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "m4g_logging.h"
#include "m4g_led.h"
#include "m4g_usb.h"
#include "m4g_espnow.h"
#include "m4g_diag.h"

static const char *TAG = "M4G-RIGHT";

// USB report callback - forward HID reports to LEFT side via ESP-NOW
static void usb_report_callback(const uint8_t *data, size_t len)
{
    if (!data || len == 0 || len > 64)
    {
        return;
    }

    // Check report type and forward to LEFT via ESP-NOW
    uint8_t report_id = data[0];
    bool is_charachorder = (len >= 2 && data[1] != 0); // Heuristic: CharaChorder reports have data
    
    esp_err_t ret = m4g_espnow_send_hid_report(0, data, len, is_charachorder);
    if (ret != ESP_OK && ENABLE_DEBUG_USB_LOGGING)
    {
        ESP_LOGW(TAG, "Failed to send HID report via ESP-NOW: %s", esp_err_to_name(ret));
    }
}

void app_main(void)
{
    // Disable log persistence for right side (minimal flash wear)
    m4g_logging_disable_persistence();

    // Enable USB and keypress logging for debugging
    m4g_log_enable_usb(true);
    m4g_log_enable_keypress(true);

    ESP_LOGI(TAG, "Booting M4G BLE Bridge - RIGHT SIDE");
    LOG_AND_SAVE(true, I, TAG, "Booting M4G BLE Bridge - RIGHT SIDE (USB-to-ESP-NOW)");

    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK)
    {
        LOG_AND_SAVE(true, E, TAG, "NVS init failed: %s", esp_err_to_name(err));
        return;
    }

    // Initialize LED subsystem
    if (m4g_led_init() != ESP_OK)
    {
        LOG_AND_SAVE(true, E, TAG, "LED init failed");
        return;
    }

    // Initialize ESP-NOW for transmitting to left side
    m4g_espnow_config_t espnow_cfg = {0};
    espnow_cfg.role = M4G_ESPNOW_ROLE_RIGHT;
    espnow_cfg.rx_callback = NULL; // Right side doesn't need to receive
    espnow_cfg.channel = CONFIG_M4G_ESPNOW_CHANNEL;
    
#ifdef CONFIG_M4G_ESPNOW_USE_ENCRYPTION
    espnow_cfg.use_pmk = true;
    const char *pmk_str = CONFIG_M4G_ESPNOW_PMK;
    memcpy(espnow_cfg.pmk, pmk_str, 16);
#else
    espnow_cfg.use_pmk = false;
#endif

    // Peer MAC is broadcast initially (left side will be discovered)
    memset(espnow_cfg.peer_mac, 0xFF, 6);

    if (m4g_espnow_init(&espnow_cfg) != ESP_OK)
    {
        LOG_AND_SAVE(true, E, TAG, "ESP-NOW init failed");
        return;
    }

    // Initialize USB subsystem
    if (m4g_usb_init(NULL, usb_report_callback) != ESP_OK)
    {
        LOG_AND_SAVE(true, E, TAG, "USB init failed");
        return;
    }

    LOG_AND_SAVE(true, I, TAG, "Right side initialization complete - waiting for USB devices");

    // Run diagnostics
    vTaskDelay(pdMS_TO_TICKS(200));
    m4g_diag_run_startup_checks();

    // Main loop - monitor status and update LED
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Update LED status
        // Green = USB connected
        // Red = No USB
        bool usb_connected = m4g_usb_is_connected();
        m4g_led_set_usb_connected(usb_connected);
        
        // For right side, we don't have BLE, so LED will be green when USB is connected
        // You could also set a color based on ESP-NOW peer connectivity
        bool espnow_connected = m4g_espnow_is_peer_connected();
        
        if (ENABLE_DEBUG_USB_LOGGING)
        {
            ESP_LOGD(TAG, "Status: USB=%d ESP-NOW_peer=%d", usb_connected, espnow_connected);
        }

        // Periodic stats logging
        static uint32_t stats_counter = 0;
        if (++stats_counter >= 10) // Every 10 seconds
        {
            stats_counter = 0;
            m4g_espnow_stats_t stats;
            m4g_espnow_get_stats(&stats);
            LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, TAG,
                         "ESP-NOW stats: TX=%lu RX=%lu failures=%lu lost=%lu RSSI=%d",
                         stats.packets_sent, stats.packets_received, stats.send_failures,
                         stats.packets_lost, stats.last_rssi);
        }

        // Flush logs
        if (m4g_log_persistence_enabled())
        {
            m4g_log_flush();
        }
    }
}
