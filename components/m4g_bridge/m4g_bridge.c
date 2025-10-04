#include "m4g_bridge.h"
#include "m4g_ble.h"
#include "m4g_logging.h"
#include "m4g_settings.h"
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

// Chord detection timing (runtime configurable)
// - Chord delay: max time between key presses to be considered simultaneous
// - Chord timeout: max time to wait for CharaChorder's chord output
#define M4G_CHORD_COLLECT_DELAY_MS m4g_settings_get_chord_delay_ms()
#define M4G_CHORD_OUTPUT_GRACE_MS m4g_settings_get_chord_timeout_ms()

// Mouse configuration defaults (fallback if not in sdkconfig)
#ifndef CONFIG_M4G_MOUSE_BASE_SPEED
#define CONFIG_M4G_MOUSE_BASE_SPEED 5
#endif

#ifndef CONFIG_M4G_MOUSE_ACCEL_INCREMENT
#define CONFIG_M4G_MOUSE_ACCEL_INCREMENT 3
#endif

#ifndef CONFIG_M4G_MOUSE_ACCEL_INTERVAL_MS
#define CONFIG_M4G_MOUSE_ACCEL_INTERVAL_MS 100
#endif

#ifndef CONFIG_M4G_MOUSE_MAX_SPEED
#define CONFIG_M4G_MOUSE_MAX_SPEED 40
#endif

#define USB_MOUSE_HOLD_THRESHOLD_MS 50 // Consider "held" after 50ms

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

// USB mouse acceleration tracking
static TickType_t s_usb_mouse_last_move_time = 0;
static int8_t s_usb_mouse_last_dx = 0;
static int8_t s_usb_mouse_last_dy = 0;
static TickType_t s_usb_mouse_accel_start_time = 0;

#define USB_MOUSE_RELEASE_TIMEOUT_MS 200 // Consider released after 200ms of no movement
static uint32_t s_chord_processed = 0;
static uint32_t s_chord_delayed = 0;
static bool s_charachorder_detected = false;
static bool s_charachorder_both_halves = false;

#ifdef CONFIG_M4G_ENABLE_ARROW_MOUSE
// Track when each arrow key was first pressed for acceleration
static TickType_t s_arrow_key_press_time[4] = {0}; // [up, down, left, right]
static uint8_t s_last_arrow_keys[4] = {0};         // Track which keys were pressed last
#endif

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
static bool s_filter_backspaces = false; // Filter backspaces during chord output
static TickType_t s_last_chord_release_tick = 0;
static bool s_just_filtered_backspace = false;    // Flag to extend grace period
static TickType_t s_chord_collect_start_tick = 0; // When chord collection started

// Chord deviation tracking (for quality metrics)
static TickType_t s_first_key_press_tick = 0; // When first key in chord was pressed
static TickType_t s_last_key_press_tick = 0;  // When last key in chord was pressed
static size_t s_chord_key_count_peak = 0;     // Max keys pressed simultaneously

#ifdef CONFIG_M4G_ENABLE_KEY_REPEAT
// Key repeat state
static uint8_t s_last_key = 0;               // Last non-zero key pressed
static uint8_t s_last_modifiers = 0;         // Modifiers for last key
static TickType_t s_last_key_press_time = 0; // When key was first pressed
static TickType_t s_last_repeat_time = 0;    // When last repeat was sent
static bool s_repeat_started = false;        // Whether we've started repeating
static bool s_in_repeat_emit = false;        // Flag to prevent recursion
static bool s_repeat_active = false;         // Whether repeat logic is currently bypassing chord mode
#endif

static void compute_combined_state(combined_state_t *state);
static void process_combined_state(const combined_state_t *state);
static bool chord_mode_enabled(void);
static bool use_chord_for_state(const combined_state_t *state);
static void emit_keyboard_state(uint8_t modifiers, const uint8_t keys[6], bool allow_mouse, int mx, int my);
static void chord_buffer_reset(void);
static void chord_buffer_add(const combined_state_t *state);
#ifdef CONFIG_M4G_ENABLE_KEY_REPEAT
static void emit_repeat_cycle(uint8_t key, uint8_t modifiers);
#endif

