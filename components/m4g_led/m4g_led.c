#include "m4g_led.h"
#include "driver/gpio.h"
#include "simple_neopixel.h"
#include "m4g_logging.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *LED_TAG = "M4G-LED";

// NVS key for storing detected LED GPIO
#define NVS_NAMESPACE "m4g_board"
#define NVS_KEY_LED_GPIO "led_gpio"
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

#define LED_BRIGHTNESS 10
#define RGB_LED_NUM_LEDS 1

// Kconfig parameters (defined in main/Kconfig; legacy Kconfig.projbuild removed)

static simple_neopixel_t rgb_led;
static bool usb_connected = false;
static bool ble_connected = false;

static void apply_color(uint8_t r, uint8_t g, uint8_t b)
{
#if CONFIG_M4G_LED_TYPE_NONE
  (void)r;
  (void)g;
  (void)b;
  return;
#elif CONFIG_M4G_LED_TYPE_NEOPIXEL
  simple_neopixel_set_rgb(&rgb_led, r, g, b);
  return;
#elif CONFIG_M4G_LED_TYPE_SIMPLE
  // TODO: Implement simple single GPIO LED (e.g., brightness >0 => ON)
  (void)r;
  (void)g;
  (void)b;
  return;
#endif
}

static void update_led_state(void)
{
  uint8_t r = 0, g = 0, b = 0;
  if (!usb_connected && !ble_connected)
  { // None -> RED
    r = LED_BRIGHTNESS;
    g = 0;
    b = 0;
    LOG_AND_SAVE(ENABLE_DEBUG_LED_LOGGING, I, LED_TAG, "LED: RED (no connections)");
  }
  else if (usb_connected && !ble_connected)
  { // USB only -> GREEN
    r = 0;
    g = LED_BRIGHTNESS;
    b = 0;
    LOG_AND_SAVE(ENABLE_DEBUG_LED_LOGGING, I, LED_TAG, "LED: GREEN (USB only)");
  }
  else if (!usb_connected && ble_connected)
  { // BLE only -> YELLOW
    r = LED_BRIGHTNESS;
    g = LED_BRIGHTNESS;
    b = 0;
    LOG_AND_SAVE(ENABLE_DEBUG_LED_LOGGING, I, LED_TAG, "LED: YELLOW (BLE only)");
  }
  else
  { // Both -> BLUE
    r = 0;
    g = 0;
    b = LED_BRIGHTNESS;
    LOG_AND_SAVE(ENABLE_DEBUG_LED_LOGGING, I, LED_TAG, "LED: BLUE (USB + BLE)");
  }
  apply_color(r, g, b);
}

// Auto-detect LED GPIO by testing common pins
static int detect_led_gpio(void)
{
  // Common LED GPIO pins for different ESP32-S3 boards
  const int candidate_pins[] = {
    48,  // ESP32-S3-DevKitC-1 NeoPixel
    38,  // Adafruit QT Py ESP32-S3 NeoPixel  
    2,   // Some boards use GPIO2
    8    // Alternative common pin
  };
  const int num_candidates = sizeof(candidate_pins) / sizeof(candidate_pins[0]);

  LOG_AND_SAVE(true, I, LED_TAG, "Auto-detecting LED GPIO...");

  for (int i = 0; i < num_candidates; i++)
  {
    int gpio = candidate_pins[i];
    LOG_AND_SAVE(true, D, LED_TAG, "Testing GPIO %d", gpio);

    // Try to initialize a NeoPixel on this pin
    simple_neopixel_t test_led;
    esp_err_t err = simple_neopixel_init(&test_led, gpio);
    
    if (err == ESP_OK)
    {
      // Test by setting a brief green pulse
      simple_neopixel_set_rgb(&test_led, 0, 5, 0);
      vTaskDelay(pdMS_TO_TICKS(50));
      simple_neopixel_set_rgb(&test_led, 0, 0, 0);
      
      LOG_AND_SAVE(true, I, LED_TAG, "✓ Detected LED on GPIO %d", gpio);
      
      // Store detected GPIO to NVS
      nvs_handle_t nvs;
      if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK)
      {
        nvs_set_i32(nvs, NVS_KEY_LED_GPIO, gpio);
        nvs_commit(nvs);
        nvs_close(nvs);
        LOG_AND_SAVE(true, I, LED_TAG, "Saved LED GPIO %d to NVS", gpio);
      }
      
      return gpio;
    }
  }

  // No LED detected, return default from Kconfig
  LOG_AND_SAVE(true, W, LED_TAG, "No LED detected, using Kconfig default GPIO %d", CONFIG_M4G_LED_DATA_GPIO);
  return CONFIG_M4G_LED_DATA_GPIO;
}

