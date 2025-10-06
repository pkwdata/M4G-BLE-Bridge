/*
 * Full BLE migration: integrates HID service, advertising, security, GAP/GATT init, and report notification API.
 */
#include "m4g_ble.h"
#include "m4g_logging.h"
#include "m4g_led.h"
#include "m4g_bridge.h" // for m4g_bridge_stats_t and m4g_bridge_get_stats
#include "sdkconfig.h"
#include <string.h>
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/ble_store.h" // for ble_store_util_status_rr, ble_store_util_delete_peer

// Forward declaration for NimBLE store config functions (not in public headers)
#if CONFIG_BT_NIMBLE_NVS_PERSIST
void ble_store_config_init(void);
int ble_store_config_read(int obj_type, const union ble_store_key *key, union ble_store_value *val);
int ble_store_config_write(int obj_type, const union ble_store_value *val);
int ble_store_config_delete(int obj_type, const union ble_store_key *key);
#endif

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

// Forward declare USB helper if its header is not already included here
extern uint8_t m4g_usb_active_hid_count(void);

static const char *BLE_TAG = "M4G-BLE";

// HID UUID constants (mirroring legacy main definitions)
#define BLE_HID_SERVICE_UUID 0x1812
#define BLE_HID_CHARACTERISTIC_REPORT_UUID 0x2A4D
#define BLE_HID_CHAR_REPORT_MAP_UUID 0x2A4B
#define BLE_HID_CHAR_HID_INFO_UUID 0x2A4A
#define BLE_HID_CHAR_HID_CTRL_POINT_UUID 0x2A4C

// Embedded HID report map (text file with hex bytes). ESP-IDF generates *_start/_end symbols.
extern const uint8_t _binary_hid_report_map_txt_start[];
extern const uint8_t _binary_hid_report_map_txt_end[];
static uint8_t s_hid_report_map[128];
static size_t s_hid_report_map_len = 0;

static int m4g_hex_val(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static void parse_embedded_report_map(void)
{
  const uint8_t *p = _binary_hid_report_map_txt_start;
  const uint8_t *end = _binary_hid_report_map_txt_end;
  while (p < end && s_hid_report_map_len < sizeof(s_hid_report_map))
  {
    // Skip whitespace
    while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t'))
      ++p;
    if (p >= end)
      break;
    // Expect two hex chars
    if (end - p < 2)
      break;
    char h0 = (char)p[0];
    char h1 = (char)p[1];
    int v0 = m4g_hex_val(h0);
    int v1 = m4g_hex_val(h1);
    if (v0 < 0 || v1 < 0)
    {
      // Skip invalid char and continue (tolerant parser)
      ++p;
      continue;
    }
    s_hid_report_map[s_hid_report_map_len++] = (uint8_t)((v0 << 4) | v1);
    p += 2;
  }
  if (s_hid_report_map_len == 0)
  {
    // Fallback to zero-length safe map
    LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, E, BLE_TAG, "HID report map parse produced 0 bytes");
  }
  else
  {
    LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, I, BLE_TAG, "HID report map parsed: %d bytes", s_hid_report_map_len);
  }
}

// Connection and state
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool s_report_notifications_enabled = false;
static bool s_encrypted = false;
static uint8_t s_addr_type = 0;

// Characteristic handle we will discover for report notifications
static uint16_t s_report_chr_handle = 0;
static uint16_t s_boot_report_chr_handle = 0;
static bool s_boot_notifications_enabled = false;

bool m4g_ble_is_connected(void) { return s_conn_handle != BLE_HS_CONN_HANDLE_NONE; }
bool m4g_ble_notifications_enabled(void) { return s_report_notifications_enabled || s_boot_notifications_enabled; }

// Forward decls
static int gap_event_handler(struct ble_gap_event *event, void *arg);
static void start_advertising(void);
static int hid_svc_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

