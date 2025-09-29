#include "m4g_logging.h"
#include <string.h>
#include "esp_log.h"
#include "sdkconfig.h"
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#if CONFIG_M4G_LOG_PERSISTENCE
#include "nvs_flash.h"
#endif

// LOG_TAG removed (was unused) to silence -Wunused-variable warning

// Internal debug flags (can later be loaded from NVS or menuconfig)
bool ENABLE_DEBUG_LED_LOGGING = false;
bool ENABLE_DEBUG_USB_LOGGING = false;
bool ENABLE_DEBUG_BLE_LOGGING = true;
bool ENABLE_DEBUG_KEYPRESS_LOGGING = false;

#if CONFIG_M4G_LOG_PERSISTENCE
#define NVS_LOG_NAMESPACE "logbuf"
#define NVS_LOG_KEY "logs"
#define NVS_LOG_MAX_SIZE 2048
#define STAGE_BUF_SIZE CONFIG_M4G_LOG_BUFFER_SIZE
#define STAGE_FLUSH_THRESHOLD CONFIG_M4G_LOG_FLUSH_THRESHOLD
#define LOG_QUEUE_LENGTH 32
#define LOG_MAX_LINE_LEN 192

typedef enum
{
  LOG_MSG_LINE,
  LOG_MSG_FLUSH
} log_msg_type_t;

typedef struct
{
  log_msg_type_t type;
  uint16_t len;
  char line[LOG_MAX_LINE_LEN];
} log_msg_t;

static QueueHandle_t s_log_queue = NULL;
static TaskHandle_t s_log_task = NULL;
static bool s_nvs_ready = false;
static bool s_log_queue_overflowed = false;
static char s_stage_buf[STAGE_BUF_SIZE];
static size_t s_stage_len = 0;
static bool s_persistence_enabled = true;

static void flush_stage_buffer(void);
static void append_line_to_stage(const char *line, size_t len);
static void ensure_log_task(void);
#endif

#if CONFIG_M4G_LOG_PERSISTENCE
static void flush_stage_buffer(void)
{
  if (!s_persistence_enabled || !s_nvs_ready || s_stage_len == 0)
    return;
  s_stage_buf[s_stage_len] = '\0';
  nvs_log_append_internal(s_stage_buf);
  s_stage_len = 0;
}

static void append_line_to_stage(const char *line, size_t len)
{
  if (!s_persistence_enabled)
    return;
  if (len >= LOG_MAX_LINE_LEN)
    len = LOG_MAX_LINE_LEN - 1;
  if (len + 1 >= STAGE_BUF_SIZE)
    len = STAGE_BUF_SIZE - 2;

  if ((s_stage_len + len + 1) >= STAGE_BUF_SIZE)
  {
    flush_stage_buffer();
    if ((s_stage_len + len + 1) >= STAGE_BUF_SIZE)
    {
      size_t keep = STAGE_BUF_SIZE / 2;
      if (s_stage_len > keep)
      {
        memmove(s_stage_buf, s_stage_buf + (s_stage_len - keep), keep);
        s_stage_len = keep;
      }
      else
      {
        s_stage_len = 0;
      }
    }
  }

  memcpy(&s_stage_buf[s_stage_len], line, len);
  s_stage_len += len;
  s_stage_buf[s_stage_len++] = '\n';

  if (s_stage_len >= STAGE_FLUSH_THRESHOLD)
    flush_stage_buffer();
}

static void log_worker(void *arg)
{
  log_msg_t msg;
  (void)arg;
  while (xQueueReceive(s_log_queue, &msg, portMAX_DELAY) == pdTRUE)
  {
    if (!s_persistence_enabled)
      continue;
    if (msg.type == LOG_MSG_LINE)
    {
      append_line_to_stage(msg.line, msg.len);
    }
    else if (msg.type == LOG_MSG_FLUSH)
    {
      flush_stage_buffer();
    }
  }
}

static void ensure_log_task(void)
{
  if (!s_persistence_enabled)
    return;
  if (s_log_queue && s_log_task)
    return;
  if (!s_log_queue)
  {
    s_log_queue = xQueueCreate(LOG_QUEUE_LENGTH, sizeof(log_msg_t));
    if (!s_log_queue)
    {
      ESP_LOGW("M4G-LOG", "Failed to allocate log queue; persistence disabled");
      return;
    }
  }
  if (!s_log_task)
  {
    BaseType_t rc = xTaskCreatePinnedToCore(log_worker, "m4g_log", 3072, NULL, tskIDLE_PRIORITY + 1, &s_log_task, tskNO_AFFINITY);
    if (rc != pdPASS)
    {
      ESP_LOGW("M4G-LOG", "Failed to start log task; persistence disabled");
      vQueueDelete(s_log_queue);
      s_log_queue = NULL;
      return;
    }
  }
}
#endif

