#include "m4g_bridge.h"
#include "m4g_ble.h"
#include "m4g_logging.h"
#include "m4g_usb.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "sdkconfig.h"

static const char *BRIDGE_TAG = "M4G-BRIDGE";

// Maintain current active chord keys (up to 6 standard HID slots)
static uint8_t s_active_keys[6] = {0};
static uint8_t s_last_kb_report[8] = {0};
static uint8_t s_last_mouse_report[3] = {0};
static bool s_have_kb = false;
static bool s_have_mouse = false;
static uint32_t s_kb_sent = 0;
static uint32_t s_mouse_sent = 0;
static uint32_t s_chord_processed = 0;
static uint32_t s_chord_delayed = 0;

// CharaChorder state
static bool s_charachorder_detected = false;
static bool s_charachorder_both_halves = false;

// Chord processing state
typedef struct {
  uint8_t report[64];
  size_t len;
  bool is_charachorder;
  TickType_t timestamp;
} pending_report_t;

static pending_report_t s_pending_report = {0};
static TimerHandle_t s_chord_timer = NULL;
static bool s_report_pending = false;

static void update_active_keys(const uint8_t *keys, size_t n)
{
  memset(s_active_keys, 0, sizeof(s_active_keys));
  for (size_t i = 0; i < n && i < 6; ++i)
    s_active_keys[i] = keys[i];
}

// Forward declaration for chord timer callback
static void chord_timer_callback(TimerHandle_t xTimer);

// Process report immediately (used for non-CharaChorder devices or when delay is disabled)
static void process_report_immediate(const uint8_t *report, size_t len, bool is_charachorder);

static size_t extract_chara_keys(const uint8_t *kb_payload, size_t len, uint8_t *out6)
{
  size_t n = 0;
  if (len < 8)
    return 0;

  for (size_t i = 2; i < len && n < 6; ++i)
  {
    if (kb_payload[i] != 0)
      out6[n++] = kb_payload[i];
  }
  return n;
}

esp_err_t m4g_bridge_init(void)
{
  // Create timer for CharaChorder chord processing delay
  s_chord_timer = xTimerCreate("chord_timer", 
                               pdMS_TO_TICKS(CONFIG_M4G_CHARACHORDER_CHORD_DELAY_MS),
                               pdFALSE, // one-shot timer
                               NULL, 
                               chord_timer_callback);
  if (!s_chord_timer) {
    LOG_AND_SAVE(true, E, BRIDGE_TAG, "Failed to create chord timer");
    return ESP_FAIL;
  }
  
  LOG_AND_SAVE(true, I, BRIDGE_TAG, "Bridge initialized with chord delay=%d ms", 
               (int)CONFIG_M4G_CHARACHORDER_CHORD_DELAY_MS);
  return ESP_OK;
}

void m4g_bridge_set_charachorder_status(bool detected, bool both_halves_connected)
{
  s_charachorder_detected = detected;
  s_charachorder_both_halves = both_halves_connected;
  LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, BRIDGE_TAG, 
               "CharaChorder status: detected=%d both_halves=%d", 
               detected, both_halves_connected);
}

void m4g_bridge_get_stats(m4g_bridge_stats_t *out)
{
  if (!out)
    return;
  out->keyboard_reports_sent = s_kb_sent;
  out->mouse_reports_sent = s_mouse_sent;
  out->chord_reports_processed = s_chord_processed;
  out->chord_reports_delayed = s_chord_delayed;
}

bool m4g_bridge_get_last_keyboard(uint8_t out[8])
{
  if (!s_have_kb)
    return false;
  memcpy(out, s_last_kb_report, 8);
  return true;
}

bool m4g_bridge_get_last_mouse(uint8_t out[3])
{
  if (!s_have_mouse)
    return false;
  memcpy(out, s_last_mouse_report, 3);
  return true;
}

void m4g_bridge_process_usb_report(const uint8_t *report, size_t len, bool is_charachorder)
{
  if (!report || len == 0)
    return;

  // For CharaChorder devices, implement chord processing delay unless disabled
  if (is_charachorder && s_charachorder_detected && !CONFIG_M4G_CHARACHORDER_RAW_MODE) 
  {
    // If a report is already pending, process it immediately to prevent backlog
    if (s_report_pending) {
      LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, D, BRIDGE_TAG, 
                   "Processing pending report immediately due to new report");
      process_report_immediate(s_pending_report.report, s_pending_report.len, s_pending_report.is_charachorder);
      s_report_pending = false;
    }
    
    // Store the new report for delayed processing
    if (len <= sizeof(s_pending_report.report)) {
      memcpy(s_pending_report.report, report, len);
      s_pending_report.len = len;
      s_pending_report.is_charachorder = is_charachorder;
      s_pending_report.timestamp = xTaskGetTickCount();
      s_report_pending = true;
      s_chord_delayed++;
      
      // Start/restart the timer
      if (xTimerReset(s_chord_timer, 0) != pdPASS) {
        LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, W, BRIDGE_TAG, "Failed to start chord timer");
        // Fall back to immediate processing
        process_report_immediate(report, len, is_charachorder);
        s_report_pending = false;
      }
    } else {
      LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, W, BRIDGE_TAG, "Report too large for pending buffer, processing immediately");
      process_report_immediate(report, len, is_charachorder);
    }
  } else {
    // Non-CharaChorder or raw mode - process immediately
    process_report_immediate(report, len, is_charachorder);
  }
}

