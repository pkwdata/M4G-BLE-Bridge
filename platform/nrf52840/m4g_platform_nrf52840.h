#pragma once

/*
 * nRF52840 + MAX3421E Platform Configuration
 * 
 * Platform-specific configuration for nRF52840 with external MAX3421E
 * USB host controller via SPI.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// nRF52840 platform configuration
typedef struct {
    // SPI configuration for MAX3421E
    uint8_t spi_instance;        // 0, 1, 2 for nRF52840
    uint32_t spi_frequency;      // SPI clock frequency
    uint8_t cs_pin;              // Chip select GPIO
    uint8_t int_pin;             // Interrupt GPIO from MAX3421E
    uint8_t reset_pin;           // Reset GPIO to MAX3421E (-1 if not used)
    
    // LED configuration
    uint8_t led_pin;             // GPIO for status LED
    bool led_active_low;         // true if LED is active low
    
    // USB Host configuration
    uint8_t max_devices;         // Maximum concurrent USB devices
    uint32_t poll_interval_ms;   // MAX3421E polling interval
    
    // BLE configuration (Zephyr/SoftDevice)
    const char *device_name;
    uint16_t appearance;
    bool enable_bonding;
    
    // Power management
    bool enable_dcdc;            // Enable DC/DC converter
    uint32_t sleep_timeout_ms;   // Idle sleep timeout
} m4g_platform_config_t;

// nRF52840 specific functions
bool m4g_nrf52840_init_spi(void);
bool m4g_nrf52840_init_max3421e(void);
bool m4g_nrf52840_init_ble_stack(void);
void m4g_nrf52840_poll_max3421e(void);

#ifdef __cplusplus
}
#endif