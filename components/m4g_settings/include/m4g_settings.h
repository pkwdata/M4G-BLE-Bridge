/**
 * @file m4g_settings.h
 * @brief Runtime configuration settings with NVS persistence
 *
 * Provides runtime-adjustable settings for chord detection, key repeat,
 * and feature toggles. Settings can be persisted to NVS and survive reboots.
 *
 * Based on CharaChorder DeviceManager settings system analysis.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

  /**
   * @brief Setting IDs for get/set operations
   *
   * These IDs are used to identify specific settings for runtime modification.
   * Compatible with CharaChorder-style VAR B1/B2 command interface.
   */
  typedef enum
  {
    // Chord Detection Settings (0x01-0x0F)
    M4G_SETTING_CHORD_DELAY_MS = 0x01,                 /*!< CharaChorder output detection delay (ms) */
    M4G_SETTING_CHORD_TIMEOUT_MS = 0x02,               /*!< Single-key timeout before emit (ms) */
    M4G_SETTING_CHORD_PRESS_DEVIATION_MAX_MS = 0x03,   /*!< Max press deviation for chords (ms) */
    M4G_SETTING_CHORD_RELEASE_DEVIATION_MAX_MS = 0x04, /*!< Max release deviation for chords (ms) */

    // Key Repeat Settings (0x10-0x1F)
    M4G_SETTING_KEY_REPEAT_ENABLED = 0x10,  /*!< Enable/disable key repeat (bool) */
    M4G_SETTING_KEY_REPEAT_DELAY_MS = 0x11, /*!< Initial delay before repeat starts (ms) */
    M4G_SETTING_KEY_REPEAT_RATE_MS = 0x12,  /*!< Time between repeats (ms) */

    // Feature Toggles (0x20-0x2F)
    M4G_SETTING_RAW_MODE_ENABLED = 0x20,              /*!< Bypass chord detection (bool) */
    M4G_SETTING_DUPLICATE_SUPPRESSION_ENABLED = 0x21, /*!< Suppress duplicate reports (bool) */
    M4G_SETTING_DEVIATION_TRACKING_ENABLED = 0x22,    /*!< Track chord quality metrics (bool) */

    M4G_SETTING_MAX /*!< Sentinel value for validation */
  } m4g_setting_id_t;

  /**
   * @brief Setting metadata for validation and UI
   */
  typedef struct
  {
    m4g_setting_id_t id;     /*!< Setting identifier */
    const char *name;        /*!< Human-readable name */
    const char *description; /*!< Help text */
    bool is_boolean;         /*!< True if boolean, false if integer */
    uint32_t min_value;      /*!< Minimum value (for integers) */
    uint32_t max_value;      /*!< Maximum value (for integers) */
    uint32_t default_value;  /*!< Default value from Kconfig */
    const char *unit;        /*!< Unit string (e.g., "ms", "%") */
  } m4g_setting_metadata_t;

  /**
   * @brief Initialize settings subsystem
   *
   * Loads settings from NVS if available, otherwise uses Kconfig defaults.
   * Must be called after nvs_flash_init() and before using any settings.
   *
   * @return
   *      - ESP_OK: Success
   *      - ESP_ERR_NVS_NOT_INITIALIZED: NVS not initialized
   *      - ESP_ERR_NO_MEM: Failed to allocate memory
   */
  esp_err_t m4g_settings_init(void);

  /**
   * @brief Get a setting value
   *
   * @param setting_id Setting to retrieve
   * @param value Pointer to store the value
   * @return
   *      - ESP_OK: Success
   *      - ESP_ERR_INVALID_ARG: Invalid setting_id or NULL value pointer
   *      - ESP_ERR_NOT_FOUND: Setting not initialized
   */
  esp_err_t m4g_settings_get(m4g_setting_id_t setting_id, uint32_t *value);

  /**
   * @brief Set a setting value (temporary, not persisted)
   *
   * Changes the setting in RAM only. Use m4g_settings_commit() to persist to NVS.
   *
   * @param setting_id Setting to modify
   * @param value New value (validated against min/max for integers)
   * @return
   *      - ESP_OK: Success
   *      - ESP_ERR_INVALID_ARG: Invalid setting_id or value out of range
   *      - ESP_ERR_NOT_FOUND: Setting not initialized
   */
  esp_err_t m4g_settings_set(m4g_setting_id_t setting_id, uint32_t value);

  /**
   * @brief Commit all settings to NVS (persist across reboots)
   *
   * CAUTION: NVS has limited write cycles (~100,000). Don't call this
   * on every setting change. Only commit when user explicitly saves.
   *
   * Similar to CharaChorder's VAR B0 command.
   *
   * @return
   *      - ESP_OK: Success
   *      - ESP_ERR_NVS_*: NVS operation failed
   */
  esp_err_t m4g_settings_commit(void);

  /**
   * @brief Reset all settings to Kconfig defaults
   *
   * Resets RAM and NVS to default values. Similar to CharaChorder's RST PARAMS.
   *
   * @param erase_nvs If true, erases NVS storage. If false, just resets RAM.
   * @return
   *      - ESP_OK: Success
   *      - ESP_ERR_NVS_*: NVS operation failed
   */
  esp_err_t m4g_settings_reset_to_defaults(bool erase_nvs);

  /**
   * @brief Get setting metadata for UI/validation
   *
   * @param setting_id Setting to query
   * @return Pointer to metadata structure, or NULL if invalid setting_id
   */
  const m4g_setting_metadata_t *m4g_settings_get_metadata(m4g_setting_id_t setting_id);

  /**
   * @brief Get all settings metadata
   *
   * @param count Pointer to store number of settings
   * @return Pointer to array of metadata structures
   */
  const m4g_setting_metadata_t *m4g_settings_get_all_metadata(size_t *count);

  /**
   * @brief Print all current settings to console
   *
   * Useful for debugging and diagnostics.
   */
  void m4g_settings_dump(void);

  // Convenience accessors for frequently used settings

  /**
   * @brief Get chord delay in milliseconds
   */
  static inline uint32_t m4g_settings_get_chord_delay_ms(void)
  {
    uint32_t value = CONFIG_M4G_CHORD_DELAY_MS_DEFAULT;
    m4g_settings_get(M4G_SETTING_CHORD_DELAY_MS, &value);
    return value;
  }

  /**
   * @brief Get chord timeout in milliseconds
   */
  static inline uint32_t m4g_settings_get_chord_timeout_ms(void)
  {
    uint32_t value = CONFIG_M4G_CHORD_TIMEOUT_MS_DEFAULT;
    m4g_settings_get(M4G_SETTING_CHORD_TIMEOUT_MS, &value);
    return value;
  }

  /**
   * @brief Get maximum chord press deviation in milliseconds
   */
  static inline uint32_t m4g_settings_get_chord_press_deviation_max_ms(void)
  {
    uint32_t value = CONFIG_M4G_CHORD_PRESS_DEVIATION_MAX_MS_DEFAULT;
    m4g_settings_get(M4G_SETTING_CHORD_PRESS_DEVIATION_MAX_MS, &value);
    return value;
  }

  /**
   * @brief Get maximum chord release deviation in milliseconds
   */
  static inline uint32_t m4g_settings_get_chord_release_deviation_max_ms(void)
  {
    uint32_t value = CONFIG_M4G_CHORD_RELEASE_DEVIATION_MAX_MS_DEFAULT;
    m4g_settings_get(M4G_SETTING_CHORD_RELEASE_DEVIATION_MAX_MS, &value);
    return value;
  }

  /**
   * @brief Check if key repeat is enabled
   */
  static inline bool m4g_settings_is_key_repeat_enabled(void)
  {
    uint32_t value = 0;
#ifdef CONFIG_M4G_ENABLE_KEY_REPEAT
    value = CONFIG_M4G_ENABLE_KEY_REPEAT;
#endif
    m4g_settings_get(M4G_SETTING_KEY_REPEAT_ENABLED, &value);
    return (bool)value;
  }

  /**
   * @brief Get key repeat delay in milliseconds
   */
  static inline uint32_t m4g_settings_get_key_repeat_delay_ms(void)
  {
    uint32_t value = CONFIG_M4G_KEY_REPEAT_DELAY_MS_DEFAULT;
    m4g_settings_get(M4G_SETTING_KEY_REPEAT_DELAY_MS, &value);
    return value;
  }

  /**
   * @brief Get key repeat rate in milliseconds
   */
  static inline uint32_t m4g_settings_get_key_repeat_rate_ms(void)
  {
    uint32_t value = CONFIG_M4G_KEY_REPEAT_RATE_MS_DEFAULT;
    m4g_settings_get(M4G_SETTING_KEY_REPEAT_RATE_MS, &value);
    return value;
  }

  /**
   * @brief Check if raw mode is enabled
   */
  static inline bool m4g_settings_is_raw_mode_enabled(void)
  {
    uint32_t value = 0;
#ifdef CONFIG_M4G_RAW_MODE_DEFAULT
    value = CONFIG_M4G_RAW_MODE_DEFAULT;
#endif
    m4g_settings_get(M4G_SETTING_RAW_MODE_ENABLED, &value);
    return (bool)value;
  }

  /**
   * @brief Check if duplicate suppression is enabled
   */
  static inline bool m4g_settings_is_duplicate_suppression_enabled(void)
  {
    uint32_t value = CONFIG_M4G_DUPLICATE_SUPPRESSION_DEFAULT;
    m4g_settings_get(M4G_SETTING_DUPLICATE_SUPPRESSION_ENABLED, &value);
    return (bool)value;
  }

  /**
   * @brief Check if deviation tracking is enabled
   */
  static inline bool m4g_settings_is_deviation_tracking_enabled(void)
  {
    uint32_t value = 0;
#ifdef CONFIG_M4G_CHORD_DEVIATION_TRACKING_DEFAULT
    value = CONFIG_M4G_CHORD_DEVIATION_TRACKING_DEFAULT;
#endif
    m4g_settings_get(M4G_SETTING_DEVIATION_TRACKING_ENABLED, &value);
    return (bool)value;
  }

#ifdef __cplusplus
}
#endif
