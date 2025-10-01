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

#define M4G_MAX_BUFFERED_KEYS 16

#ifndef CONFIG_M4G_CHARACHORDER_CHORD_DELAY_MS
#define CONFIG_M4G_CHARACHORDER_CHORD_DELAY_MS 15
#endif

#define M4G_CHORD_OUTPUT_GRACE_MS CONFIG_M4G_CHARACHORDER_CHORD_DELAY_MS

typedef struct
{
  bool present;
  bool is_charachorder;
  uint8_t modifiers;
  uint8_t keys[6];
} bridge_slot_state_t;

static bridge_slot_state_t s_slots[M4G_BRIDGE_MAX_SLOTS];
static bool s_warned_invalid_slot = false;

static uint8_t s_last_kb_report[8] = {0};
static uint8_t s_last_mouse_report[3] = {0};
static bool s_have_kb = false;
static bool s_have_mouse = false;
static uint32_t s_kb_sent = 0;
static uint32_t s_mouse_sent = 0;
static uint32_t s_chord_processed = 0;
static uint32_t s_chord_delayed = 0;
static bool s_charachorder_detected = false;
static bool s_charachorder_both_halves = false;

typedef struct
{
  uint8_t modifiers;
  uint8_t keys[6];
  size_t key_count;
  bool any_charachorder;
#ifdef CONFIG_M4G_ENABLE_ARROW_MOUSE
  int mouse_dx;
  int mouse_dy;
#endif
} combined_state_t;

typedef enum
{
  CHORD_STATE_IDLE = 0,
  CHORD_STATE_COLLECTING,
  CHORD_STATE_EXPECTING_OUTPUT,
  CHORD_STATE_PASSING_OUTPUT,
} chord_state_t;

static chord_state_t s_chord_state = CHORD_STATE_IDLE;
static uint8_t s_chord_buffer[M4G_MAX_BUFFERED_KEYS];
static size_t s_chord_buffer_len = 0;
static uint8_t s_chord_buffer_modifiers = 0;
static TickType_t s_expect_output_tick = 0;
static bool s_output_sequence_active = false;

static void compute_combined_state(combined_state_t *state);
static void process_combined_state(const combined_state_t *state);
static bool chord_mode_enabled(void);
static bool use_chord_for_state(const combined_state_t *state);
static void emit_keyboard_state(uint8_t modifiers, const uint8_t keys[6], bool allow_mouse, int mx, int my);
static void chord_buffer_reset(void);
static void chord_buffer_add(const combined_state_t *state);
static void send_buffered_single_key(void);


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

static void chord_buffer_reset(void)
{
  memset(s_chord_buffer, 0, sizeof(s_chord_buffer));
  s_chord_buffer_len = 0;
  s_chord_buffer_modifiers = 0;
  s_output_sequence_active = false;
}

static void chord_buffer_add(const combined_state_t *state)
{
  s_chord_buffer_modifiers |= state->modifiers;
  for (size_t i = 0; i < state->key_count; ++i)
  {
    uint8_t key = state->keys[i];
    if (key == 0)
      continue;
    bool already_present = false;
    for (size_t j = 0; j < s_chord_buffer_len; ++j)
    {
      if (s_chord_buffer[j] == key)
      {
        already_present = true;
        break;
      }
    }
    if (!already_present && s_chord_buffer_len < M4G_MAX_BUFFERED_KEYS)
    {
      s_chord_buffer[s_chord_buffer_len++] = key;
    }
  }
}

static bool chord_mode_enabled(void)
{
#ifdef CONFIG_M4G_CHARACHORDER_RAW_MODE
  if (CONFIG_M4G_CHARACHORDER_RAW_MODE)
    return false;
#endif
  if (!s_charachorder_detected)
    return false;
#ifdef CONFIG_M4G_CHARACHORDER_REQUIRE_BOTH_HALVES
  if (CONFIG_M4G_CHARACHORDER_REQUIRE_BOTH_HALVES && !s_charachorder_both_halves)
    return false;
#endif
  return true;
}

static bool use_chord_for_state(const combined_state_t *state)
{
  if (!state)
    return false;
  if (!state->any_charachorder)
    return false;
  return chord_mode_enabled();
}

