#include "m4g_espnow.h"
#include "m4g_logging.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "M4G-ESPNOW";

// ESP-NOW configuration
static m4g_espnow_config_t s_config = {0};
static bool s_initialized = false;
static uint8_t s_peer_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // Broadcast by default
static uint32_t s_tx_sequence = 0;
static uint32_t s_rx_sequence = 0;
static TickType_t s_last_peer_rx_time = 0;

// Statistics
static m4g_espnow_stats_t s_stats = {0};

// Receive queue for processing in task
#define ESPNOW_QUEUE_SIZE 10
static QueueHandle_t s_espnow_rx_queue = NULL;

typedef struct
{
  uint8_t mac[6];
  uint8_t data[sizeof(m4g_espnow_hid_packet_t)];
  int data_len;
  int rssi;
} espnow_rx_event_t;

/**
 * @brief ESP-NOW send callback (ESP-IDF v6.0 signature)
 */
static void espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
  (void)tx_info; // Unused in v6.0
  
  if (status == ESP_NOW_SEND_SUCCESS)
  {
    if (ENABLE_DEBUG_USB_LOGGING)
    {
      LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, D, TAG, "ESP-NOW send success");
    }
  }
  else
  {
    s_stats.send_failures++;
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, W, TAG, "ESP-NOW send failed");
  }
}

/**
 * @brief ESP-NOW receive callback
 */
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int data_len)
{
  if (!s_initialized || !data || data_len == 0)
  {
    return;
  }

  // Queue the received packet for processing
  espnow_rx_event_t evt;
  memcpy(evt.mac, recv_info->src_addr, 6);
  memcpy(evt.data, data, (data_len > sizeof(evt.data)) ? sizeof(evt.data) : data_len);
  evt.data_len = data_len;
  evt.rssi = recv_info->rx_ctrl->rssi;

  if (xQueueSend(s_espnow_rx_queue, &evt, 0) != pdTRUE)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, W, TAG, "ESP-NOW RX queue full, dropping packet");
  }
}

/**
 * @brief Process received ESP-NOW packet
 */
static void process_rx_packet(const espnow_rx_event_t *evt)
{
  if (evt->data_len < 1)
  {
    return;
  }

  s_stats.packets_received++;
  s_stats.last_rssi = evt->rssi;
  s_last_peer_rx_time = xTaskGetTickCount();

  uint8_t pkt_type = evt->data[0];

  switch (pkt_type)
  {
  case M4G_ESPNOW_PKT_HID_REPORT:
  {
    if (evt->data_len < sizeof(m4g_espnow_hid_packet_t))
    {
      LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, W, TAG, "HID packet too small: %d bytes", evt->data_len);
      return;
    }

    m4g_espnow_hid_packet_t *pkt = (m4g_espnow_hid_packet_t *)evt->data;

    // Check sequence for packet loss
    if (s_rx_sequence > 0 && pkt->sequence != (s_rx_sequence + 1))
    {
      uint32_t lost = pkt->sequence - s_rx_sequence - 1;
      s_stats.packets_lost += lost;
      LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, W, TAG, "Packet loss detected: %lu packets lost", lost);
    }
    s_rx_sequence = pkt->sequence;

    // Update peer MAC if this is first contact
    if (memcmp(s_peer_mac, evt->mac, 6) != 0)
    {
      memcpy(s_peer_mac, evt->mac, 6);
      LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, TAG, "Peer MAC updated: " MACSTR, MAC2STR(s_peer_mac));
    }

    if (ENABLE_DEBUG_KEYPRESS_LOGGING)
    {
      LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, TAG,
                   "RX HID: slot=%d len=%d chara=%d seq=%lu rssi=%d",
                   pkt->slot, pkt->report_len, pkt->is_charachorder, pkt->sequence, evt->rssi);
    }

    // Invoke callback if registered
    if (s_config.rx_callback && pkt->report_len > 0)
    {
      s_config.rx_callback(pkt->slot, pkt->report, pkt->report_len, pkt->is_charachorder);
    }
    break;
  }

  case M4G_ESPNOW_PKT_HEARTBEAT:
  {
    if (ENABLE_DEBUG_USB_LOGGING)
    {
      LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, D, TAG, "Heartbeat from peer (RSSI: %d)", evt->rssi);
    }
    break;
  }

  default:
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, W, TAG, "Unknown packet type: 0x%02X", pkt_type);
    break;
  }
}

