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

#define LED_BRIGHTNESS 30  // Increased from 10 for better visibility
#define RGB_LED_NUM_LEDS 1
#define LED_FLASH_PERIOD_MS 500  // Flash every 500ms when advertising

// Kconfig parameters (defined in main/Kconfig; legacy Kconfig.projbuild removed)

static simple_neopixel_t rgb_led;
static bool usb_connected = false;
static bool ble_connected = false;
static bool espnow_connected = false;  // RIGHT side connection for split keyboard
static bool ble_advertising = false;   // BLE is advertising but not connected
static TaskHandle_t led_task_handle = NULL;
static bool flash_state = false;  // Alternates between base color and blue

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

// Get the base LED color based on connections (without BLE advertising flash)
static void get_base_led_color(uint8_t *r, uint8_t *g, uint8_t *b)
{
  *r = 0; *g = 0; *b = 0;
  
#if defined(CONFIG_M4G_SPLIT_ROLE_LEFT)
  // LEFT side split keyboard LED logic
  if (!usb_connected && !espnow_connected && !ble_connected)
  {
    // No connections at all -> RED
    r = LED_BRIGHTNESS;
    g = 0;
    b = 0;
    LOG_AND_SAVE(ENABLE_DEBUG_LED_LOGGING, I, LED_TAG, "LED: RED (no connections)");
  }
  else if (usb_connected && !espnow_connected && !ble_connected)
  {
    // Local USB only (left keyboard detected) -> MAGENTA
    r = LED_BRIGHTNESS;
    g = 0;
    b = LED_BRIGHTNESS;
    LOG_AND_SAVE(ENABLE_DEBUG_LED_LOGGING, I, LED_TAG, "LED: MAGENTA (left USB only)");
  }
  else if (usb_connected && !espnow_connected && ble_connected)
  {
    // Local USB + BLE but no right side -> MAGENTA (keep magenta)
    r = LED_BRIGHTNESS;
    g = 0;
    b = LED_BRIGHTNESS;
    LOG_AND_SAVE(ENABLE_DEBUG_LED_LOGGING, I, LED_TAG, "LED: MAGENTA (left USB + BLE, no right)");
  }
  else if (usb_connected && espnow_connected && !ble_connected)
  {
    // Local USB + Right side connected -> GREEN
    r = 0;
    g = LED_BRIGHTNESS;
    b = 0;
    LOG_AND_SAVE(ENABLE_DEBUG_LED_LOGGING, I, LED_TAG, "LED: GREEN (both halves, no BLE)");
  }
  else if (usb_connected && espnow_connected && ble_connected)
  {
    // All connected (left USB + right + BLE) -> BLUE
    r = 0;
    g = 0;
    b = LED_BRIGHTNESS;
    LOG_AND_SAVE(ENABLE_DEBUG_LED_LOGGING, I, LED_TAG, "LED: BLUE (full split + BLE)");
  }
  else
  {
    // Fallback for other combinations -> YELLOW
    r = LED_BRIGHTNESS;
    g = LED_BRIGHTNESS;
    b = 0;
    LOG_AND_SAVE(ENABLE_DEBUG_LED_LOGGING, W, LED_TAG, "LED: YELLOW (unexpected state)");
  }
#else
  // STANDALONE or RIGHT side - original logic
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
#endif
}

static void update_led_state(void)
{
  uint8_t r = 0, g = 0, b = 0;
  get_base_led_color(&r, &g, &b);
  
  // If BLE is advertising (not connected), flash between base color and blue
  if (ble_advertising && !ble_connected)
  {
    if (flash_state)
    {
      // Flash blue to indicate BLE advertising
      r = 0;
      g = 0;
      b = LED_BRIGHTNESS;
    }
    // else: use base color (already set)
  }
  
  apply_color(r, g, b);
}

