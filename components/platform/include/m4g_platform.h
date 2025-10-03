/**
 * @file m4g_platform.h
 * @brief Platform abstraction layer for M4G BLE Bridge
 *
 * Provides a unified interface for different hardware platforms:
 * - ESP32-S3 DevKit (native USB host)
 * - Adafruit QT Py ESP32-S3 (native USB host, compact)
 * - nRF52840 + MAX3421E (external USB host controller) - future
 *
 * This layer wraps component initialization and provides platform-specific
 * configurations while keeping the core bridge logic platform-agnostic.
 */

#ifndef M4G_PLATFORM_H
#define M4G_PLATFORM_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

  /**
   * @brief Platform types supported by the firmware
   */
  typedef enum
  {
    M4G_PLATFORM_ESP32S3_DEVKIT,    ///< ESP32-S3 DevKit with native USB OTG
    M4G_PLATFORM_ESP32S3_QTPY,      ///< Adafruit QT Py ESP32-S3
    M4G_PLATFORM_NRF52840_MAX3421E, ///< nRF52840 with MAX3421E USB host (future)
    M4G_PLATFORM_UNKNOWN
  } m4g_platform_type_t;

  /**
   * @brief Initialize the platform abstraction layer and all subsystems
   *
   * Initializes in order:
   * 1. NVS (for BLE bonding and persistent logs)
   * 2. LED subsystem (for status indication)
   * 3. BLE subsystem (HOGP stack)
   * 4. Bridge subsystem (HID report translation)
   * 5. USB subsystem (host with hub support)
   *
   * @return ESP_OK on success, error code otherwise
   */
  esp_err_t m4g_platform_init(void);

  /**
   * @brief Main platform run loop (if needed by platform)
   *
   * For ESP32-S3: No-op, uses FreeRTOS tasks
   * For nRF52840: May implement custom event loop
   *
   * @return ESP_OK on success, error code otherwise
   */
  esp_err_t m4g_platform_run(void);

  /**
   * @brief Get the current platform type
   *
   * @return Platform type enum based on compile-time configuration
   */
  m4g_platform_type_t m4g_platform_get_type(void);

  /**
   * @brief Get platform name string
   *
   * @return Human-readable platform name
   */
  const char *m4g_platform_get_name(void);

#ifdef __cplusplus
}
#endif

#endif // M4G_PLATFORM_H