/**
 * @brief ESP-NOW receive processing task
 */
static void espnow_rx_task(void *arg)
{
  (void)arg;

  espnow_rx_event_t evt;
  while (1)
  {
    if (xQueueReceive(s_espnow_rx_queue, &evt, portMAX_DELAY) == pdTRUE)
    {
      process_rx_packet(&evt);
    }
  }
}

esp_err_t m4g_espnow_init(const m4g_espnow_config_t *config)
{
  if (s_initialized)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, W, TAG, "Already initialized");
    return ESP_OK;
  }

  if (!config)
  {
    return ESP_ERR_INVALID_ARG;
  }

  memcpy(&s_config, config, sizeof(m4g_espnow_config_t));

  // Initialize WiFi in STA mode (required for ESP-NOW)
  esp_err_t ret = esp_netif_init();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, E, TAG, "netif init failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = esp_event_loop_create_default();
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, E, TAG, "event loop create failed: %s", esp_err_to_name(ret));
    return ret;
  }

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ret = esp_wifi_init(&cfg);
  if (ret != ESP_OK)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, E, TAG, "WiFi init failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = esp_wifi_set_mode(WIFI_MODE_STA);
  if (ret != ESP_OK)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, E, TAG, "WiFi set mode failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = esp_wifi_set_storage(WIFI_STORAGE_RAM);
  if (ret != ESP_OK)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, E, TAG, "WiFi set storage failed: %s", esp_err_to_name(ret));
    return ret;
  }

  // For RIGHT side, override MAC address BEFORE starting WiFi to avoid conflicts with LEFT
  #if defined(CONFIG_M4G_SPLIT_ROLE_RIGHT)
  uint8_t custom_mac[6];
  esp_read_mac(custom_mac, ESP_MAC_WIFI_STA);
  custom_mac[5] = (custom_mac[5] + 1) & 0xFF;  // Increment last byte to make unique
  ret = esp_wifi_set_mac(WIFI_IF_STA, custom_mac);
  if (ret == ESP_OK)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, TAG, "RIGHT: Set custom MAC=" MACSTR " (original +1)", MAC2STR(custom_mac));
  }
  else
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, W, TAG, "Failed to set custom MAC: %s", esp_err_to_name(ret));
  }
  #endif

  ret = esp_wifi_start();
  if (ret != ESP_OK)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, E, TAG, "WiFi start failed: %s", esp_err_to_name(ret));
    return ret;
  }

  // Set WiFi channel
  ret = esp_wifi_set_channel(config->channel, WIFI_SECOND_CHAN_NONE);
  if (ret != ESP_OK)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, W, TAG, "WiFi set channel failed: %s", esp_err_to_name(ret));
  }

  // Initialize ESP-NOW
  ret = esp_now_init();
  if (ret != ESP_OK)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, E, TAG, "ESP-NOW init failed: %s", esp_err_to_name(ret));
    esp_wifi_stop();
    return ret;
  }

  // Register callbacks
  ret = esp_now_register_send_cb(espnow_send_cb);
  if (ret != ESP_OK)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, E, TAG, "ESP-NOW register send callback failed: %s", esp_err_to_name(ret));
    esp_now_deinit();
    esp_wifi_stop();
    return ret;
  }

  ret = esp_now_register_recv_cb(espnow_recv_cb);
  if (ret != ESP_OK)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, E, TAG, "ESP-NOW register recv callback failed: %s", esp_err_to_name(ret));
    esp_now_deinit();
    esp_wifi_stop();
    return ret;
  }

  // Set PMK if provided
  if (config->use_pmk)
  {
    ret = esp_now_set_pmk(config->pmk);
    if (ret != ESP_OK)
    {
      LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, W, TAG, "ESP-NOW set PMK failed: %s", esp_err_to_name(ret));
    }
  }

  // Add peer (broadcast or specific MAC)
  esp_now_peer_info_t peer = {0};
  peer.channel = config->channel;
  peer.ifidx = WIFI_IF_STA;

  if (config->peer_mac[0] == 0 && config->peer_mac[1] == 0)
  {
    // Use broadcast - ESP-NOW does NOT support encryption on broadcast addresses
    memset(peer.peer_addr, 0xFF, 6);
    memcpy(s_peer_mac, peer.peer_addr, 6);
    peer.encrypt = false;  // Force disable encryption for broadcast
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, TAG, "Using broadcast peer (encryption disabled for broadcast)");
  }
  else
  {
    // Use specific peer MAC - encryption can be enabled
    memcpy(peer.peer_addr, config->peer_mac, 6);
    memcpy(s_peer_mac, config->peer_mac, 6);
    peer.encrypt = config->use_pmk;
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, TAG, "Using specific peer MAC (encrypt=%d)", peer.encrypt);
  }

  ret = esp_now_add_peer(&peer);
  if (ret != ESP_OK && ret != ESP_ERR_ESPNOW_EXIST)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, E, TAG, "ESP-NOW add peer failed: %s", esp_err_to_name(ret));
    esp_now_deinit();
    esp_wifi_stop();
    return ret;
  }

  // Create RX queue and task
  s_espnow_rx_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(espnow_rx_event_t));
  if (!s_espnow_rx_queue)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, E, TAG, "Failed to create RX queue");
    esp_now_deinit();
    esp_wifi_stop();
    return ESP_ERR_NO_MEM;
  }

  xTaskCreate(espnow_rx_task, "espnow_rx", 4096, NULL, 5, NULL);

  s_initialized = true;
  s_tx_sequence = 0;
  s_rx_sequence = 0;

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, TAG,
               "ESP-NOW initialized (role=%s, channel=%d, MAC=" MACSTR ", peer=" MACSTR ")",
               config->role == M4G_ESPNOW_ROLE_LEFT ? "LEFT" : "RIGHT",
               config->channel, MAC2STR(mac), MAC2STR(s_peer_mac));

  return ESP_OK;
}