// GATT service definition (Device Info, Battery (minimal), HID, optional Diagnostics)
static const struct ble_gatt_svc_def hid_svcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x180A), .characteristics = (struct ble_gatt_chr_def[]){{.uuid = BLE_UUID16_DECLARE(0x2A29), .access_cb = hid_svc_access_cb, .flags = BLE_GATT_CHR_F_READ}, // Manufacturer
                                                                                                                           {.uuid = BLE_UUID16_DECLARE(0x2A24), .access_cb = hid_svc_access_cb, .flags = BLE_GATT_CHR_F_READ}, // Model
                                                                                                                           {.uuid = BLE_UUID16_DECLARE(0x2A50), .access_cb = hid_svc_access_cb, .flags = BLE_GATT_CHR_F_READ}, // PnP ID
                                                                                                                           {0}}},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0x180F), .characteristics = (struct ble_gatt_chr_def[]){{.uuid = BLE_UUID16_DECLARE(0x2A19), .access_cb = hid_svc_access_cb, .flags = BLE_GATT_CHR_F_READ}, // Battery Level
                                                                                                                           {0}}},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(BLE_HID_SERVICE_UUID), .characteristics = (struct ble_gatt_chr_def[]){{.uuid = BLE_UUID16_DECLARE(BLE_HID_CHAR_HID_INFO_UUID), .access_cb = hid_svc_access_cb, .flags = BLE_GATT_CHR_F_READ}, {.uuid = BLE_UUID16_DECLARE(BLE_HID_CHAR_REPORT_MAP_UUID), .access_cb = hid_svc_access_cb, .flags = BLE_GATT_CHR_F_READ}, {.uuid = BLE_UUID16_DECLARE(BLE_HID_CHAR_HID_CTRL_POINT_UUID), .access_cb = hid_svc_access_cb, .flags = BLE_GATT_CHR_F_WRITE_NO_RSP}, {.uuid = BLE_UUID16_DECLARE(0x2A4E), .access_cb = hid_svc_access_cb, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP}, // Protocol Mode
                                                                                                                                         {.uuid = BLE_UUID16_DECLARE(0x2A22), .access_cb = hid_svc_access_cb, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .min_key_size = 16, .descriptors = (struct ble_gatt_dsc_def[]){{.uuid = BLE_UUID16_DECLARE(0x2902), .access_cb = hid_svc_access_cb, .att_flags = BLE_ATT_F_READ | BLE_ATT_F_WRITE}, {0}}},                                                                                                                                                                                                     // Boot Keyboard Input Report
                                                                                                                                         {.uuid = BLE_UUID16_DECLARE(BLE_HID_CHARACTERISTIC_REPORT_UUID), .access_cb = hid_svc_access_cb, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .min_key_size = 16, .descriptors = (struct ble_gatt_dsc_def[]){{.uuid = BLE_UUID16_DECLARE(0x2902), .access_cb = hid_svc_access_cb, .att_flags = BLE_ATT_F_READ | BLE_ATT_F_WRITE},                                                                                                                                                                                // CCCD
                                                                                                                                                                                                                                                                                                                                                               {.uuid = BLE_UUID16_DECLARE(0x2908), .access_cb = hid_svc_access_cb, .att_flags = BLE_ATT_F_READ},                                                                                                                                                                                                  // Report Reference (composite kbd+mouse)
                                                                                                                                                                                                                                                                                                                                                               {0}}},
                                                                                                                                         {0}}},
#ifdef CONFIG_M4G_ENABLE_DIAG_GATT
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = BLE_UUID16_DECLARE(0xFFF0), .characteristics = (struct ble_gatt_chr_def[]){{.uuid = BLE_UUID16_DECLARE(0xFFF1), .access_cb = hid_svc_access_cb, .flags = BLE_GATT_CHR_F_READ}, {0}}},
#endif
    {0}};