static void compute_combined_state(combined_state_t *state)
{
  memset(state, 0, sizeof(*state));

  uint8_t combined_keys[6] = {0};
  size_t combined_count = 0;

  for (uint8_t slot = 0; slot < M4G_BRIDGE_MAX_SLOTS; ++slot)
  {
    if (!s_slots[slot].present)
      continue;

    if (s_slots[slot].is_charachorder)
      state->any_charachorder = true;

    state->modifiers |= s_slots[slot].modifiers;
    for (size_t i = 0; i < 6; ++i)
    {
      uint8_t key = s_slots[slot].keys[i];
      if (key == 0)
        continue;
      bool already_present = false;
      for (size_t j = 0; j < combined_count; ++j)
      {
        if (combined_keys[j] == key)
        {
          already_present = true;
          break;
        }
      }
      if (!already_present && combined_count < 6)
      {
        combined_keys[combined_count++] = key;
      }
    }
  }

  memcpy(state->keys, combined_keys, sizeof(state->keys));
  state->key_count = combined_count;

#ifdef CONFIG_M4G_ENABLE_ARROW_MOUSE
  int mx = 0;
  int my = 0;
  for (size_t i = 0; i < combined_count; ++i)
  {
    switch (combined_keys[i])
    {
    case 0x29: // Escape - Mouse Up
      my -= 10;
      break;
    case 0x08: // Backspace - Mouse Down
      my += 10;
      break;
    case 0x38: // Forward Slash - Mouse Left
      mx -= 10;
      break;
    case 0x2E: // Period - Mouse Right
      mx += 10;
      break;
    default:
      break;
    }
  }
  state->mouse_dx = mx;
  state->mouse_dy = my;
#endif
}

esp_err_t m4g_bridge_init(void)
{
  // Currently stateless; placeholder for future mapping tables/macros
  chord_buffer_reset();
  s_chord_state = CHORD_STATE_IDLE;
  s_expect_output_tick = xTaskGetTickCount();
  s_charachorder_detected = false;
  s_charachorder_both_halves = false;
  s_warned_invalid_slot = false;
  s_chord_processed = 0;
  s_chord_delayed = 0;
  return ESP_OK;
}

void m4g_bridge_set_charachorder_status(bool detected, bool both_halves_connected)
{
  bool previous_detected = s_charachorder_detected;
  s_charachorder_detected = detected;
  s_charachorder_both_halves = both_halves_connected;

  if (!detected)
  {
    chord_buffer_reset();
    s_chord_state = CHORD_STATE_IDLE;
  }

  if (ENABLE_DEBUG_USB_LOGGING && (previous_detected != detected))
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, BRIDGE_TAG,
                 "CharaChorder detection %s", detected ? "ENABLED" : "DISABLED");
  }
  if (ENABLE_DEBUG_USB_LOGGING && detected)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, BRIDGE_TAG,
                 "CharaChorder halves connected=%d", both_halves_connected);
  }
}

static void emit_keyboard_state(uint8_t modifiers, const uint8_t keys[6], bool allow_mouse, int mx, int my)
{
  uint8_t kb_report[8] = {0};
  kb_report[0] = modifiers;
  kb_report[1] = 0;
  memcpy(&kb_report[2], keys, 6);

  if (ENABLE_DEBUG_KEYPRESS_LOGGING)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                 "Emit report: mod=0x%02X keys=[0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X]",
                 kb_report[0], kb_report[2], kb_report[3], kb_report[4],
                 kb_report[5], kb_report[6], kb_report[7]);
  }

#ifdef CONFIG_M4G_ENABLE_DUPLICATE_SUPPRESSION
  bool kb_changed = (!s_have_kb) || (memcmp(s_last_kb_report, kb_report, sizeof(kb_report)) != 0);
#else
  bool kb_changed = true;
#endif

  if (kb_changed)
  {
    if (m4g_ble_send_keyboard_report(kb_report))
    {
      memcpy(s_last_kb_report, kb_report, sizeof(kb_report));
      s_have_kb = true;
      ++s_kb_sent;
    }
    else
    {
      LOG_AND_SAVE(true, E, BRIDGE_TAG, "Keyboard report failed (conn=%d notify=%d)",
                   m4g_ble_is_connected(), m4g_ble_notifications_enabled());
    }
  }
  else if (ENABLE_DEBUG_KEYPRESS_LOGGING)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, D, BRIDGE_TAG, "Duplicate keyboard report suppressed");
  }