// LED flashing task for BLE advertising indication
static void led_flash_task(void *pvParameters)
{
  (void)pvParameters;
  
  while (1)
  {
    vTaskDelay(pdMS_TO_TICKS(LED_FLASH_PERIOD_MS));
    
    // Toggle flash state if BLE is advertising
    if (ble_advertising && !ble_connected)
    {
      flash_state = !flash_state;
      update_led_state();
    }
    else
    {
      // Not advertising - ensure flash state is reset
      if (flash_state)
      {
        flash_state = false;
        update_led_state();
      }
    }
  }
}

// Board-specific LED configurations
typedef struct {
  int data_gpio;
  int power_gpio;  // -1 if no power pin needed
  const char *board_name;
} led_config_t;

// Auto-detect LED GPIO by testing common pins
static int detect_led_gpio(void)
{
  // Common LED configurations for different ESP32-S3 boards
  // User confirmed: QT Py uses GPIO 39=data, GPIO 38=power
  const led_config_t candidate_configs[] = {
    {39, 38, "Adafruit QT Py ESP32-S3"},   // CONFIRMED: data=39, power=38
    {48, -1, "ESP32-S3-DevKitC-1"},        // DevKit NeoPixel
    {38, -1, "QT Py alt (GPIO38 only)"},   // Fallback
    { 2, -1, "Generic board (GPIO2)"},     // Some boards use GPIO2
  };
  const int num_candidates = sizeof(candidate_configs) / sizeof(candidate_configs[0]);

  LOG_AND_SAVE(true, I, LED_TAG, "Auto-detecting LED GPIO...");

  for (int i = 0; i < num_candidates; i++)
  {
    const led_config_t *config = &candidate_configs[i];
    LOG_AND_SAVE(true, D, LED_TAG, "Testing %s: data GPIO %d, power GPIO %d", 
                 config->board_name, config->data_gpio, config->power_gpio);

    // Enable power GPIO if needed (e.g., QT Py requires GPIO 8 HIGH)
    if (config->power_gpio >= 0)
    {
      gpio_config_t pwr_cfg = {
        .pin_bit_mask = (1ULL << config->power_gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
      };
      gpio_config(&pwr_cfg);
      gpio_set_level(config->power_gpio, 1);  // Enable power
      vTaskDelay(pdMS_TO_TICKS(10));  // Give it time to power up
    }

    // Try to initialize a NeoPixel on this pin
    simple_neopixel_t test_led;
    esp_err_t err = simple_neopixel_init(&test_led, config->data_gpio);
    
    if (err == ESP_OK)
    {
      // Test with a bright visible pulse (magenta = success detection)
      simple_neopixel_set_rgb(&test_led, 50, 0, 50);  // Bright magenta
      vTaskDelay(pdMS_TO_TICKS(300));
      simple_neopixel_set_rgb(&test_led, 0, 0, 0);
      
      LOG_AND_SAVE(true, I, LED_TAG, "✓ Detected LED: %s (data=%d, power=%d)", 
                   config->board_name, config->data_gpio, config->power_gpio);
      
      // Store both GPIO pins to NVS
      nvs_handle_t nvs;
      if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK)
      {
        nvs_set_i32(nvs, NVS_KEY_LED_GPIO, config->data_gpio);
        nvs_set_i32(nvs, "led_power", config->power_gpio);
        nvs_commit(nvs);
        nvs_close(nvs);
        LOG_AND_SAVE(true, I, LED_TAG, "Saved LED config to NVS");
      }
      
      return config->data_gpio;
    }
    
    // Failed - disable power GPIO if we enabled it
    if (config->power_gpio >= 0)
    {
      gpio_set_level(config->power_gpio, 0);
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
  int32_t detection_version = 0;
  const int32_t CURRENT_DETECTION_VERSION = 2;  // Increment to force re-detection

  // Try to read from NVS first
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK)
  {
    esp_err_t err = nvs_get_i32(nvs, NVS_KEY_LED_GPIO, &stored_gpio);
    nvs_get_i32(nvs, "led_detect_ver", &detection_version);  // Check detection version
    nvs_close(nvs);
    
    // Use stored value only if detection version matches and GPIO is valid
    if (err == ESP_OK && stored_gpio >= 0 && detection_version == CURRENT_DETECTION_VERSION)
    {
      LOG_AND_SAVE(true, I, LED_TAG, "Using stored LED GPIO %d from NVS (v%d)", (int)stored_gpio, (int)detection_version);
      return (int)stored_gpio;
    }
    
    if (detection_version != CURRENT_DETECTION_VERSION)
    {
      LOG_AND_SAVE(true, I, LED_TAG, "Detection version changed (v%d -> v%d), re-detecting...", 
                   (int)detection_version, (int)CURRENT_DETECTION_VERSION);
    }
  }

  // First boot, no stored value, or detection algorithm updated - auto-detect
  int detected = detect_led_gpio();
  
  // Store detection version along with GPIO
  if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK)
  {
    nvs_set_i32(nvs, "led_detect_ver", CURRENT_DETECTION_VERSION);
    nvs_commit(nvs);
    nvs_close(nvs);
  }
  
  return detected;
}