static void chord_timer_callback(TimerHandle_t xTimer)
{
  (void)xTimer;
  if (s_report_pending) {
    LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, D, BRIDGE_TAG, 
                 "Chord delay expired, processing report");
    process_report_immediate(s_pending_report.report, s_pending_report.len, s_pending_report.is_charachorder);
    s_report_pending = false;
    s_chord_processed++;
  }
}

static void process_report_immediate(const uint8_t *report, size_t len, bool is_charachorder)
{
  if (!report || len == 0)
    return;
  const uint8_t *kb_payload = NULL;
  size_t kb_len = 0;

  // Allow either Report ID-prefixed or plain 8-byte keyboard reports
  if (len >= 9 && report[0] == 0x01)
  {
    kb_payload = &report[1];
    kb_len = len - 1;
  }
  else if (len >= 8)
  {
    kb_payload = report;
    kb_len = len;
  }
  else
    return;

  uint8_t new_keys[6] = {0};
  size_t nk = extract_chara_keys(kb_payload, kb_len, new_keys);
  update_active_keys(new_keys, nk);

  // Build standard 8-byte keyboard HID report
  uint8_t kb_report[8] = {0};
  kb_report[0] = (kb_len >= 1) ? kb_payload[0] : 0;
  kb_report[1] = 0; // reserved

  // Use cached active keys (non-zero entries) to populate HID slots
  memcpy(&kb_report[2], s_active_keys, 6);

  // For CharaChorder devices in raw mode, disable arrow-to-mouse translation to preserve chord processing
  bool disable_arrow_mouse = is_charachorder && !CONFIG_M4G_CHARACHORDER_RAW_MODE;
  
  // Arrow to mouse mapping (optional, but disabled for CharaChorder chord processing)
  int8_t mx = 0, my = 0;
#ifdef CONFIG_M4G_ENABLE_ARROW_MOUSE
  if (!disable_arrow_mouse) {
    for (size_t i = 0; i < nk; ++i)
    {
      switch (new_keys[i])
      {
      case 0x4F: // Right
        mx += 10;
        break;
      case 0x50: // Left
        mx -= 10;
        break;
      case 0x51: // Down
        my += 10;
        break;
      case 0x52: // Up
        my -= 10;
        break;
      default:
        break;
      }
    }
  }
#endif

  // Suppress duplicate consecutive keyboard reports to save BLE airtime (optional)
#ifdef CONFIG_M4G_ENABLE_DUPLICATE_SUPPRESSION
  bool kb_changed = (!s_have_kb) || (memcmp(s_last_kb_report, kb_report, 8) != 0);
#else
  bool kb_changed = true;
#endif
  if (kb_changed)
  {
    if (m4g_ble_send_keyboard_report(kb_report))
    {
      memcpy(s_last_kb_report, kb_report, 8);
      s_have_kb = true;
      ++s_kb_sent;
    }
    else if (ENABLE_DEBUG_KEYPRESS_LOGGING)
    {
      LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, W, BRIDGE_TAG, "BLE send dropped (conn=%d notify=%d)", m4g_ble_is_connected(), m4g_ble_notifications_enabled());
    }
  }

  // If any mouse movement, send 3-byte mouse report (only if arrow-to-mouse is enabled)
  if ((mx || my) && !disable_arrow_mouse)
  {
    uint8_t mouse[3] = {0};
    mouse[1] = (uint8_t)mx;
    mouse[2] = (uint8_t)my;
    bool mouse_changed = true;
#ifdef CONFIG_M4G_ENABLE_DUPLICATE_SUPPRESSION
    mouse_changed = (!s_have_mouse) || (memcmp(s_last_mouse_report, mouse, 3) != 0);
#endif
    if (mouse_changed)
    {
      if (m4g_ble_send_mouse_report(mouse))
      {
        memcpy(s_last_mouse_report, mouse, 3);
        s_have_mouse = true;
        ++s_mouse_sent;
      }
    }
  }
}