// Access callback implementing reads/writes for HID service
static int hid_svc_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
  (void)attr_handle;
  (void)arg;
  int rc;
  static uint8_t protocol_mode = 1; // Report Protocol
  uint8_t hid_info[4] = {0x11, 0x01, 0x00, 0x00};
  switch (ctxt->op)
  {
  case BLE_GATT_ACCESS_OP_READ_CHR:
#ifdef CONFIG_M4G_ENABLE_DIAG_GATT
    if (ble_uuid_cmp(ctxt->chr->uuid, BLE_UUID16_DECLARE(0xFFF1)) == 0)
    {
      bool ble_conn = m4g_ble_is_connected();
      m4g_bridge_stats_t stats;
      m4g_bridge_get_stats(&stats);
      char diag[64];
      int n = snprintf(diag, sizeof(diag), "B%d U%d KB%lu M%lu", ble_conn ? 1 : 0, (int)m4g_usb_active_hid_count(), (unsigned long)stats.keyboard_reports_sent, (unsigned long)stats.mouse_reports_sent);
      int rc2 = os_mbuf_append(ctxt->om, diag, n);
      return rc2 == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
#endif
    if (ble_uuid_cmp(ctxt->chr->uuid, BLE_UUID16_DECLARE(BLE_HID_CHAR_REPORT_MAP_UUID)) == 0)
    {
      if (s_hid_report_map_len == 0)
        parse_embedded_report_map();
      rc = os_mbuf_append(ctxt->om, s_hid_report_map, s_hid_report_map_len);
      return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ble_uuid_cmp(ctxt->chr->uuid, BLE_UUID16_DECLARE(BLE_HID_CHAR_HID_INFO_UUID)) == 0)
    {
      rc = os_mbuf_append(ctxt->om, hid_info, sizeof(hid_info));
      return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ble_uuid_cmp(ctxt->chr->uuid, BLE_UUID16_DECLARE(BLE_HID_CHARACTERISTIC_REPORT_UUID)) == 0)
    {
      uint8_t empty[8] = {0};
      rc = os_mbuf_append(ctxt->om, empty, sizeof(empty));
      return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ble_uuid_cmp(ctxt->chr->uuid, BLE_UUID16_DECLARE(0x2A22)) == 0)
    {
      uint8_t empty_boot[8] = {0};
      rc = os_mbuf_append(ctxt->om, empty_boot, sizeof(empty_boot));
      return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ble_uuid_cmp(ctxt->chr->uuid, BLE_UUID16_DECLARE(0x2A4E)) == 0)
    {
      rc = os_mbuf_append(ctxt->om, &protocol_mode, 1);
      return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    break;
  case BLE_GATT_ACCESS_OP_READ_DSC:
    if (ble_uuid_cmp(ctxt->dsc->uuid, BLE_UUID16_DECLARE(0x2908)) == 0)
    {
      // Report Reference descriptor - need to distinguish keyboard vs mouse
      // Single Report characteristic with multiple Report IDs in Report Map
      uint8_t report_ref[2];
      report_ref[0] = 0x00; // Report ID (0 means use Report ID from Report Map)
      report_ref[1] = 0x01; // Input Report type
      rc = os_mbuf_append(ctxt->om, report_ref, sizeof(report_ref));
      return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ble_uuid_cmp(ctxt->dsc->uuid, BLE_UUID16_DECLARE(0x2902)) == 0)
    {
      bool enabled;
      if (ble_uuid_cmp(ctxt->chr->uuid, BLE_UUID16_DECLARE(0x2A22)) == 0)
      {
        enabled = s_boot_notifications_enabled;
      }
      else
      {
        enabled = s_report_notifications_enabled;
      }
      uint16_t cccd = enabled ? 0x0001 : 0x0000;
      rc = os_mbuf_append(ctxt->om, &cccd, sizeof(cccd));
      return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    break;
  case BLE_GATT_ACCESS_OP_WRITE_DSC:
    if (ble_uuid_cmp(ctxt->dsc->uuid, BLE_UUID16_DECLARE(0x2902)) == 0)
    {
      uint16_t cccd_val = 0;
      rc = ble_hs_mbuf_to_flat(ctxt->om, &cccd_val, sizeof(cccd_val), NULL);
      if (rc == 0)
      {
        bool enable = (cccd_val != 0);
        if (ble_uuid_cmp(ctxt->chr->uuid, BLE_UUID16_DECLARE(0x2A22)) == 0)
        {
          s_boot_notifications_enabled = enable;
          LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, I, BLE_TAG, "Boot notifications %s", enable ? "ENABLED" : "disabled");
        }
        else
        {
          s_report_notifications_enabled = enable;
          LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, I, BLE_TAG, "Report notifications %s", enable ? "ENABLED" : "disabled");
        }
      }
      return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    break;
  case BLE_GATT_ACCESS_OP_WRITE_CHR:
    if (ble_uuid_cmp(ctxt->chr->uuid, BLE_UUID16_DECLARE(BLE_HID_CHAR_HID_CTRL_POINT_UUID)) == 0)
      return 0; // accept
    if (ble_uuid_cmp(ctxt->chr->uuid, BLE_UUID16_DECLARE(0x2A4E)) == 0)
    {
      rc = ble_hs_mbuf_to_flat(ctxt->om, &protocol_mode, 1, NULL);
      return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    break;
  default:
    break;
  }
  return BLE_ATT_ERR_UNLIKELY;
}

static void discover_report_handles(void)
{
  uint16_t chr_handle = 0;
  int rc = ble_gatts_find_chr(BLE_UUID16_DECLARE(BLE_HID_SERVICE_UUID), BLE_UUID16_DECLARE(BLE_HID_CHARACTERISTIC_REPORT_UUID), NULL, &chr_handle);
  if (rc == 0 && chr_handle != 0)
  {
    s_report_chr_handle = chr_handle;
    LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, I, BLE_TAG, "Report characteristic handle=0x%04X (composite kbd+mouse)", s_report_chr_handle);
  }
  else
  {
    LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, E, BLE_TAG, "Failed to resolve report characteristic rc=%d", rc);
#ifdef CONFIG_M4G_ASSERT_BLE_HANDLE
    assert(false && "BLE report characteristic handle not resolved");
#endif
  }

  chr_handle = 0;
  rc = ble_gatts_find_chr(BLE_UUID16_DECLARE(BLE_HID_SERVICE_UUID), BLE_UUID16_DECLARE(0x2A22), NULL, &chr_handle);
  if (rc == 0 && chr_handle != 0)
  {
    s_boot_report_chr_handle = chr_handle;
    LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, I, BLE_TAG, "Boot report handle=0x%04X", s_boot_report_chr_handle);
  }
  else
  {
    LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, W, BLE_TAG, "Failed to resolve boot report handle rc=%d", rc);
  }
}