static size_t extract_chara_keys(const uint8_t *kb_payload, size_t len, uint8_t *out6, bool is_charachorder)
{
  size_t n = 0;
  if (len < 8)
    return 0;

  // Check if we should filter backspaces (during chord output phase)
  // Allow a 500ms window after chord release for CharaChorder to send cleanup backspaces
  bool filter_backspace_now = false;
  if (is_charachorder && s_filter_backspaces)
  {
    TickType_t elapsed = xTaskGetTickCount() - s_last_chord_release_tick;
    if (elapsed < pdMS_TO_TICKS(500))
    {
      filter_backspace_now = true;
    }
    else
    {
      // Timeout expired - stop filtering
      s_filter_backspaces = false;
    }
  }

  // Track if we filtered any backspaces (to extend grace period)
  s_just_filtered_backspace = false;

  for (size_t i = 2; i < len && n < 6; ++i)
  {
    uint8_t key = kb_payload[i];
    // Filter out HID error codes (0x01-0x03) and zeros
    // 0x01 = ErrorRollOver (too many keys/buffer overflow)
    // 0x02 = POSTFail
    // 0x03 = ErrorUndefined
    // Also filter out 0x2A (Backspace/Delete) during CharaChorder chord output
    // CharaChorder sends backspaces to erase individual chord keys,
    // but we pass keys through immediately so there's nothing to erase
    bool is_backspace = (key == 0x2A);
    bool should_filter = (is_backspace && filter_backspace_now);

    if (should_filter)
    {
      s_just_filtered_backspace = true;
    }

    if (key != 0 && key > 0x03 && !should_filter)
      out6[n++] = key;
  }

  return n;
}

static void chord_buffer_reset(void)
{
  memset(s_chord_buffer, 0, sizeof(s_chord_buffer));
  s_chord_buffer_len = 0;
  s_chord_buffer_modifiers = 0;
  s_output_sequence_active = false;

  // Reset deviation tracking
  s_first_key_press_tick = 0;
  s_last_key_press_tick = 0;
  s_chord_key_count_peak = 0;
}

