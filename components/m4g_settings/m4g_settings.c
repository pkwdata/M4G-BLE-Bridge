/**
 * @file m4g_settings.c
 * @brief Runtime configuration settings with NVS persistence implementation
 */

#include "m4g_settings.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "m4g_settings";

#define NVS_NAMESPACE "m4g_settings"
#define NVS_KEY_PREFIX "setting_"

// Current runtime settings (RAM)
static uint32_t s_settings[M4G_SETTING_MAX];
static bool s_initialized = false;

// Setting metadata table
static const m4g_setting_metadata_t s_metadata[] = {
    // Chord Detection Settings
    {
        .id = M4G_SETTING_CHORD_DELAY_MS,
        .name = "Chord Delay",
        .description = "CharaChorder output detection delay",
        .is_boolean = false,
        .min_value = 10,
        .max_value = 50,
        .default_value = CONFIG_M4G_CHORD_DELAY_MS_DEFAULT,
        .unit = "ms"},
    {.id = M4G_SETTING_CHORD_TIMEOUT_MS,
     .name = "Chord Timeout",
     .description = "Single-key timeout before emit for repeat",
     .is_boolean = false,
     .min_value = 100,
     .max_value = 2000,
     .default_value = CONFIG_M4G_CHORD_TIMEOUT_MS_DEFAULT,
     .unit = "ms"},
    {.id = M4G_SETTING_CHORD_PRESS_DEVIATION_MAX_MS,
     .name = "Press Deviation Max",
     .description = "Maximum press time spread for chord",
     .is_boolean = false,
     .min_value = 20,
     .max_value = 500,
     .default_value = CONFIG_M4G_CHORD_PRESS_DEVIATION_MAX_MS_DEFAULT,
     .unit = "ms"},
    {.id = M4G_SETTING_CHORD_RELEASE_DEVIATION_MAX_MS,
     .name = "Release Deviation Max",
     .description = "Maximum release time spread for chord",
     .is_boolean = false,
     .min_value = 20,
     .max_value = 300,
     .default_value = CONFIG_M4G_CHORD_RELEASE_DEVIATION_MAX_MS_DEFAULT,
     .unit = "ms"},

    // Key Repeat Settings
    {
        .id = M4G_SETTING_KEY_REPEAT_ENABLED,
        .name = "Key Repeat",
        .description = "Enable key repeat functionality",
        .is_boolean = true,
        .min_value = 0,
        .max_value = 1,
#ifdef CONFIG_M4G_ENABLE_KEY_REPEAT
        .default_value = CONFIG_M4G_ENABLE_KEY_REPEAT,
#else
        .default_value = 0,
#endif
        .unit = ""},
    {.id = M4G_SETTING_KEY_REPEAT_DELAY_MS,
     .name = "Repeat Delay",
     .description = "Initial delay before repeat starts",
     .is_boolean = false,
     .min_value = 250,
     .max_value = 2000,
     .default_value = CONFIG_M4G_KEY_REPEAT_DELAY_MS_DEFAULT,
     .unit = "ms"},
    {.id = M4G_SETTING_KEY_REPEAT_RATE_MS,
     .name = "Repeat Rate",
     .description = "Time between repeated keys",
     .is_boolean = false,
     .min_value = 16,
     .max_value = 200,
     .default_value = CONFIG_M4G_KEY_REPEAT_RATE_MS_DEFAULT,
     .unit = "ms"},

    // Feature Toggles
    {
        .id = M4G_SETTING_RAW_MODE_ENABLED,
        .name = "Raw Mode",
        .description = "Bypass chord detection",
        .is_boolean = true,
        .min_value = 0,
        .max_value = 1,
#ifdef CONFIG_M4G_RAW_MODE_DEFAULT
        .default_value = CONFIG_M4G_RAW_MODE_DEFAULT,
#else
        .default_value = 0,
#endif
        .unit = ""},
    {.id = M4G_SETTING_DUPLICATE_SUPPRESSION_ENABLED,
     .name = "Duplicate Suppression",
     .description = "Suppress identical consecutive reports",
     .is_boolean = true,
     .min_value = 0,
     .max_value = 1,
     .default_value = CONFIG_M4G_DUPLICATE_SUPPRESSION_DEFAULT,
     .unit = ""},
    {.id = M4G_SETTING_DEVIATION_TRACKING_ENABLED,
     .name = "Chord Deviation Tracking",
     .description = "Log chord timing quality",
     .is_boolean = true,
     .min_value = 0,
     .max_value = 1,
#ifdef CONFIG_M4G_CHORD_DEVIATION_TRACKING_DEFAULT
     .default_value = CONFIG_M4G_CHORD_DEVIATION_TRACKING_DEFAULT,
#else
     .default_value = 0,
#endif
     .unit = ""}};