#ifdef CONFIG_M4G_ENABLE_ARROW_MOUSE
  if (allow_mouse)
  {
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
#ifdef CONFIG_M4G_ENABLE_DUPLICATE_SUPPRESSION
      bool mouse_changed = (!s_have_mouse) || (memcmp(s_last_mouse_report, mouse, sizeof(mouse)) != 0);
#else
      bool mouse_changed = true;
#endif
      if (mouse_changed)
      {
        if (m4g_ble_send_mouse_report(mouse))
        {
          memcpy(s_last_mouse_report, mouse, sizeof(mouse));
          s_have_mouse = true;
          ++s_mouse_sent;
        }
        else
        {
          LOG_AND_SAVE(true, W, BRIDGE_TAG, "Mouse report failed (conn=%d notify=%d)",
                       m4g_ble_is_connected(), m4g_ble_notifications_enabled());
        }
      }
      else if (ENABLE_DEBUG_KEYPRESS_LOGGING)
      {
        LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, D, BRIDGE_TAG, "Duplicate mouse report suppressed");
      }
    }
  }
#else
  (void)allow_mouse;
  (void)mx;
  (void)my;
#endif
}

static void send_buffered_single_key(void)
{
  bool have_key = (s_chord_buffer_len > 0);
  bool have_modifier = (s_chord_buffer_modifiers != 0);
  if (!have_key && !have_modifier)
    return;

  uint8_t keys[6] = {0};
  if (have_key)
    keys[0] = s_chord_buffer[0];

  if (ENABLE_DEBUG_KEYPRESS_LOGGING)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                 "Single-key fallback: mod=0x%02X key=0x%02X", s_chord_buffer_modifiers, keys[0]);
  }

  emit_keyboard_state(s_chord_buffer_modifiers, keys, false, 0, 0);
  uint8_t empty[6] = {0};
  emit_keyboard_state(0, empty, false, 0, 0);
}

static void process_combined_state(const combined_state_t *state)
{
  if (!state)
    return;

  TickType_t now = xTaskGetTickCount();
  bool has_keys = (state->key_count > 0) || (state->modifiers != 0);
#ifdef CONFIG_M4G_ENABLE_ARROW_MOUSE
  bool has_activity = has_keys || (state->mouse_dx != 0) || (state->mouse_dy != 0);
#else
  bool has_activity = has_keys;
#endif

  if (!use_chord_for_state(state))
  {
    chord_buffer_reset();
    s_chord_state = CHORD_STATE_IDLE;
    s_output_sequence_active = false;
    s_expect_output_tick = now;
    emit_keyboard_state(state->modifiers, state->keys, true,
#ifdef CONFIG_M4G_ENABLE_ARROW_MOUSE
                        state->mouse_dx, state->mouse_dy
#else
                        0, 0
#endif
    );
    return;
  }

  switch (s_chord_state)
  {
  case CHORD_STATE_IDLE:
    if (has_activity)
    {
      chord_buffer_reset();
      chord_buffer_add(state);
      s_chord_state = CHORD_STATE_COLLECTING;
      if (ENABLE_DEBUG_KEYPRESS_LOGGING)
      {
        LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                     "Chord collecting started (keys=%u)", (unsigned)state->key_count);
      }
    }
    break;

  case CHORD_STATE_COLLECTING:
    if (has_activity)
    {
      chord_buffer_add(state);
    }
    else
    {
      // Physical keys released
      if (s_chord_buffer_len <= 1 && (s_chord_buffer_len > 0 || s_chord_buffer_modifiers != 0))
      {
        send_buffered_single_key();
        ++s_chord_processed;
        s_chord_state = CHORD_STATE_IDLE;
      }
      else if (s_chord_buffer_len > 1)
      {
        s_expect_output_tick = now;
        s_output_sequence_active = false;
        s_chord_state = CHORD_STATE_EXPECTING_OUTPUT;
        ++s_chord_delayed;
        if (ENABLE_DEBUG_KEYPRESS_LOGGING)
        {
          LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                       "Chord released (%u keys) awaiting output", (unsigned)s_chord_buffer_len);
        }
      }
      else
      {
        s_chord_state = CHORD_STATE_IDLE;
      }
      chord_buffer_reset();
    }
    break;

  case CHORD_STATE_EXPECTING_OUTPUT:
    if (has_activity)
    {
      TickType_t delta = now - s_expect_output_tick;
      if (delta <= pdMS_TO_TICKS(M4G_CHORD_OUTPUT_GRACE_MS))
      {
        s_chord_state = CHORD_STATE_PASSING_OUTPUT;
        s_output_sequence_active = true;
        ++s_chord_processed;
        emit_keyboard_state(state->modifiers, state->keys, true,
#ifdef CONFIG_M4G_ENABLE_ARROW_MOUSE
                            state->mouse_dx, state->mouse_dy
#else
                            0, 0
#endif
        );
      }
      else
      {
        // Treat as a new chord start if the gap exceeded grace window
        chord_buffer_reset();
        chord_buffer_add(state);
        s_chord_state = CHORD_STATE_COLLECTING;
        if (ENABLE_DEBUG_KEYPRESS_LOGGING)
        {
          LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, W, BRIDGE_TAG,
                       "No chord output within grace window; restarting collection");
        }
      }
    }
    else if (s_output_sequence_active && (now - s_expect_output_tick) > pdMS_TO_TICKS(M4G_CHORD_OUTPUT_GRACE_MS))
    {
      s_chord_state = CHORD_STATE_IDLE;
      s_output_sequence_active = false;
    }
    break;

  case CHORD_STATE_PASSING_OUTPUT:
    emit_keyboard_state(state->modifiers, state->keys, true,
#ifdef CONFIG_M4G_ENABLE_ARROW_MOUSE
                        state->mouse_dx, state->mouse_dy
#else
                        0, 0
#endif
    );
    if (!has_activity)
    {
      s_expect_output_tick = now;
      s_chord_state = CHORD_STATE_EXPECTING_OUTPUT;
    }
    break;
  }
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