static void chord_buffer_add(const combined_state_t *state)
{
  TickType_t now = xTaskGetTickCount();
  bool added_new_key = false;

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
      added_new_key = true;
    }
  }

  // Track press timing for deviation metrics
  if (added_new_key)
  {
    if (s_first_key_press_tick == 0)
    {
      s_first_key_press_tick = now;
    }
    s_last_key_press_tick = now;
  }

  // Track peak simultaneous key count
  if (state->key_count > s_chord_key_count_peak)
  {
    s_chord_key_count_peak = state->key_count;
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

// Apply acceleration to USB mouse movement based on continuous direction
static void apply_usb_mouse_acceleration(int8_t *dx, int8_t *dy)
{
  TickType_t now = xTaskGetTickCount();

  // Determine movement direction (normalize to -1, 0, or 1)
  int8_t dir_x = (*dx > 0) ? 1 : ((*dx < 0) ? -1 : 0);
  int8_t dir_y = (*dy > 0) ? 1 : ((*dy < 0) ? -1 : 0);

  // Check if movement stopped (timeout for key release detection)
  TickType_t time_since_last = now - s_usb_mouse_last_move_time;
  uint32_t idle_ms = time_since_last * portTICK_PERIOD_MS;

  if (idle_ms > USB_MOUSE_RELEASE_TIMEOUT_MS || (dir_x == 0 && dir_y == 0))
  {
    // No movement for timeout period or explicit stop - reset acceleration
    if (ENABLE_DEBUG_KEYPRESS_LOGGING && s_usb_mouse_accel_start_time != 0)
    {
      LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                   "Mouse accel RESET (idle=%lums, timeout=%ums)",
                   (unsigned long)idle_ms, USB_MOUSE_RELEASE_TIMEOUT_MS);
    }
    s_usb_mouse_last_dx = dir_x;
    s_usb_mouse_last_dy = dir_y;
    s_usb_mouse_last_move_time = now;
    s_usb_mouse_accel_start_time = 0; // Reset start time

    // Tap = 5px
    if (dir_x != 0)
      *dx = dir_x * 5;
    if (dir_y != 0)
      *dy = dir_y * 5;
    return;
  }

  // Movement detected - update last move time
  s_usb_mouse_last_move_time = now;

  // If this is the first movement after idle, start acceleration timer
  if (s_usb_mouse_accel_start_time == 0)
  {
    s_usb_mouse_accel_start_time = now;
  }

  // Calculate time since movement started
  TickType_t accel_duration = now - s_usb_mouse_accel_start_time;
  uint32_t accel_ms = accel_duration * portTICK_PERIOD_MS;

  int speed;
  if (accel_ms < USB_MOUSE_HOLD_THRESHOLD_MS)
  {
    // Still in tap mode (< 50ms)
    speed = 5;
  }
  else if (accel_ms < 1000)
  {
    // Held: ramp from 10px to 40px over 1 second
    // Linear interpolation: 10 + (30 * progress)
    uint32_t held_ms = accel_ms - USB_MOUSE_HOLD_THRESHOLD_MS;
    speed = 10 + (30 * held_ms / 1000);
  }
  else
  {
    // After 1 second, stay at max
    speed = 40;
  }

  if (ENABLE_DEBUG_KEYPRESS_LOGGING && accel_ms > USB_MOUSE_HOLD_THRESHOLD_MS)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                 "Mouse accel: speed=%dpx (held_ms=%lu)",
                 speed, (unsigned long)(accel_ms - USB_MOUSE_HOLD_THRESHOLD_MS));
  }

  // Apply speed
  if (dir_x != 0)
    *dx = dir_x * speed;
  if (dir_y != 0)
    *dy = dir_y * speed;

  // Update direction tracking
  s_usb_mouse_last_dx = dir_x;
  s_usb_mouse_last_dy = dir_y;
}

#ifdef CONFIG_M4G_ENABLE_ARROW_MOUSE
// Calculate accelerated mouse speed based on how long key has been held
static int calculate_mouse_speed(uint8_t keycode, size_t arrow_index)
{
  TickType_t now = xTaskGetTickCount();
  int base_speed = CONFIG_M4G_MOUSE_BASE_SPEED;

  // Check if this is a new key press or continuation
  if (s_last_arrow_keys[arrow_index] != keycode)
  {
    // New key press - reset timer and use base speed
    s_last_arrow_keys[arrow_index] = keycode;
    s_arrow_key_press_time[arrow_index] = now;
    return base_speed;
  }

#ifdef CONFIG_M4G_MOUSE_ENABLE_ACCELERATION
  // Key is being held - calculate acceleration
  TickType_t held_duration = now - s_arrow_key_press_time[arrow_index];
  uint32_t held_ms = held_duration * portTICK_PERIOD_MS;

  // Calculate speed: base + (increments based on time held)
  uint32_t accel_steps = held_ms / CONFIG_M4G_MOUSE_ACCEL_INTERVAL_MS;
  int speed = base_speed + (accel_steps * CONFIG_M4G_MOUSE_ACCEL_INCREMENT);

  // Cap at maximum speed
  if (speed > CONFIG_M4G_MOUSE_MAX_SPEED)
  {
    speed = CONFIG_M4G_MOUSE_MAX_SPEED;
  }

  return speed;
#else
  return base_speed;
#endif
}