esp_err_t m4g_espnow_send_hid_report(uint8_t slot, const uint8_t *report, size_t len, bool is_charachorder)
{
  if (!s_initialized)
  {
    return ESP_ERR_INVALID_STATE;
  }

  if (!report || len == 0 || len > M4G_ESPNOW_MAX_HID_SIZE)
  {
    return ESP_ERR_INVALID_ARG;
  }

  m4g_espnow_hid_packet_t pkt = {0};
  pkt.type = M4G_ESPNOW_PKT_HID_REPORT;
  pkt.slot = slot;
  pkt.is_charachorder = is_charachorder ? 1 : 0;
  pkt.report_len = len;
  pkt.sequence = ++s_tx_sequence;
  memcpy(pkt.report, report, len);

  esp_err_t ret = esp_now_send(s_peer_mac, (uint8_t *)&pkt, sizeof(pkt));
  if (ret == ESP_OK)
  {
    s_stats.packets_sent++;

    if (ENABLE_DEBUG_KEYPRESS_LOGGING)
    {
      LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, TAG,
                   "TX HID: slot=%d len=%zu chara=%d seq=%lu",
                   slot, len, is_charachorder, pkt.sequence);
    }
  }
  else
  {
    s_stats.send_failures++;
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, W, TAG, "ESP-NOW send failed: %s", esp_err_to_name(ret));
  }

  return ret;
}

bool m4g_espnow_is_peer_connected(void)
{
  if (!s_initialized)
  {
    return false;
  }

  // Consider peer connected if we received data within last 5 seconds
  TickType_t elapsed = xTaskGetTickCount() - s_last_peer_rx_time;
  return (elapsed < pdMS_TO_TICKS(5000));
}

void m4g_espnow_get_stats(m4g_espnow_stats_t *out)
{
  if (out)
  {
    memcpy(out, &s_stats, sizeof(m4g_espnow_stats_t));
  }
}

esp_err_t m4g_espnow_deinit(void)
{
  if (!s_initialized)
  {
    return ESP_OK;
  }

  esp_now_deinit();
  esp_wifi_stop();

  if (s_espnow_rx_queue)
  {
    vQueueDelete(s_espnow_rx_queue);
    s_espnow_rx_queue = NULL;
  }

  s_initialized = false;
  LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, TAG, "ESP-NOW deinitialized");

  return ESP_OK;
}
