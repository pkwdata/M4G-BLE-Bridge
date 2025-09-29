#pragma once

#include "esp_err.h"
#include "driver/rmt_tx.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  /**
   * @brief Simple NeoPixel (WS2812) driver for single LED
   *
   * Optimized for single LED control using ESP32-S3 RMT peripheral.
   * Supports standard WS2812 timing (800kHz, GRB format).
   */
  typedef struct
  {
    int gpio_num;
    rmt_channel_handle_t rmt_chan;
    rmt_encoder_handle_t encoder;
    rmt_transmit_config_t tx_config;
  } simple_neopixel_t;

  /**
   * @brief Initialize simple NeoPixel driver
   *
   * @param led Pointer to NeoPixel handle
   * @param gpio_num GPIO pin number for data line
   * @return ESP_OK on success, error code on failure
   */
  esp_err_t simple_neopixel_init(simple_neopixel_t *led, int gpio_num);

  /**
   * @brief Set RGB color and immediately update LED
   *
   * @param led Pointer to initialized NeoPixel handle
   * @param r Red component (0-255)
   * @param g Green component (0-255)
   * @param b Blue component (0-255)
   * @return ESP_OK on success, error code on failure
   */
  esp_err_t simple_neopixel_set_rgb(simple_neopixel_t *led, uint8_t r, uint8_t g, uint8_t b);

  /**
   * @brief Turn off LED (set to black)
   *
   * @param led Pointer to initialized NeoPixel handle
   * @return ESP_OK on success, error code on failure
   */
  esp_err_t simple_neopixel_clear(simple_neopixel_t *led);

  /**
   * @brief Cleanup NeoPixel resources
   *
   * @param led Pointer to initialized NeoPixel handle
   */
  void simple_neopixel_deinit(simple_neopixel_t *led);

#ifdef __cplusplus
}
#endif