static void start_advertising(void)
{
  struct ble_gap_adv_params adv_params;
  memset(&adv_params, 0, sizeof(adv_params));
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  // Force proper advertising intervals (fix for itvl_min=0 itvl_max=0 issue)
  adv_params.itvl_min = 32; // 20ms (32 * 0.625ms)
  adv_params.itvl_max = 48; // 30ms (48 * 0.625ms)

  LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, I, BLE_TAG, "BEFORE ADV START: min=%d max=%d", adv_params.itvl_min, adv_params.itvl_max);
  adv_params.channel_map = 0;   // Use all channels
  adv_params.filter_policy = 0; // Allow all connections
  struct ble_hs_adv_fields fields;
  memset(&fields, 0, sizeof(fields));
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  static const char *name = "M4G BLE Bridge";
  fields.name = (uint8_t *)name;
  fields.name_len = strlen(name);
  fields.name_is_complete = 1;
  static ble_uuid16_t hid_uuid = BLE_UUID16_INIT(BLE_HID_SERVICE_UUID);
  fields.uuids16 = &hid_uuid;
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1; // Mark as complete - we're advertising all services
  fields.appearance = 0x03C0;     // Generic HID (supports both keyboard and mouse)
  fields.appearance_is_present = 1;
  int rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, E, BLE_TAG, "adv set fields rc=%d", rc);
    return;
  }
  LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, I, BLE_TAG, "CALLING ble_gap_adv_start with min=%d max=%d", adv_params.itvl_min, adv_params.itvl_max);
  rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &adv_params, gap_event_handler, NULL);
  if (rc != 0)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, E, BLE_TAG, "adv start rc=%d", rc);
  }
  else
  {
    LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, I, BLE_TAG, "Advertising started");
  }
}

static void handle_connect_success(uint16_t conn_handle)
{
  s_conn_handle = conn_handle;
  m4g_led_set_ble_connected(true);
  LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, I, BLE_TAG, "Connected handle=%d", s_conn_handle);
}