// Reset arrow key tracking when key is released
static void reset_arrow_key_if_released(uint8_t keycode, size_t arrow_index, bool is_pressed)
{
  if (!is_pressed && s_last_arrow_keys[arrow_index] == keycode)
  {
    s_last_arrow_keys[arrow_index] = 0;
    s_arrow_key_press_time[arrow_index] = 0;
  }
}
#endif

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

  // First pass: calculate mouse movement and identify mouse keys
#ifdef CONFIG_M4G_ENABLE_ARROW_MOUSE
  int mx = 0;
  int my = 0;
  bool is_mouse_key[6] = {false};
  bool arrow_pressed[4] = {false}; // [up, down, left, right]

  for (size_t i = 0; i < combined_count; ++i)
  {
    switch (combined_keys[i])
    {
    case 0x29: // Escape - Mouse Up
      my -= calculate_mouse_speed(0x29, 0);
      is_mouse_key[i] = true;
      arrow_pressed[0] = true;
      break;
    case 0x2A: // Backspace - Mouse Down
      my += calculate_mouse_speed(0x2A, 1);
      is_mouse_key[i] = true;
      arrow_pressed[1] = true;
      break;
    case 0x38: // Forward Slash - Mouse Left
      mx -= calculate_mouse_speed(0x38, 2);
      is_mouse_key[i] = true;
      arrow_pressed[2] = true;
      break;
    case 0x2E: // Period - Mouse Right
      mx += calculate_mouse_speed(0x2E, 3);
      is_mouse_key[i] = true;
      arrow_pressed[3] = true;
      break;
    default:
      break;
    }
  }
  state->mouse_dx = mx;
  state->mouse_dy = my;

  // Reset tracking for any arrow keys that were released
  reset_arrow_key_if_released(0x29, 0, arrow_pressed[0]);
  reset_arrow_key_if_released(0x2A, 1, arrow_pressed[1]);
  reset_arrow_key_if_released(0x38, 2, arrow_pressed[2]);
  reset_arrow_key_if_released(0x2E, 3, arrow_pressed[3]);

  // Second pass: filter out mouse keys from keyboard report if mouse movement detected
  if (mx != 0 || my != 0)
  {
    size_t filtered_count = 0;
    for (size_t i = 0; i < combined_count; ++i)
    {
      if (!is_mouse_key[i])
      {
        state->keys[filtered_count++] = combined_keys[i];
      }
    }
    // Zero out remaining slots
    for (size_t i = filtered_count; i < 6; ++i)
    {
      state->keys[i] = 0;
    }
    state->key_count = filtered_count;

    if (ENABLE_DEBUG_KEYPRESS_LOGGING && filtered_count != combined_count)
    {
      LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                   "Filtered mouse keys: %u -> %u keys, mouse dx=%d dy=%d",
                   (unsigned)combined_count, (unsigned)filtered_count, mx, my);
    }
  }
  else
  {
    // No mouse movement - keep all keys in keyboard report
    memcpy(state->keys, combined_keys, sizeof(state->keys));
    state->key_count = combined_count;
  }
#else
  // No arrow-to-mouse conversion - just copy all keys
  memcpy(state->keys, combined_keys, sizeof(state->keys));
  state->key_count = combined_count;
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
                 "Emit report: mod=0x%02X keys=[0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X] allow_mouse=%d mx=%d my=%d",
                 kb_report[0], kb_report[2], kb_report[3], kb_report[4],
                 kb_report[5], kb_report[6], kb_report[7], allow_mouse, mx, my);
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
      if (ENABLE_DEBUG_KEYPRESS_LOGGING)
      {
        LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                     "Mouse movement: dx=%d dy=%d", mx, my);
      }

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