static const size_t s_metadata_count = sizeof(s_metadata) / sizeof(s_metadata[0]);

/**
 * @brief Load a single setting from NVS
 */
static esp_err_t load_setting_from_nvs(nvs_handle_t handle, m4g_setting_id_t setting_id)
{
  if (setting_id >= M4G_SETTING_MAX)
  {
    return ESP_ERR_INVALID_ARG;
  }

  char key[16];
  snprintf(key, sizeof(key), "%s%02X", NVS_KEY_PREFIX, (unsigned int)setting_id);

  uint32_t value;
  esp_err_t err = nvs_get_u32(handle, key, &value);
  if (err == ESP_OK)
  {
    s_settings[setting_id] = value;
    ESP_LOGI(TAG, "Loaded setting 0x%02X = %u from NVS", setting_id, (unsigned int)value);
  }
  else if (err == ESP_ERR_NVS_NOT_FOUND)
  {
    // Not in NVS, use default
    const m4g_setting_metadata_t *meta = m4g_settings_get_metadata(setting_id);
    if (meta)
    {
      s_settings[setting_id] = meta->default_value;
      ESP_LOGI(TAG, "Setting 0x%02X not in NVS, using default %u", setting_id, (unsigned int)meta->default_value);
    }
  }

  return err;
}

/**
 * @brief Save a single setting to NVS
 */
static esp_err_t save_setting_to_nvs(nvs_handle_t handle, m4g_setting_id_t setting_id)
{
  if (setting_id >= M4G_SETTING_MAX)
  {
    return ESP_ERR_INVALID_ARG;
  }

  char key[16];
  snprintf(key, sizeof(key), "%s%02X", NVS_KEY_PREFIX, (unsigned int)setting_id);

  esp_err_t err = nvs_set_u32(handle, key, s_settings[setting_id]);
  if (err == ESP_OK)
  {
    ESP_LOGI(TAG, "Saved setting 0x%02X = %u to NVS", setting_id, (unsigned int)s_settings[setting_id]);
  }

  return err;
}

esp_err_t m4g_settings_init(void)
{
  if (s_initialized)
  {
    ESP_LOGW(TAG, "Already initialized");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Initializing settings subsystem");

  // Initialize all settings to defaults first
  for (size_t i = 0; i < s_metadata_count; i++)
  {
    s_settings[s_metadata[i].id] = s_metadata[i].default_value;
  }

#ifdef CONFIG_M4G_SETTINGS_ENABLE_NVS_PERSISTENCE
  // Try to load from NVS
  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err == ESP_OK)
  {
    ESP_LOGI(TAG, "Loading settings from NVS");

    for (size_t i = 0; i < s_metadata_count; i++)
    {
      load_setting_from_nvs(handle, s_metadata[i].id);
    }

    nvs_close(handle);
  }
  else
  {
    ESP_LOGI(TAG, "No saved settings in NVS, using defaults");
  }

#ifdef CONFIG_M4G_SETTINGS_RESET_ON_BOOT
  ESP_LOGW(TAG, "CONFIG_M4G_SETTINGS_RESET_ON_BOOT enabled - resetting to defaults");
  m4g_settings_reset_to_defaults(true);
#endif
#else
  ESP_LOGI(TAG, "NVS persistence disabled, using compile-time defaults only");
#endif

  s_initialized = true;

  // Dump current settings
  m4g_settings_dump();

  return ESP_OK;
}