#if CONFIG_M4G_LOG_PERSISTENCE
static void nvs_log_append_internal(const char *msg)
{
  nvs_handle_t nvs;
  esp_err_t err = nvs_open(NVS_LOG_NAMESPACE, NVS_READWRITE, &nvs);
  if (err != ESP_OK)
    return;
  size_t required_size = 0;
  err = nvs_get_blob(nvs, NVS_LOG_KEY, NULL, &required_size);
  char buf[NVS_LOG_MAX_SIZE] = {0};
  size_t len = 0;
  if (err == ESP_OK && required_size > 0 && required_size < NVS_LOG_MAX_SIZE)
  {
    nvs_get_blob(nvs, NVS_LOG_KEY, buf, &required_size);
    len = required_size;
  }
  size_t msg_len = strlen(msg);
  if (len + msg_len + 2 < NVS_LOG_MAX_SIZE)
  {
    memcpy(buf + len, msg, msg_len);
    buf[len + msg_len] = '\n';
    len += msg_len + 1;
    nvs_set_blob(nvs, NVS_LOG_KEY, buf, len);
    nvs_commit(nvs);
  }
  nvs_close(nvs);
}
#endif

void m4g_log_dump_and_clear(void)
{
#if CONFIG_M4G_LOG_PERSISTENCE
  if (!s_persistence_enabled)
    return;
  m4g_log_flush();
  nvs_handle_t nvs;
  esp_err_t err = nvs_open(NVS_LOG_NAMESPACE, NVS_READWRITE, &nvs);
  if (err != ESP_OK)
    return;
  size_t required_size = 0;
  err = nvs_get_blob(nvs, NVS_LOG_KEY, NULL, &required_size);
  if (err == ESP_OK && required_size > 0 && required_size < NVS_LOG_MAX_SIZE)
  {
    char buf[NVS_LOG_MAX_SIZE] = {0};
    nvs_get_blob(nvs, NVS_LOG_KEY, buf, &required_size);
    printf("\n--- Stored Logs from NVS ---\n");
    printf("%.*s\n", (int)required_size, buf);
    printf("--- End of Stored Logs ---\n\n");
    nvs_erase_key(nvs, NVS_LOG_KEY);
    nvs_commit(nvs);
  }
  nvs_close(nvs);
#endif
}

void m4g_log_enable_led(bool en) { ENABLE_DEBUG_LED_LOGGING = en; }
void m4g_log_enable_usb(bool en) { ENABLE_DEBUG_USB_LOGGING = en; }
void m4g_log_enable_ble(bool en) { ENABLE_DEBUG_BLE_LOGGING = en; }
void m4g_log_enable_keypress(bool en) { ENABLE_DEBUG_KEYPRESS_LOGGING = en; }

bool m4g_log_is_led_enabled(void) { return ENABLE_DEBUG_LED_LOGGING; }
bool m4g_log_is_usb_enabled(void) { return ENABLE_DEBUG_USB_LOGGING; }
bool m4g_log_is_ble_enabled(void) { return ENABLE_DEBUG_BLE_LOGGING; }
bool m4g_log_is_keypress_enabled(void) { return ENABLE_DEBUG_KEYPRESS_LOGGING; }

void m4g_log_append_line(const char *line)
{
#if CONFIG_M4G_LOG_PERSISTENCE
  if (!s_persistence_enabled)
    return;
  ensure_log_task();
  if (!s_log_queue)
    return;
  log_msg_t msg = {.type = LOG_MSG_LINE, .len = 0};
  size_t copy_len = strnlen(line, LOG_MAX_LINE_LEN - 1);
  memcpy(msg.line, line, copy_len);
  msg.line[copy_len] = '\0';
  msg.len = (uint16_t)copy_len;
  if (xQueueSend(s_log_queue, &msg, 0) != pdTRUE)
  {
    if (!s_log_queue_overflowed)
    {
      s_log_queue_overflowed = true;
      ESP_LOGW("M4G-LOG", "log queue full; dropping messages");
    }
  }
#else
  (void)line; // persistence disabled; no-op
#endif
}

void m4g_log_flush(void)
{
#if CONFIG_M4G_LOG_PERSISTENCE
  if (!s_persistence_enabled)
    return;
  ensure_log_task();
  if (!s_log_queue)
    return;
  log_msg_t msg = {.type = LOG_MSG_FLUSH, .len = 0};
  xQueueSend(s_log_queue, &msg, portMAX_DELAY);
#endif
}

void m4g_logging_set_nvs_ready(void)
{
#if CONFIG_M4G_LOG_PERSISTENCE
  if (!s_persistence_enabled)
    return;
  s_nvs_ready = true;
  m4g_log_flush();
#endif
}

void m4g_logging_disable_persistence(void)
{
#if CONFIG_M4G_LOG_PERSISTENCE
  if (!s_persistence_enabled)
    return;
  s_persistence_enabled = false;
  s_nvs_ready = false;
  s_stage_len = 0;
  s_log_queue_overflowed = false;
  if (s_log_task)
  {
    vTaskDelete(s_log_task);
    s_log_task = NULL;
  }
  if (s_log_queue)
  {
    vQueueDelete(s_log_queue);
    s_log_queue = NULL;
  }
#endif
}

bool m4g_log_persistence_enabled(void)
{
#if CONFIG_M4G_LOG_PERSISTENCE
  return s_persistence_enabled;
#else
  return false;
#endif
}