#ifdef CONFIG_M4G_ENABLE_KEY_REPEAT
  // Track key for repeat functionality (skip if this is a repeat emission)
  if (ENABLE_DEBUG_KEYPRESS_LOGGING)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                 "emit_keyboard_state tracking: s_in_repeat_emit=%d keys[0]=0x%02X",
                 s_in_repeat_emit, keys[0]);
  }

  if (!s_in_repeat_emit)
  {
    TickType_t now = xTaskGetTickCount();

    // Count how many keys are pressed
    int key_count = 0;
    uint8_t current_key = 0;
    for (int i = 0; i < 6; i++)
    {
      if (keys[i] != 0)
      {
        if (current_key == 0)
          current_key = keys[i]; // Remember first key
        key_count++;
      }
    }

    // If multiple keys are pressed, disable key repeat
    // (this is likely a chord or intentional multi-key combination)
    if (key_count > 1)
    {
      s_last_key = 0; // Disable repeat
      s_repeat_started = false;
      s_repeat_active = false;
      if (ENABLE_DEBUG_KEYPRESS_LOGGING)
      {
        LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                     "Multi-key detected - disabling repeat");
      }
    }
    // Check if key changed or released
    else if (current_key != s_last_key || modifiers != s_last_modifiers)
    {
      if (ENABLE_DEBUG_KEYPRESS_LOGGING)
      {
        LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                     "Key state change: last=0x%02X current=0x%02X repeat_started=%d",
                     s_last_key, current_key, s_repeat_started);
      }
      // Key changed - reset repeat state
      s_last_key = current_key;
      s_last_modifiers = modifiers;
      s_last_key_press_time = now;
      s_repeat_started = false;
      s_repeat_active = false;
    }
    // Same key still held - keep original press time
  }
  else
  {
    if (ENABLE_DEBUG_KEYPRESS_LOGGING)
    {
      LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                   "Skipping key tracking (in repeat emit)");
    }
  }
#endif
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

  bool use_chord = use_chord_for_state(state);

#ifdef CONFIG_M4G_ENABLE_KEY_REPEAT
  if (s_repeat_active && has_keys)
  {
    use_chord = false;
  }

  if (!has_keys)
  {
    s_repeat_active = false;
  }
#endif

  if (ENABLE_DEBUG_KEYPRESS_LOGGING && has_keys)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                 "process_combined_state: use_chord=%d charachorder=%d keys=%d",
                 use_chord, state->any_charachorder, state->key_count);
  }

  // Track multi-key sequences for backspace filtering (works in both chord and RAW mode)
  static size_t s_last_key_count = 0;

  if (state->any_charachorder)
  {
    if (state->key_count == 0 && s_last_key_count >= 2)
    {
      // Multi-key chord just released - enable backspace filtering
      s_filter_backspaces = true;
      s_last_chord_release_tick = now;
      if (ENABLE_DEBUG_KEYPRESS_LOGGING)
      {
        LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                     "Multi-key release detected - enabling backspace filter for 500ms");
      }
    }
    s_last_key_count = state->key_count;
  }

  if (!use_chord)
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
      s_chord_collect_start_tick = now;

#ifdef CONFIG_M4G_ENABLE_KEY_REPEAT
      // Stop any active key repeat when entering chord collection
      s_last_key = 0;
      s_repeat_started = false;
      s_repeat_active = false;
#endif

      if (ENABLE_DEBUG_KEYPRESS_LOGGING)
      {
        LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                     "Chord collecting started (keys=%u)", (unsigned)state->key_count);
      }
    }
#ifdef CONFIG_M4G_ENABLE_KEY_REPEAT
    else if (s_last_key != 0)
    {
      // Key is being tracked for repeat - don't emit release yet
      // The repeat system will handle it when key actually releases
      if (ENABLE_DEBUG_KEYPRESS_LOGGING)
      {
        LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                     "Suppressing IDLE release - key repeat active for 0x%02X", s_last_key);
      }
    }