static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
  (void)arg;
  switch (event->type)
  {
  case BLE_GAP_EVENT_CONNECT:
    if (event->connect.status == 0)
    {
      handle_connect_success(event->connect.conn_handle);
    }
    else
    {
      s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
      start_advertising();
    }
    return 0;
  case BLE_GAP_EVENT_DISCONNECT:
    LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, I, BLE_TAG, "Disconnected: reason=%d", event->disconnect.reason);
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_report_notifications_enabled = false;
    s_boot_notifications_enabled = false;
    s_encrypted = false;
    m4g_led_set_ble_connected(false);
    start_advertising();
    return 0;
  case BLE_GAP_EVENT_ENC_CHANGE:
    if (event->enc_change.status == 0)
    {
      s_encrypted = true;
      LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, I, BLE_TAG, "Encryption complete");
    }
    else
    {
      LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, W, BLE_TAG, "Encryption failed %d", event->enc_change.status);
    }
    return 0;
  case BLE_GAP_EVENT_REPEAT_PAIRING:
  {
    struct ble_gap_conn_desc desc;
    int rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
    if (rc != 0)
    {
      LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, W, BLE_TAG, "repeat pairing lookup failed rc=%d", rc);
      return BLE_GAP_REPEAT_PAIRING_IGNORE;
    }
    rc = ble_store_util_delete_peer(&desc.peer_id_addr);
    if (rc != 0)
    {
      LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, W, BLE_TAG, "repeat pairing delete peer rc=%d", rc);
      return BLE_GAP_REPEAT_PAIRING_IGNORE;
    }
    LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, I, BLE_TAG, "repeat pairing: cleared old bond");
    return BLE_GAP_REPEAT_PAIRING_RETRY;
  }
  case BLE_GAP_EVENT_NOTIFY_TX:
    return 0;
  case BLE_GAP_EVENT_SUBSCRIBE:
    if (ENABLE_DEBUG_BLE_LOGGING)
    {
      LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, I, BLE_TAG,
                   "Subscribe attr=0x%04X cur_notify=%d cur_indicate=%d (report=0x%04X boot=0x%04X)",
                   event->subscribe.attr_handle, event->subscribe.cur_notify, event->subscribe.cur_indicate,
                   s_report_chr_handle, s_boot_report_chr_handle);
    }
    if (event->subscribe.attr_handle == s_report_chr_handle)
    {
      s_report_notifications_enabled = event->subscribe.cur_notify;
      LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, I, BLE_TAG, "Report notifications %s (via subscribe)", s_report_notifications_enabled ? "ENABLED" : "disabled");
    }
    else if (event->subscribe.attr_handle == s_boot_report_chr_handle)
    {
      s_boot_notifications_enabled = event->subscribe.cur_notify;
      LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, I, BLE_TAG, "Boot notifications %s (via subscribe)", s_boot_notifications_enabled ? "ENABLED" : "disabled");
    }
    return 0;
  default:
    break;
  }
  return 0;
}

static void on_reset(int reason) { LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, W, BLE_TAG, "Host reset reason=%d", reason); }
static void on_sync(void)
{
  LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, I, BLE_TAG, "Host sync");
  int rc = ble_hs_util_ensure_addr(0);
  if (rc != 0)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, E, BLE_TAG, "ensure addr rc=%d", rc);
    return;
  }
  rc = ble_hs_id_infer_auto(0, &s_addr_type);
  if (rc != 0)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, E, BLE_TAG, "infer addr rc=%d", rc);
    return;
  }
  start_advertising();
}

static void host_task(void *param)
{
  nimble_port_run();
  nimble_port_freertos_deinit();
}

esp_err_t m4g_ble_init(void)
{
  // Release classic BT memory (done in original code outside; safe noop if already released)
  esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  esp_err_t rc = nimble_port_init();
  if (rc != ESP_OK)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, E, BLE_TAG, "nimble_port_init failed: %s", esp_err_to_name(rc));
    return rc;
  }
  // Security & host config mirroring legacy approach
  ble_hs_cfg.reset_cb = on_reset;
  ble_hs_cfg.sync_cb = on_sync;
  ble_hs_cfg.gatts_register_cb = NULL;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
  ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 0;
  ble_hs_cfg.sm_sc = 0;
  ble_hs_cfg.sm_keypress = 0;
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  int irc = ble_svc_gap_device_name_set("M4G BLE Bridge");
  if (irc != 0)
    LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, E, BLE_TAG, "name set rc=%d", irc);
  ble_svc_gap_init();
  ble_svc_gatt_init();
  // Load/parse embedded HID map early (not strictly required here but ensures length known)
  parse_embedded_report_map();
  if (s_hid_report_map_len == 0)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, W, BLE_TAG, "HID report map empty after parse");
  }
  // Count and add our HID services
  irc = ble_gatts_count_cfg(hid_svcs);
  if (irc != 0)
    LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, E, BLE_TAG, "count cfg rc=%d", irc);
  irc = ble_gatts_add_svcs(hid_svcs);
  if (irc != 0)
    LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, E, BLE_TAG, "add svcs rc=%d", irc);

  // Configure NVS storage for bonding keys BEFORE ble_store_config_init()
