#include "simple_neopixel.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "SIMPLE_NEOPIXEL";

// WS2812 timing parameters (in nanoseconds, for 800kHz)
#define WS2812_T0H_NS 350   // 0 code, high level time
#define WS2812_T0L_NS 800   // 0 code, low level time
#define WS2812_T1H_NS 700   // 1 code, high level time
#define WS2812_T1L_NS 600   // 1 code, low level time
#define WS2812_RESET_US 280 // Reset code duration (microseconds)

// RMT resolution (10MHz = 100ns per tick)
#define RMT_RESOLUTION_HZ 10000000

// Convert nanoseconds to RMT ticks
#define NS_TO_RMT_TICKS(ns) ((uint32_t)(((uint64_t)(ns) * RMT_RESOLUTION_HZ) / 1000000000ULL))

// WS2812 timing in RMT ticks
#define WS2812_T0H_TICKS NS_TO_RMT_TICKS(WS2812_T0H_NS)
#define WS2812_T0L_TICKS NS_TO_RMT_TICKS(WS2812_T0L_NS)
#define WS2812_T1H_TICKS NS_TO_RMT_TICKS(WS2812_T1H_NS)
#define WS2812_T1L_TICKS NS_TO_RMT_TICKS(WS2812_T1L_NS)

/**
 * @brief Simple RMT encoder for WS2812 protocol
 */
typedef struct
{
  rmt_encoder_t base;
  rmt_encoder_t *bytes_encoder;
  rmt_encoder_t *copy_encoder;
  int state;
  rmt_symbol_word_t reset_code;
} ws2812_encoder_t;

static size_t rmt_encode_ws2812(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                                const void *primary_data, size_t data_size, rmt_encode_state_t *ret_state)
{
  ws2812_encoder_t *ws2812_encoder = __containerof(encoder, ws2812_encoder_t, base);
  rmt_encoder_handle_t bytes_encoder = ws2812_encoder->bytes_encoder;
  rmt_encoder_handle_t copy_encoder = ws2812_encoder->copy_encoder;
  rmt_encode_state_t session_state = 0;
  rmt_encode_state_t state = 0;
  size_t encoded_symbols = 0;

  switch (ws2812_encoder->state)
  {
  case 0: // send RGB data
    encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, primary_data, data_size, &session_state);
    if (session_state & RMT_ENCODING_COMPLETE)
    {
      ws2812_encoder->state = 1; // switch to next state when current encoding session finished
    }
    if (session_state & RMT_ENCODING_MEM_FULL)
    {
      state |= RMT_ENCODING_MEM_FULL;
      goto out; // yield if there's no free space for encoding artifacts
    }
    // fall-through
  case 1: // send reset code
    encoded_symbols += copy_encoder->encode(copy_encoder, channel, &ws2812_encoder->reset_code,
                                            sizeof(ws2812_encoder->reset_code), &session_state);
    if (session_state & RMT_ENCODING_COMPLETE)
    {
      ws2812_encoder->state = 0; // back to the initial encoding session
      state |= RMT_ENCODING_COMPLETE;
    }
    if (session_state & RMT_ENCODING_MEM_FULL)
    {
      state |= RMT_ENCODING_MEM_FULL;
      goto out; // yield if there's no free space for encoding artifacts
    }
  }
out:
  *ret_state = state;
  return encoded_symbols;
}

static esp_err_t rmt_del_ws2812_encoder(rmt_encoder_t *encoder)
{
  ws2812_encoder_t *ws2812_encoder = __containerof(encoder, ws2812_encoder_t, base);
  rmt_del_encoder(ws2812_encoder->bytes_encoder);
  rmt_del_encoder(ws2812_encoder->copy_encoder);
  free(ws2812_encoder);
  return ESP_OK;
}

static esp_err_t rmt_ws2812_encoder_reset(rmt_encoder_t *encoder)
{
  ws2812_encoder_t *ws2812_encoder = __containerof(encoder, ws2812_encoder_t, base);
  rmt_encoder_reset(ws2812_encoder->bytes_encoder);
  rmt_encoder_reset(ws2812_encoder->copy_encoder);
  ws2812_encoder->state = 0;
  return ESP_OK;
}