#endif
    else
    {
      // No activity in IDLE state - this is a key release, pass it through
      emit_keyboard_state(state->modifiers, state->keys, true,
#ifdef CONFIG_M4G_ENABLE_ARROW_MOUSE
                          state->mouse_dx, state->mouse_dy
#else
                          0, 0
#endif
      );
    }
    break;

  case CHORD_STATE_COLLECTING:
    if (has_activity)
    {
      chord_buffer_add(state);

      // If we now have multiple keys, we're definitely in a chord
      if (s_chord_buffer_len >= 2)
      {
        if (ENABLE_DEBUG_KEYPRESS_LOGGING)
        {
          LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                       "Multi-key chord detected (%u keys)", (unsigned)s_chord_buffer_len);
        }
      }
    }
    else
    {
      // Physical keys released
      TickType_t collect_duration = now - s_chord_collect_start_tick;

      // If single key released quickly (< chord timeout), emit immediately
      // This handles normal typing - no need to wait for CharaChorder
      if (s_chord_buffer_len == 1 && collect_duration < pdMS_TO_TICKS(m4g_settings_get_chord_timeout_ms()))
      {
        // Quick single keypress - send immediately (press then release)
        uint8_t keys[6] = {s_chord_buffer[0], 0, 0, 0, 0, 0};
        emit_keyboard_state(s_chord_buffer_modifiers, keys, true, 0, 0);
        uint8_t empty[6] = {0};
        emit_keyboard_state(0, empty, true, 0, 0); // Send key release

        if (ENABLE_DEBUG_KEYPRESS_LOGGING)
        {
          LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                       "Quick single key (0x%02X) - sent immediately", keys[0]);
        }

        chord_buffer_reset();
        s_chord_state = CHORD_STATE_IDLE;

#ifdef CONFIG_M4G_ENABLE_KEY_REPEAT
        s_last_key = 0;
        s_repeat_started = false;
        s_repeat_active = false;
#endif
      }
      else
      {
        // Either multi-key OR single key held long enough - wait for CharaChorder output
        s_expect_output_tick = now;
        s_output_sequence_active = false;
        s_chord_state = CHORD_STATE_EXPECTING_OUTPUT;
        s_filter_backspaces = true; // Start filtering backspaces during chord output
        s_last_chord_release_tick = now;
#ifdef CONFIG_M4G_ENABLE_KEY_REPEAT
        if (s_chord_buffer_len == 1)
        {
          s_repeat_active = true;
        }
#endif

        if (ENABLE_DEBUG_KEYPRESS_LOGGING)
        {
          LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                       "Chord released (%u keys, %ums held) awaiting CharaChorder output",
                       (unsigned)s_chord_buffer_len,
                       (unsigned)pdTICKS_TO_MS(collect_duration));

          // Log chord quality metrics if deviation tracking is enabled
          if (m4g_settings_is_deviation_tracking_enabled() && s_chord_buffer_len >= 2)
          {
            // Calculate press deviation (time from first to last key press)
            uint32_t press_deviation_ms = 0;
            if (s_last_key_press_tick > s_first_key_press_tick)
            {
              press_deviation_ms = pdTICKS_TO_MS(s_last_key_press_tick - s_first_key_press_tick);
            }

            // Determine chord quality based on CharaChorder thresholds
            // Perfect: <= 10ms per key, Good: <= 25ms per key, Acceptable: <= per-key thresholds
            const char *quality = "ACCEPTABLE";
            uint32_t per_key_press_threshold = m4g_settings_get_chord_press_deviation_max_ms();
            uint32_t perfect_threshold = 10 * (s_chord_buffer_len - 1); // 10ms per additional key
            uint32_t good_threshold = 25 * (s_chord_buffer_len - 1);    // 25ms per additional key

            if (press_deviation_ms <= perfect_threshold)
            {
              quality = "PERFECT";
            }
            else if (press_deviation_ms <= good_threshold)
            {
              quality = "GOOD";
            }
            else if (press_deviation_ms > per_key_press_threshold)
            {
              quality = "POOR";
            }

            LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                         "Chord quality: %s (press_deviation=%lums, peak_keys=%u)",
                         quality, (unsigned long)press_deviation_ms, (unsigned)s_chord_key_count_peak);
          }
        }
      }
    }
    break;

  case CHORD_STATE_EXPECTING_OUTPUT:
    // If we just filtered a backspace, extend the grace period
    // CharaChorder sends backspaces before the actual word output
    if (s_just_filtered_backspace)
    {
      s_expect_output_tick = now;
      s_just_filtered_backspace = false;
    }

    // Check if timeout expired FIRST (before processing new activity)
    TickType_t delta = now - s_expect_output_tick;
    if (delta > pdMS_TO_TICKS(M4G_CHORD_OUTPUT_GRACE_MS))
    {
      // Timeout - CharaChorder didn't output anything, so this wasn't a chord
      // For multi-key combinations or held single keys, just discard the buffer
      // (user attempted a chord but it didn't match anything)
      if (ENABLE_DEBUG_KEYPRESS_LOGGING)
      {
        LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                     "Timeout - discarding %u buffered key(s) (failed chord attempt)",
                     (unsigned)s_chord_buffer_len);
      }

      chord_buffer_reset();
      s_chord_state = CHORD_STATE_IDLE;
      s_output_sequence_active = false;

      // Now process new activity if present (start fresh from IDLE state)
      if (has_activity)
      {
        chord_buffer_add(state);
        s_chord_state = CHORD_STATE_COLLECTING;
        s_chord_collect_start_tick = now;
#ifdef CONFIG_M4G_ENABLE_KEY_REPEAT
        if (s_chord_buffer_len == 1)
        {
          s_repeat_active = true;
        }
#endif
      }
    }
    else if (has_activity)
    {
      // Within grace window and have activity
      if (delta <= pdMS_TO_TICKS(M4G_CHORD_OUTPUT_GRACE_MS))
      {
        // CharaChorder sent output - this was a real chord, pass it through
        s_chord_state = CHORD_STATE_PASSING_OUTPUT;
        s_output_sequence_active = true;
        ++s_chord_processed;
        chord_buffer_reset(); // Clear buffer since CharaChorder handled it
        emit_keyboard_state(state->modifiers, state->keys, true,
#ifdef CONFIG_M4G_ENABLE_ARROW_MOUSE
                            state->mouse_dx, state->mouse_dy
#else
                            0, 0
#endif
        );
      }
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

#ifdef CONFIG_M4G_ENABLE_KEY_REPEAT
static void emit_repeat_cycle(uint8_t key, uint8_t modifiers)
{
  if (key == 0)
    return;

  uint8_t release_keys[6] = {0};
  uint8_t press_keys[6] = {key, 0, 0, 0, 0, 0};

  s_in_repeat_emit = true;

  emit_keyboard_state(modifiers, release_keys, false, 0, 0);
  emit_keyboard_state(modifiers, press_keys, false, 0, 0);

  s_in_repeat_emit = false;

  if (ENABLE_DEBUG_KEYPRESS_LOGGING)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                 "Repeat cycle emitted for key=0x%02X mods=0x%02X", key, modifiers);
  }
}
#endif

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

  // Check for mouse report (Report ID 0x02) - forward to BLE
  if (len > 0 && report[0] == 0x02)
  {
    // Mouse report format: [0x02, buttons, dx, dy, ...]
    // We need at least 4 bytes: Report ID + buttons + dx + dy
    if (len >= 4)
    {
      // Extract and apply acceleration to mouse movement
      uint8_t mouse[3];
      mouse[0] = report[1]; // buttons
      int8_t dx = (int8_t)report[2];
      int8_t dy = (int8_t)report[3];

      // Apply acceleration to USB mouse movement
      apply_usb_mouse_acceleration(&dx, &dy);

      mouse[1] = (uint8_t)dx;
      mouse[2] = (uint8_t)dy;

      if (ENABLE_DEBUG_KEYPRESS_LOGGING)
      {
        LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                     "USB Mouse: buttons=0x%02X dx=%d dy=%d (accelerated from %d,%d)",
                     mouse[0], (int8_t)mouse[1], (int8_t)mouse[2],
                     (int8_t)report[2], (int8_t)report[3]);
      }

      if (m4g_ble_send_mouse_report(mouse))
      {
        ++s_mouse_sent;
      }
      else
      {
        LOG_AND_SAVE(true, W, BRIDGE_TAG, "USB mouse report forward failed");
      }
    }
    return;
  }

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
  size_t key_count = extract_chara_keys(kb_payload, kb_len, slot_keys, is_charachorder);

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