#if CONFIG_BT_NIMBLE_NVS_PERSIST
  ble_hs_cfg.store_read_cb = ble_store_config_read;
  ble_hs_cfg.store_write_cb = ble_store_config_write;
  ble_hs_cfg.store_delete_cb = ble_store_config_delete;
  ble_store_config_init();

  // Optional: Clear bonding data if there were persistent connection issues
  // This can be enabled manually when needed rather than every boot
#ifdef CONFIG_M4G_CLEAR_BONDING_ON_BOOT
  int clear_rc = ble_store_clear();
  LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, W, BLE_TAG, "Cleared bonding store: rc=%d (CONFIG_M4G_CLEAR_BONDING_ON_BOOT enabled)", clear_rc);
#else
  LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, I, BLE_TAG, "BLE bonding initialized - existing bonds preserved");
#endif
#else
  LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, W, BLE_TAG, "CONFIG_BT_NIMBLE_NVS_PERSIST is disabled; BLE bonds will not survive reflashing");
#endif

  nimble_port_freertos_init(host_task);
  discover_report_handles();
  LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, I, BLE_TAG, "BLE HID initialized");
  return ESP_OK;
}

static bool notify_handle(uint16_t chr_handle, const uint8_t *report, size_t len)
{
  struct os_mbuf *om = ble_hs_mbuf_from_flat(report, len);
  if (!om)
    return false;
  int rc = ble_gatts_notify_custom(s_conn_handle, chr_handle, om);
  if (rc != 0)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, W, BLE_TAG, "notify handle 0x%04X failed rc=%d", chr_handle, rc);
    os_mbuf_free_chain(om);
    return false;
  }
  return true;
}

static bool send_report_internal(const uint8_t *report, size_t len)
{
  if (!m4g_ble_is_connected())
    return false;
  if (len > 64)
    len = 64; // safety cap
  bool sent = false;

  if (s_report_notifications_enabled && s_report_chr_handle != 0)
  {
    sent |= notify_handle(s_report_chr_handle, report, len);
  }

  if (s_boot_notifications_enabled && s_boot_report_chr_handle != 0)
  {
    sent |= notify_handle(s_boot_report_chr_handle, report, len);
  }

#ifdef CONFIG_M4G_ASSERT_BLE_HANDLE
  if (!sent && (s_report_notifications_enabled || s_boot_notifications_enabled))
  {
    assert(false && "Failed to notify any HID characteristic");
  }
#endif

  return sent;
}

bool m4g_ble_send_keyboard_report(const uint8_t report[8])
{
  // Prepend Report ID 0x01 for keyboard
  uint8_t report_with_id[9];
  report_with_id[0] = 0x01; // Keyboard Report ID
  memcpy(&report_with_id[1], report, 8);

  return send_report_internal(report_with_id, 9);
}

bool m4g_ble_send_mouse_report(const uint8_t report[3])
{
  if (!m4g_ble_is_connected())
    return false;

  // Mouse: Send 3 bytes WITHOUT Report ID to the mouse Report characteristic
  // Format: [Buttons, X, Y] = 3 bytes (no Report ID)
  if (ENABLE_DEBUG_BLE_LOGGING)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_BLE_LOGGING, I, BLE_TAG,
                 "Sending mouse report with ID: [0x02 0x%02X 0x%02X 0x%02X] (Report ID, buttons, dx=%d, dy=%d)",
                 report[0], report[1], report[2],
                 (int8_t)report[1], (int8_t)report[2]);
  }

  // Prepend Report ID 0x02 for mouse
  uint8_t report_with_id[4];
  report_with_id[0] = 0x02; // Mouse Report ID
  memcpy(&report_with_id[1], report, 3);

  return send_report_internal(report_with_id, 4);
}
void m4g_ble_start_advertising(void) { start_advertising(); }
void m4g_ble_host_task(void *param) { host_task(param); }