esp_err_t m4g_settings_get(m4g_setting_id_t setting_id, uint32_t *value)
{
  if (!s_initialized)
  {
    return ESP_ERR_NOT_FOUND;
  }
  if (setting_id >= M4G_SETTING_MAX)
  {
    return ESP_ERR_INVALID_ARG;
  }
  if (value == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  *value = s_settings[setting_id];
  return ESP_OK;
}

esp_err_t m4g_settings_set(m4g_setting_id_t setting_id, uint32_t value)
{
  if (!s_initialized)
  {
    return ESP_ERR_NOT_FOUND;
  }
  if (setting_id >= M4G_SETTING_MAX)
  {
    return ESP_ERR_INVALID_ARG;
  }

  const m4g_setting_metadata_t *meta = m4g_settings_get_metadata(setting_id);
  if (meta == NULL)
  {
    return ESP_ERR_INVALID_ARG;
  }

  // Validate range for non-boolean settings
  if (!meta->is_boolean)
  {
    if (value < meta->min_value || value > meta->max_value)
    {
      ESP_LOGE(TAG, "Setting 0x%02X value %u out of range [%u, %u]",
               setting_id, (unsigned int)value, (unsigned int)meta->min_value, (unsigned int)meta->max_value);
      return ESP_ERR_INVALID_ARG;
    }
  }
  else
  {
    // Boolean: coerce to 0 or 1
    value = value ? 1 : 0;
  }

  uint32_t old_value = s_settings[setting_id];
  s_settings[setting_id] = value;

  ESP_LOGI(TAG, "Setting 0x%02X (%s) changed: %u -> %u",
           setting_id, meta->name, (unsigned int)old_value, (unsigned int)value);

  return ESP_OK;
}

esp_err_t m4g_settings_commit(void)
{
  if (!s_initialized)
  {
    return ESP_ERR_NOT_FOUND;
  }

#ifdef CONFIG_M4G_SETTINGS_ENABLE_NVS_PERSISTENCE
  ESP_LOGI(TAG, "Committing settings to NVS");

  nvs_handle_t handle;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
    return err;
  }

  // Save all settings
  for (size_t i = 0; i < s_metadata_count; i++)
  {
    esp_err_t save_err = save_setting_to_nvs(handle, s_metadata[i].id);
    if (save_err != ESP_OK)
    {
      ESP_LOGE(TAG, "Failed to save setting 0x%02X: %s", s_metadata[i].id, esp_err_to_name(save_err));
    }
  }

  // Commit changes to NVS
  err = nvs_commit(handle);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
  }
  else
  {
    ESP_LOGI(TAG, "Successfully committed settings to NVS");
  }

  nvs_close(handle);
  return err;
#else
  ESP_LOGW(TAG, "NVS persistence disabled - commit ignored");
  return ESP_OK;
#endif
}

esp_err_t m4g_settings_reset_to_defaults(bool erase_nvs)
{
  if (!s_initialized)
  {
    return ESP_ERR_NOT_FOUND;
  }

  ESP_LOGW(TAG, "Resetting all settings to defaults (erase_nvs=%d)", erase_nvs);

  // Reset RAM to defaults
  for (size_t i = 0; i < s_metadata_count; i++)
  {
    s_settings[s_metadata[i].id] = s_metadata[i].default_value;
    ESP_LOGI(TAG, "Reset 0x%02X to default %u", s_metadata[i].id, (unsigned int)s_metadata[i].default_value);
  }

#ifdef CONFIG_M4G_SETTINGS_ENABLE_NVS_PERSISTENCE
  if (erase_nvs)
  {
    // Erase NVS namespace
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK)
    {
      err = nvs_erase_all(handle);
      if (err == ESP_OK)
      {
        err = nvs_commit(handle);
        ESP_LOGI(TAG, "Erased NVS settings namespace");
      }
      nvs_close(handle);
    }
    return err;
  }
#endif

  return ESP_OK;
}

const m4g_setting_metadata_t *m4g_settings_get_metadata(m4g_setting_id_t setting_id)
{
  for (size_t i = 0; i < s_metadata_count; i++)
  {
    if (s_metadata[i].id == setting_id)
    {
      return &s_metadata[i];
    }
  }
  return NULL;
}

const m4g_setting_metadata_t *m4g_settings_get_all_metadata(size_t *count)
{
  if (count)
  {
    *count = s_metadata_count;
  }
  return s_metadata;
}

void m4g_settings_dump(void)
{
  if (!s_initialized)
  {
    ESP_LOGW(TAG, "Settings not initialized");
    return;
  }

  ESP_LOGI(TAG, "Current Settings:");
  ESP_LOGI(TAG, "==========================================");

  for (size_t i = 0; i < s_metadata_count; i++)
  {
    const m4g_setting_metadata_t *meta = &s_metadata[i];
    uint32_t value = s_settings[meta->id];

    if (meta->is_boolean)
    {
      ESP_LOGI(TAG, "  [0x%02X] %-25s : %s",
               meta->id, meta->name, value ? "ENABLED" : "DISABLED");
    }
    else
    {
      ESP_LOGI(TAG, "  [0x%02X] %-25s : %u%s",
               meta->id, meta->name, (unsigned int)value, meta->unit);
    }
  }

  ESP_LOGI(TAG, "==========================================");
}
