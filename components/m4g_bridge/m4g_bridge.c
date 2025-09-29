#include "m4g_bridge.h"
#include "m4g_ble.h"
#include "m4g_logging.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

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

static const char *BRIDGE_TAG = "M4G-BRIDGE";

// Maintain current active chord keys (up to 6 standard HID slots)
static uint8_t s_active_keys[6] = {0};
static uint8_t s_last_kb_report[8] = {0};
static uint8_t s_last_mouse_report[3] = {0};
static bool s_have_kb = false;
static bool s_have_mouse = false;
static uint32_t s_kb_sent = 0;
static uint32_t s_mouse_sent = 0;

static void update_active_keys(const uint8_t *keys, size_t n)
{
  memset(s_active_keys, 0, sizeof(s_active_keys));
  for (size_t i = 0; i < n && i < 6; ++i)
    s_active_keys[i] = keys[i];
}

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
  // Currently stateless; placeholder for future mapping tables/macros
  return ESP_OK;
}

void m4g_bridge_get_stats(m4g_bridge_stats_t *out)
{
  if (!out)
    return;
  out->keyboard_reports_sent = s_kb_sent;
  out->mouse_reports_sent = s_mouse_sent;
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

void m4g_bridge_process_usb_report(const uint8_t *report, size_t len)
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

  // Debug: Always log what keys we extracted
  if (nk > 0)
  {
    LOG_AND_SAVE(true, I, BRIDGE_TAG, "Extracted %zu keys: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X", 
                 nk, new_keys[0], new_keys[1], new_keys[2], new_keys[3], new_keys[4], new_keys[5]);
  }

  // Arrow to mouse mapping (optional)
  int mx = 0, my = 0;
#ifdef CONFIG_M4G_ENABLE_ARROW_MOUSE
  for (size_t i = 0; i < nk; ++i)
  {
    LOG_AND_SAVE(true, I, BRIDGE_TAG, "Checking key[%zu] = 0x%02X for mouse mapping", i, new_keys[i]);
    switch (new_keys[i])
    {
    // CharaChorder mouse movement keys (based on your logs)
    case 0x29: // Escape - Mouse Up
      my -= 10;
      LOG_AND_SAVE(true, I, BRIDGE_TAG, "*** MOUSE UP: 0x29 (Escape) ***");
      break;
    case 0x08: // Backspace - Mouse Down  
      my += 10;
      LOG_AND_SAVE(true, I, BRIDGE_TAG, "*** MOUSE DOWN: 0x08 (Backspace) ***");
      break;
    case 0x38: // Forward Slash - Mouse Left
      mx -= 10;
      LOG_AND_SAVE(true, I, BRIDGE_TAG, "*** MOUSE LEFT: 0x38 (/) ***");
      break;
    case 0x2E: // Period - Mouse Right
      mx += 10;
      LOG_AND_SAVE(true, I, BRIDGE_TAG, "*** MOUSE RIGHT: 0x2E (.) ***");
      break;
    
    // Standard arrow keys for cursor movement (keep these for cursor)
    case 0x4F: // Right Arrow - don't convert to mouse
    case 0x50: // Left Arrow
    case 0x51: // Down Arrow
    case 0x52: // Up Arrow
      // Let these pass through as normal cursor keys
      break;
    default:
      break;
    }
  }

  // DUAL-FUNCTION: Mouse keys work for BOTH mouse movement AND keyboard input
  // Keep all keys in keyboard report - mouse keys will also type their characters
  size_t filtered_n = nk;  // Keep all keys, no filtering needed anymore
  
  if (ENABLE_DEBUG_KEYPRESS_LOGGING)
  {
    LOG_AND_SAVE(true, I, TAG, "Dual-function: Mouse keys will generate BOTH mouse movement AND keyboard input");
  }
  nk = filtered_n;
  if (nk < 6)
    memset(&new_keys[nk], 0, (6 - nk) * sizeof(uint8_t));
#endif

  update_active_keys(new_keys, nk);

  // Build standard 8-byte keyboard HID report
  uint8_t kb_report[8] = {0};
  kb_report[0] = (kb_len >= 1) ? kb_payload[0] : 0;
  kb_report[1] = 0; // reserved

  // Use cached active keys (non-zero entries) to populate HID slots
  memcpy(&kb_report[2], s_active_keys, 6);

  // Enhanced logging for keyboard report debugging
  LOG_AND_SAVE(true, I, TAG, "*** KEYBOARD REPORT: mod=0x%02X keys=[0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X]", 
               kb_report[0], kb_report[2], kb_report[3], kb_report[4], kb_report[5], kb_report[6], kb_report[7]);

  // Suppress duplicate consecutive keyboard reports to save BLE airtime (optional)
#ifdef CONFIG_M4G_ENABLE_DUPLICATE_SUPPRESSION
  bool kb_changed = (!s_have_kb) || (memcmp(s_last_kb_report, kb_report, 8) != 0);
#else
  bool kb_changed = true;
#endif
  
  LOG_AND_SAVE(true, I, TAG, "*** Keyboard changed: %s, sending keyboard report ***", kb_changed ? "YES" : "NO");
  
  if (kb_changed)
  {
    if (m4g_ble_send_keyboard_report(kb_report))
    {
      LOG_AND_SAVE(true, I, TAG, "*** KEYBOARD REPORT SENT SUCCESSFULLY ***");
      memcpy(s_last_kb_report, kb_report, 8);
      s_have_kb = true;
      ++s_kb_sent;
    }
    else 
    {
      LOG_AND_SAVE(true, E, TAG, "*** KEYBOARD REPORT FAILED TO SEND ***");
    }
    else if (ENABLE_DEBUG_KEYPRESS_LOGGING)
    {
      LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, W, BRIDGE_TAG, "BLE send dropped (conn=%d notify=%d)", m4g_ble_is_connected(), m4g_ble_notifications_enabled());
    }
  }

  // Send proper mouse HID reports for detected mouse movement
  if (mx > 127)
    mx = 127;
  else if (mx < -127)
    mx = -127;
  if (my > 127)
    my = 127;
  else if (my < -127)
    my = -127;
  if (mx || my)
  {
    LOG_AND_SAVE(true, I, BRIDGE_TAG, "*** Preparing to send mouse report: x=%d y=%d ***", mx, my);
    uint8_t mouse[3] = {0};
    mouse[1] = (uint8_t)mx;
    mouse[2] = (uint8_t)my;
    bool mouse_changed = true;
#ifdef CONFIG_M4G_ENABLE_DUPLICATE_SUPPRESSION
    mouse_changed = (!s_have_mouse) || (memcmp(s_last_mouse_report, mouse, 3) != 0);
#endif
    if (mouse_changed)
    {
      LOG_AND_SAVE(true, I, BRIDGE_TAG, "*** Sending mouse report via BLE ***");
      if (m4g_ble_send_mouse_report(mouse))
      {
        memcpy(s_last_mouse_report, mouse, 3);
        s_have_mouse = true;
        ++s_mouse_sent;
        LOG_AND_SAVE(true, I, BRIDGE_TAG, "*** MOUSE REPORT SENT SUCCESSFULLY ***");
      }
      else 
      {
        LOG_AND_SAVE(true, W, BRIDGE_TAG, "*** MOUSE REPORT FAILED *** (conn=%d notify=%d)", m4g_ble_is_connected(), m4g_ble_notifications_enabled());
      }
    }
    else
    {
      LOG_AND_SAVE(true, I, BRIDGE_TAG, "Mouse report unchanged, not sending");
    }
  }
}
