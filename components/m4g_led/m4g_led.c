#include "m4g_led.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "m4g_logging.h"
#include "sdkconfig.h"

static const char *LED_TAG = "M4G-LED";

#define LED_BRIGHTNESS 10
#define RGB_LED_NUM_LEDS 1

// Kconfig parameters (defined in main/Kconfig; legacy Kconfig.projbuild removed)

static led_strip_handle_t rgb_strip = NULL;
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
  if (!rgb_strip)
    return;
  led_strip_set_pixel(rgb_strip, 0, r, g, b);
  led_strip_refresh(rgb_strip);
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

esp_err_t m4g_led_init(void)
{
#if CONFIG_M4G_LED_TYPE_NONE
  LOG_AND_SAVE(ENABLE_DEBUG_LED_LOGGING, I, LED_TAG, "LED disabled by config");
  return ESP_OK;
#elif CONFIG_M4G_LED_TYPE_NEOPIXEL
  LOG_AND_SAVE(ENABLE_DEBUG_LED_LOGGING, I, LED_TAG, "Initializing NeoPixel (data GPIO=%d power GPIO=%d)", CONFIG_M4G_LED_DATA_GPIO, CONFIG_M4G_LED_POWER_GPIO);
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
  led_strip_config_t strip_config = {
      .strip_gpio_num = CONFIG_M4G_LED_DATA_GPIO,
      .max_leds = RGB_LED_NUM_LEDS,
  };
  led_strip_rmt_config_t rmt_config = {
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = 10 * 1000 * 1000,
      .mem_block_symbols = 0,
      .flags = {.with_dma = false},
  };
  esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &rgb_strip);
  if (err != ESP_OK)
  {
    LOG_AND_SAVE(true, E, LED_TAG, "Failed to init LED strip: %s", esp_err_to_name(err));
    return err;
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
  update_led_state();
}

void m4g_led_force_color(uint8_t r, uint8_t g, uint8_t b)
{
  apply_color(r, g, b);
}

bool m4g_led_is_usb_connected(void) { return usb_connected; }
bool m4g_led_is_ble_connected(void) { return ble_connected; }