// Get LED GPIO from NVS or auto-detect
static int get_led_gpio(void)
{
  nvs_handle_t nvs;
  int32_t stored_gpio = -1;

  // Try to read from NVS first
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK)
  {
    esp_err_t err = nvs_get_i32(nvs, NVS_KEY_LED_GPIO, &stored_gpio);
    nvs_close(nvs);
    
    if (err == ESP_OK && stored_gpio >= 0)
    {
      LOG_AND_SAVE(true, I, LED_TAG, "Using stored LED GPIO %d from NVS", (int)stored_gpio);
      return (int)stored_gpio;
    }
  }

  // First boot or no stored value - auto-detect
  return detect_led_gpio();
}

esp_err_t m4g_led_init(void)
{
#if CONFIG_M4G_LED_TYPE_NONE
  LOG_AND_SAVE(ENABLE_DEBUG_LED_LOGGING, I, LED_TAG, "LED disabled by config");
  return ESP_OK;
#elif CONFIG_M4G_LED_TYPE_NEOPIXEL
  // Auto-detect or retrieve stored LED GPIO
  int led_gpio = get_led_gpio();
  
  LOG_AND_SAVE(ENABLE_DEBUG_LED_LOGGING, I, LED_TAG, "Initializing NeoPixel (data GPIO=%d power GPIO=%d)", led_gpio, CONFIG_M4G_LED_POWER_GPIO);

  // Initialize power GPIO if specified
  if (CONFIG_M4G_LED_POWER_GPIO >= 0)
  {
    // Guard shift to avoid warning when GPIO is -1 (disabled)
    uint64_t mask = (CONFIG_M4G_LED_POWER_GPIO >= 0 && CONFIG_M4G_LED_POWER_GPIO < 64)
                        ? (1ULL << CONFIG_M4G_LED_POWER_GPIO)
                        : 0ULL;
    if (mask)
    {
      gpio_config_t pwr_cfg = {
          .pin_bit_mask = mask,
          .mode = GPIO_MODE_OUTPUT,
          .pull_up_en = GPIO_PULLUP_DISABLE,
          .pull_down_en = GPIO_PULLDOWN_DISABLE,
          .intr_type = GPIO_INTR_DISABLE};
      gpio_config(&pwr_cfg);
      gpio_set_level(CONFIG_M4G_LED_POWER_GPIO, 1);
    }
  }

  // Initialize simple NeoPixel driver with auto-detected GPIO
  esp_err_t err = simple_neopixel_init(&rgb_led, led_gpio);
  if (err != ESP_OK)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_LED_LOGGING, E, LED_TAG, "Failed to init NeoPixel on GPIO %d: %s", led_gpio, esp_err_to_name(err));
    return err;
  }

  LOG_AND_SAVE(true, I, LED_TAG, "✓ LED initialized successfully on GPIO %d", led_gpio);
  update_led_state();
  return ESP_OK;
#elif CONFIG_M4G_LED_TYPE_SIMPLE
  LOG_AND_SAVE(ENABLE_DEBUG_LED_LOGGING, I, LED_TAG, "Simple LED type selected (no implementation yet)");
  return ESP_OK;
#endif
}

void m4g_led_set_usb_connected(bool connected)
{
  usb_connected = connected;
  update_led_state();
}

void m4g_led_set_ble_connected(bool connected)
{
  ble_connected = connected;
  update_led_state();
}

void m4g_led_force_color(uint8_t r, uint8_t g, uint8_t b)
{
  apply_color(r, g, b);
}

bool m4g_led_is_usb_connected(void) { return usb_connected; }
bool m4g_led_is_ble_connected(void) { return ble_connected; }

esp_err_t m4g_led_clear_stored_gpio(void)
{
  nvs_handle_t nvs;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
  if (err != ESP_OK)
  {
    LOG_AND_SAVE(true, E, LED_TAG, "Failed to open NVS for clearing GPIO: %s", esp_err_to_name(err));
    return err;
  }

  err = nvs_erase_key(nvs, NVS_KEY_LED_GPIO);
  if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND)
  {
    nvs_commit(nvs);
    nvs_close(nvs);
    LOG_AND_SAVE(true, I, LED_TAG, "Cleared stored LED GPIO - will re-detect on next boot");
    return ESP_OK;
  }

  nvs_close(nvs);
  LOG_AND_SAVE(true, E, LED_TAG, "Failed to clear LED GPIO: %s", esp_err_to_name(err));
  return err;
}