static esp_err_t ws2812_encoder_new(rmt_encoder_handle_t *ret_encoder)
{
  esp_err_t ret = ESP_OK;
  ws2812_encoder_t *ws2812_encoder = NULL;

  ws2812_encoder = calloc(1, sizeof(ws2812_encoder_t));
  if (!ws2812_encoder)
  {
    return ESP_ERR_NO_MEM;
  }

  ws2812_encoder->base.encode = rmt_encode_ws2812;
  ws2812_encoder->base.del = rmt_del_ws2812_encoder;
  ws2812_encoder->base.reset = rmt_ws2812_encoder_reset;

  // Create byte encoder for RGB data
  rmt_bytes_encoder_config_t bytes_encoder_config = {
      .bit0 = {
          .level0 = 1,
          .duration0 = WS2812_T0H_TICKS,
          .level1 = 0,
          .duration1 = WS2812_T0L_TICKS,
      },
      .bit1 = {
          .level0 = 1,
          .duration0 = WS2812_T1H_TICKS,
          .level1 = 0,
          .duration1 = WS2812_T1L_TICKS,
      },
      .flags.msb_first = 1, // WS2812 uses MSB first
  };
  ret = rmt_new_bytes_encoder(&bytes_encoder_config, &ws2812_encoder->bytes_encoder);
  if (ret != ESP_OK)
  {
    goto err;
  }

  // Create copy encoder for reset signal
  rmt_copy_encoder_config_t copy_encoder_config = {};
  ret = rmt_new_copy_encoder(&copy_encoder_config, &ws2812_encoder->copy_encoder);
  if (ret != ESP_OK)
  {
    goto err;
  }

  // Prepare reset code (long low period)
  uint32_t reset_ticks = RMT_RESOLUTION_HZ / 1000000 * WS2812_RESET_US; // Convert microseconds to ticks
  ws2812_encoder->reset_code = (rmt_symbol_word_t){
      .level0 = 0,
      .duration0 = reset_ticks,
      .level1 = 0,
      .duration1 = 0,
  };

  *ret_encoder = &ws2812_encoder->base;
  return ESP_OK;

err:
  if (ws2812_encoder)
  {
    if (ws2812_encoder->bytes_encoder)
    {
      rmt_del_encoder(ws2812_encoder->bytes_encoder);
    }
    if (ws2812_encoder->copy_encoder)
    {
      rmt_del_encoder(ws2812_encoder->copy_encoder);
    }
    free(ws2812_encoder);
  }
  return ret;
}

esp_err_t simple_neopixel_init(simple_neopixel_t *led, int gpio_num)
{
  if (!led || gpio_num < 0)
  {
    return ESP_ERR_INVALID_ARG;
  }

  // Initialize structure
  memset(led, 0, sizeof(simple_neopixel_t));
  led->gpio_num = gpio_num;

  // Create RMT TX channel
  rmt_tx_channel_config_t tx_chan_config = {
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .gpio_num = gpio_num,
      .mem_block_symbols = 64,
      .resolution_hz = RMT_RESOLUTION_HZ,
      .trans_queue_depth = 4,
  };

  esp_err_t ret = rmt_new_tx_channel(&tx_chan_config, &led->rmt_chan);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to create RMT TX channel: %s", esp_err_to_name(ret));
    return ret;
  }

  // Create WS2812 encoder
  ret = ws2812_encoder_new(&led->encoder);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to create WS2812 encoder: %s", esp_err_to_name(ret));
    rmt_del_channel(led->rmt_chan);
    return ret;
  }

  // Enable RMT channel
  ret = rmt_enable(led->rmt_chan);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to enable RMT channel: %s", esp_err_to_name(ret));
    rmt_del_encoder(led->encoder);
    rmt_del_channel(led->rmt_chan);
    return ret;
  }

  // Setup transmission config
  led->tx_config = (rmt_transmit_config_t){
      .loop_count = 0, // no loop
  };

  ESP_LOGI(TAG, "Simple NeoPixel initialized on GPIO %d", gpio_num);
  return ESP_OK;
}

esp_err_t simple_neopixel_set_rgb(simple_neopixel_t *led, uint8_t r, uint8_t g, uint8_t b)
{
  if (!led || !led->rmt_chan || !led->encoder)
  {
    return ESP_ERR_INVALID_ARG;
  }

  // WS2812 expects GRB format (Green, Red, Blue)
  uint8_t rgb_data[3] = {g, r, b};

  esp_err_t ret = rmt_transmit(led->rmt_chan, led->encoder, rgb_data, sizeof(rgb_data), &led->tx_config);
  if (ret != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to transmit RGB data: %s", esp_err_to_name(ret));
    return ret;
  }

  return ESP_OK;
}

esp_err_t simple_neopixel_clear(simple_neopixel_t *led)
{
  return simple_neopixel_set_rgb(led, 0, 0, 0);
}

void simple_neopixel_deinit(simple_neopixel_t *led)
{
  if (!led)
  {
    return;
  }

  if (led->rmt_chan)
  {
    rmt_disable(led->rmt_chan);
    rmt_del_channel(led->rmt_chan);
    led->rmt_chan = NULL;
  }

  if (led->encoder)
  {
    rmt_del_encoder(led->encoder);
    led->encoder = NULL;
  }

  ESP_LOGI(TAG, "Simple NeoPixel deinitialized");
}