void m4g_bridge_process_key_repeat(void)
{
#ifdef CONFIG_M4G_ENABLE_KEY_REPEAT
  TickType_t now = xTaskGetTickCount();

  // First, check if a single key has been held long enough to start repeat.
  // Once the configured repeat delay elapses, treat it as a held key even in chord mode.
  if (s_chord_state == CHORD_STATE_COLLECTING && s_chord_buffer_len == 1)
  {
    TickType_t collect_duration = now - s_chord_collect_start_tick;
    uint32_t repeat_delay_ms = m4g_settings_get_key_repeat_delay_ms();

    if (collect_duration >= pdMS_TO_TICKS(repeat_delay_ms))
    {
      // Timeout expired - transition to a special "HELD" state for single keys
      // Build the key array from buffer
      uint8_t keys[6] = {0};
      uint8_t held_key = 0;
      uint8_t held_modifiers = 0;

      if (s_chord_buffer_len > 0)
      {
        held_key = s_chord_buffer[0];
        held_modifiers = s_chord_buffer_modifiers;
        keys[0] = held_key;
      }

      // Emit the key and immediately set up repeat tracking
      emit_keyboard_state(held_modifiers, keys, true, 0, 0);

      // CRITICAL: Clear the chord buffer and go to IDLE state
      // This allows process_combined_state to detect key release properly
      chord_buffer_reset();
      s_chord_state = CHORD_STATE_IDLE;

      // Manually set up repeat tracking since we just emitted
      s_last_key = held_key;
      s_last_modifiers = held_modifiers;
      s_last_key_press_time = now;
      s_repeat_started = false;
      s_repeat_active = true;

      if (ENABLE_DEBUG_KEYPRESS_LOGGING)
      {
        LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                     "Single-key held (>= %ums) - emitting for repeat (key=0x%02X, transitioning to IDLE for repeat tracking)",
                     repeat_delay_ms, held_key);
      }
    }
  }

  // Only process repeat if a key is currently held
  if (s_last_key == 0)
  {
    s_repeat_started = false;
    s_repeat_active = false;
    return;
  }

  TickType_t elapsed = now - s_last_key_press_time;

  if (!s_repeat_started)
  {
    // Check if we've exceeded the initial delay
    uint32_t repeat_delay_ms = m4g_settings_get_key_repeat_delay_ms();
    if (elapsed >= pdMS_TO_TICKS(repeat_delay_ms))
    {
      // Start repeating
      s_repeat_started = true;
      s_last_repeat_time = now;
      s_repeat_active = true;

      // Send first repeat as release/press pair to ensure host sees transition
      emit_repeat_cycle(s_last_key, s_last_modifiers);

      if (ENABLE_DEBUG_KEYPRESS_LOGGING)
      {
        LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, BRIDGE_TAG,
                     "Key repeat started: key=0x%02X (after %ums)", s_last_key, repeat_delay_ms);
      }
    }
  }
  else
  {
    // Already repeating - check if it's time for next repeat
    TickType_t since_last_repeat = now - s_last_repeat_time;
    uint32_t repeat_rate_ms = m4g_settings_get_key_repeat_rate_ms();

    if (since_last_repeat >= pdMS_TO_TICKS(repeat_rate_ms))
    {
      s_last_repeat_time = now;

      // Send repeat as release/press pair
      emit_repeat_cycle(s_last_key, s_last_modifiers);
    }
  }
#endif
}