esp_err_t m4g_led_init(void)
{
#if CONFIG_M4G_LED_TYPE_NONE
  LOG_AND_SAVE(ENABLE_DEBUG_LED_LOGGING, I, LED_TAG, "LED disabled by config");
  return ESP_OK;
#elif CONFIG_M4G_LED_TYPE_NEOPIXEL
  // Auto-detect or retrieve stored LED GPIO
  int led_gpio = get_led_gpio();
  
  // Try to get stored power GPIO, fallback to Kconfig
  int power_gpio = CONFIG_M4G_LED_POWER_GPIO;
  nvs_handle_t nvs;
  if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK)
  {
    int32_t stored_power = -1;
    if (nvs_get_i32(nvs, "led_power", &stored_power) == ESP_OK)
    {
      power_gpio = stored_power;
    }
    nvs_close(nvs);
  }
  
  LOG_AND_SAVE(ENABLE_DEBUG_LED_LOGGING, I, LED_TAG, "Initializing NeoPixel (data GPIO=%d power GPIO=%d)", led_gpio, power_gpio);

  // Initialize power GPIO if specified (e.g., QT Py needs GPIO 8)
  if (power_gpio >= 0)
  {
    gpio_config_t pwr_cfg = {
        .pin_bit_mask = (1ULL << power_gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&pwr_cfg);
    gpio_set_level(power_gpio, 1);  // Enable LED power
    vTaskDelay(pdMS_TO_TICKS(10));  // Give it time to power up
  }

  // Initialize simple NeoPixel driver with auto-detected GPIO
  esp_err_t err = simple_neopixel_init(&rgb_led, led_gpio);
  if (err != ESP_OK)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_LED_LOGGING, E, LED_TAG, "Failed to init NeoPixel on GPIO %d: %s", led_gpio, esp_err_to_name(err));
    return err;
  }

  LOG_AND_SAVE(true, I, LED_TAG, "✓ LED initialized successfully (data=%d, power=%d)", led_gpio, power_gpio);
  
  // Start LED flashing task for BLE advertising indication
  if (led_task_handle == NULL)
  {
    xTaskCreate(led_flash_task, "led_flash", 2048, NULL, 5, &led_task_handle);
  }
  
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
  if (connected)
  {
    ble_advertising = false;  // No longer advertising once connected
  }
  update_led_state();
}

void m4g_led_set_espnow_connected(bool connected)
{
  espnow_connected = connected;
  update_led_state();
}

void m4g_led_set_ble_advertising(bool advertising)
{
  ble_advertising = advertising;
  update_led_state();
}

void m4g_led_force_color(uint8_t r, uint8_t g, uint8_t b)
{
  apply_color(r, g, b);
}

bool m4g_led_is_usb_connected(void) { return usb_connected; }
bool m4g_led_is_ble_connected(void) { return ble_connected; }
bool m4g_led_is_espnow_connected(void) { return espnow_connected; }
bool m4g_led_is_ble_advertising(void) { return ble_advertising; }

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
