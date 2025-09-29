#include "m4g_bridge.h"
#include "m4g_ble.h"
#include "m4g_logging.h"
#include <string.h>
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

  // Arrow to mouse mapping (optional)
  int mx = 0, my = 0;
#ifdef CONFIG_M4G_ENABLE_ARROW_MOUSE
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

  // Remove arrow keys from keyboard report payload when mapping to mouse
  size_t filtered_n = 0;
  for (size_t i = 0; i < nk; ++i)
  {
    switch (new_keys[i])
    {
    case 0x4F:
    case 0x50:
    case 0x51:
    case 0x52:
      continue;
    default:
      new_keys[filtered_n++] = new_keys[i];
      break;
    }
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

  // If any mouse movement, send 3-byte mouse report
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