void m4g_bridge_process_usb_report(uint8_t slot, const uint8_t *report, size_t len, bool is_charachorder)
{
  if (!report || len == 0)
    return;

  if (slot >= M4G_BRIDGE_MAX_SLOTS)
  {
    if (!s_warned_invalid_slot)
    {
      LOG_AND_SAVE(true, W, BRIDGE_TAG, "Ignoring report for invalid slot %u", slot);
      s_warned_invalid_slot = true;
    }
    return;
  }

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
  {
    if (ENABLE_DEBUG_KEYPRESS_LOGGING)
    {
      LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, W, BRIDGE_TAG, "Slot %u report too short (len=%zu)", slot, len);
    }
    return;
  }

  bridge_slot_state_t *state = &s_slots[slot];
  uint8_t slot_keys[6] = {0};
  size_t key_count = extract_chara_keys(kb_payload, kb_len, slot_keys);

  state->present = true;
  state->is_charachorder = is_charachorder;
  state->modifiers = (kb_len >= 1) ? kb_payload[0] : 0;
  memset(state->keys, 0, sizeof(state->keys));
  if (key_count > 0)
    memcpy(state->keys, slot_keys, key_count);

  if (ENABLE_DEBUG_KEYPRESS_LOGGING)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                 "Slot %u update: mod=0x%02X keys=[0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X]",
                 slot, state->modifiers,
                 state->keys[0], state->keys[1], state->keys[2],
                 state->keys[3], state->keys[4], state->keys[5]);
  }

  combined_state_t combined;
  compute_combined_state(&combined);
  process_combined_state(&combined);
}

void m4g_bridge_reset_slot(uint8_t slot)
{
  if (slot >= M4G_BRIDGE_MAX_SLOTS)
    return;

  if (ENABLE_DEBUG_KEYPRESS_LOGGING)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG, "Resetting slot %u", slot);
  }
  chord_buffer_reset();
  s_chord_state = CHORD_STATE_IDLE;
  s_expect_output_tick = xTaskGetTickCount();

  memset(s_slots[slot].keys, 0, sizeof(s_slots[slot].keys));
  s_slots[slot].modifiers = 0;
  s_slots[slot].present = false;
  s_slots[slot].is_charachorder = false;

  uint8_t empty_keys[6] = {0};
  emit_keyboard_state(0, empty_keys, false, 0, 0);
}
