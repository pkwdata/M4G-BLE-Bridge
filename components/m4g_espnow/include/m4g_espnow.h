#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief ESP-NOW packet types for split keyboard communication
 */
typedef enum
{
  M4G_ESPNOW_PKT_HID_REPORT = 0x01,  // HID report from right side
  M4G_ESPNOW_PKT_HEARTBEAT = 0x02,   // Keepalive
  M4G_ESPNOW_PKT_STATUS = 0x03,      // Status update
} m4g_espnow_packet_type_t;

/**
 * @brief Maximum HID report size to transmit over ESP-NOW
 */
#define M4G_ESPNOW_MAX_HID_SIZE 64

/**
 * @brief ESP-NOW packet structure for HID reports
 */
typedef struct __attribute__((packed))
{
  uint8_t type;                          // Packet type (m4g_espnow_packet_type_t)
  uint8_t slot;                          // USB slot identifier (0-1)
  uint8_t is_charachorder;               // 1 if CharaChorder device, 0 otherwise
  uint8_t report_len;                    // Actual HID report length
  uint8_t report[M4G_ESPNOW_MAX_HID_SIZE]; // HID report data
  uint32_t sequence;                     // Sequence number for packet loss detection
} m4g_espnow_hid_packet_t;

/**
 * @brief Callback for received HID reports from peer
 * 
 * @param slot USB slot identifier
 * @param report Pointer to HID report data
 * @param len Length of HID report
 * @param is_charachorder True if from CharaChorder device
 */
typedef void (*m4g_espnow_rx_cb_t)(uint8_t slot, const uint8_t *report, size_t len, bool is_charachorder);

/**
 * @brief ESP-NOW role configuration
 */
typedef enum
{
  M4G_ESPNOW_ROLE_LEFT,   // Left side (receiver + BLE transmitter)
  M4G_ESPNOW_ROLE_RIGHT,  // Right side (USB receiver + ESP-NOW transmitter)
} m4g_espnow_role_t;

/**
 * @brief ESP-NOW configuration
 */
typedef struct
{
  m4g_espnow_role_t role;       // Device role (left or right)
  m4g_espnow_rx_cb_t rx_callback; // Callback for received HID reports
  uint8_t peer_mac[6];          // Peer MAC address (if known), use {0} for broadcast initially
  uint8_t channel;              // WiFi channel (1-13, default 1)
  bool use_pmk;                 // Use Primary Master Key for encrypted communication
  uint8_t pmk[16];              // Primary Master Key (if use_pmk is true)
} m4g_espnow_config_t;

/**
 * @brief Initialize ESP-NOW subsystem
 * 
 * @param config Configuration structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t m4g_espnow_init(const m4g_espnow_config_t *config);

/**
 * @brief Send HID report to peer via ESP-NOW
 * 
 * @param slot USB slot identifier
 * @param report Pointer to HID report data
 * @param len Length of HID report
 * @param is_charachorder True if from CharaChorder device
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t m4g_espnow_send_hid_report(uint8_t slot, const uint8_t *report, size_t len, bool is_charachorder);

/**
 * @brief Check if peer is connected and responsive
 * 
 * @return True if peer responded within timeout, false otherwise
 */
bool m4g_espnow_is_peer_connected(void);

/**
 * @brief Get ESP-NOW statistics
 */
typedef struct
{
  uint32_t packets_sent;
  uint32_t packets_received;
  uint32_t send_failures;
  uint32_t packets_lost;  // Based on sequence gaps
  int8_t last_rssi;
} m4g_espnow_stats_t;

/**
 * @brief Get ESP-NOW communication statistics
 * 
 * @param out Pointer to stats structure to fill
 */
void m4g_espnow_get_stats(m4g_espnow_stats_t *out);

/**
 * @brief Deinitialize ESP-NOW subsystem
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t m4g_espnow_deinit(void);

#ifdef __cplusplus
}
#endif
