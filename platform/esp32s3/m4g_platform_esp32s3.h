#pragma once

/*
 * ESP32-S3 Platform Configuration
 * 
 * Platform-specific configuration and adapter implementation for ESP32-S3
 * with native USB host capabilities.
 */

#include <stdint.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// ESP32-S3 platform configuration
typedef struct {
    // GPIO configuration
    gpio_num_t led_data_gpio;
    gpio_num_t led_power_gpio;    // -1 if not used
    gpio_num_t vbus_enable_gpio;  // -1 if not used
    
    // USB Host configuration
    bool enable_vbus_control;
    uint32_t usb_debounce_delay_ms;
    
    // BLE configuration
    uint16_t ble_max_connections;
    uint16_t ble_att_mtu;
    
    // Task configuration
    uint32_t main_task_stack_size;
    uint8_t usb_task_priority;
} m4g_platform_config_t;

// ESP32-S3 specific functions
void m4g_esp32s3_init_gpio(void);
void m4g_esp32s3_init_usb_host(void);
void m4g_esp32s3_init_ble_stack(void);

#ifdef __cplusplus
}
#endif