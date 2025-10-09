#include "m4g_usb.h"
#include "m4g_logging.h"
#include "m4g_led.h"
#ifndef CONFIG_M4G_SPLIT_ROLE_RIGHT
#include "m4g_ble.h"    // Direct BLE forwarding (legacy path - now via bridge)
#include "m4g_bridge.h" // Bridge for translation
#else
// RIGHT side stubs - these functions/constants are not available without bridge/ble
#define M4G_BRIDGE_MAX_SLOTS 2
#define M4G_INVALID_SLOT 0xFF
static inline void m4g_bridge_set_charachorder_status(bool detected, bool both_halves) { (void)detected; (void)both_halves; }
static inline void m4g_bridge_reset_slot(uint8_t slot) { (void)slot; }
static inline void m4g_bridge_process_usb_report(uint8_t slot, const uint8_t *report, size_t len, bool is_charachorder) {
    // On RIGHT side, forward reports via ESP-NOW to LEFT
    #ifdef CONFIG_M4G_SPLIT_ROLE_RIGHT
    extern esp_err_t m4g_espnow_send_hid_report(uint8_t slot, const uint8_t *report, size_t len, bool is_charachorder);
    esp_err_t ret = m4g_espnow_send_hid_report(slot, report, len, is_charachorder);
    if (ret != ESP_OK && ENABLE_DEBUG_USB_LOGGING)
    {
        ESP_LOGW("M4G-USB", "Failed to forward HID report via ESP-NOW: %s", esp_err_to_name(ret));
    }
    #else
    (void)slot; (void)report; (void)len; (void)is_charachorder;
    #endif
}
#endif
#include "usb/usb_host.h"
#include "usb/usb_types_stack.h"
#include "usb/usb_types_ch9.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include <string.h>
#include <stdio.h>
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

static const char *USB_TAG = "M4G-USB";

#define USB_HOST_PRIORITY 20
#define USB_HOST_TASK_STACK_SIZE 4096

#define USB_CHARACHORDER_HUB_VID 0x1A40
#define USB_CHARACHORDER_HUB_PID 0x0101
#define USB_DUAL_HID_PRODUCT_WILDCARD 0xFFFF

static usb_host_client_handle_t s_client = NULL;
static m4g_usb_hid_report_cb_t s_hid_cb = NULL;
static uint8_t s_active_hid_devices = 0;
static bool s_rescan_requested = false;
static bool s_seen_charachorder_hub = false;
static bool s_charachorder_mode = false;
static uint8_t s_required_hid_devices = 1;
static uint8_t s_charachorder_halves_connected = 0;
static uint8_t s_charachorder_halves_detected = 0; // Total seen (including unclaimed)
static TickType_t s_first_half_connected_time = 0;

// HID device representation (mirrors prior main.c struct)
typedef struct
{
  usb_device_handle_t dev_hdl;
  usb_host_client_handle_t client_handle;
  uint8_t dev_addr;
  uint8_t intf_num;
  uint8_t ep_addr;
  uint8_t slot;
  bool active;
  char device_name[32];
  bool transfer_started;
  bool interface_claimed;
  usb_transfer_t *transfer;
  uint16_t vid;
  uint16_t pid;
  uint8_t consecutive_errors;
  TickType_t last_error_tick;
  bool is_charachorder;
} m4g_usb_hid_device_t;

static m4g_usb_hid_device_t s_hid_devices[M4G_BRIDGE_MAX_SLOTS];
#define M4G_USB_MAX_HID_DEVICES (sizeof(s_hid_devices) / sizeof(s_hid_devices[0]))
static uint8_t s_claimed_device_count = 0;
static bool s_restart_needed = false;

static const char *transfer_status_to_str(usb_transfer_status_t status)
{
  switch (status)
  {
  case USB_TRANSFER_STATUS_COMPLETED:
    return "completed";
  case USB_TRANSFER_STATUS_ERROR:
    return "error";
  case USB_TRANSFER_STATUS_STALL:
    return "stall";
  case USB_TRANSFER_STATUS_NO_DEVICE:
    return "no_device";
  case USB_TRANSFER_STATUS_CANCELED:
    return "canceled";
  default:
    return "unknown";
  }
}

static int allocate_hid_slot(void)
{
  for (int i = 0; i < (int)M4G_USB_MAX_HID_DEVICES; ++i)
  {
    if (!s_hid_devices[i].active)
      return i;
  }
  return -1;
}

static void update_usb_led_state(void);
static void update_required_hid_devices(void);
static bool is_charachorder_device(uint16_t vid, uint16_t pid);

static void update_usb_led_state(void)
{
  m4g_led_set_usb_connected(s_active_hid_devices >= s_required_hid_devices && s_required_hid_devices > 0);
}

static bool is_charachorder_device(uint16_t vid, uint16_t pid)
{
#if CONFIG_M4G_USB_CHARACHORDER_VENDOR_ID != 0
  if (vid != CONFIG_M4G_USB_CHARACHORDER_VENDOR_ID)
    return false;
  if (CONFIG_M4G_USB_CHARACHORDER_PRODUCT_ID != USB_DUAL_HID_PRODUCT_WILDCARD &&
      pid != CONFIG_M4G_USB_CHARACHORDER_PRODUCT_ID)
    return false;
  return true;
#else
  (void)vid;
  (void)pid;
  return false;
#endif
}

static void update_required_hid_devices(void)
{
  bool detected_charachorder = s_seen_charachorder_hub && (s_charachorder_halves_connected > 0);

  bool previous_mode = s_charachorder_mode;
  s_charachorder_mode = detected_charachorder;
  // CharaChorder firmware internally combines both halves into ONE USB device
  // We only need to claim one device, not two!
  s_required_hid_devices = 1;

  if (ENABLE_DEBUG_USB_LOGGING && previous_mode != s_charachorder_mode)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG, "Dual-HID requirement %s", s_charachorder_mode ? "ENABLED (CharaChorder detected)" : "DISABLED");
  }

  update_usb_led_state();
  bool halves_ok = (s_required_hid_devices == 0) ? false : (s_charachorder_halves_connected >= s_required_hid_devices);
  m4g_bridge_set_charachorder_status(s_charachorder_mode, halves_ok);
}

// Forward declarations
static void usb_host_client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg);
static void enumerate_device(uint8_t dev_addr);
static void setup_hid_transfers(void);
static void hid_transfer_callback(usb_transfer_t *transfer);
static bool enum_filter_cb(const usb_device_desc_t *dev_desc, uint8_t *bConfigurationValue);

bool m4g_usb_is_connected(void) { return s_required_hid_devices > 0 && s_active_hid_devices >= s_required_hid_devices; }
uint8_t m4g_usb_active_hid_count(void) { return s_active_hid_devices; }
void m4g_usb_request_rescan(void) { s_rescan_requested = true; }

// Placeholder: minimal host event loop (full logic to be migrated from main.c)
// Unified USB host processing task (library + client events)
static void usb_host_unified_task(void *arg)
{
  (void)arg;
  while (1)
  {
    // Process low-level library events
    uint32_t flags;
    usb_host_lib_handle_events(pdMS_TO_TICKS(100), &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
    {
      LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, W, USB_TAG, "No USB clients registered");
    }
    if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)
    {
      if (s_restart_needed)
      {
        LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, W, USB_TAG, "Restart flag set (not implemented)");
        s_restart_needed = false;
      }
    }
    // Process client events if any
    if (s_client)
    {
      usb_host_client_handle_events(s_client, 0); // non-blocking
    }

    if (s_rescan_requested)
    {
      s_rescan_requested = false;
      setup_hid_transfers();
    }
  }
}

static bool enum_filter_cb(const usb_device_desc_t *dev_desc, uint8_t *bConfigurationValue)
{
  LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG, "Enum filter: VID=0x%04X, PID=0x%04X, Class=0x%02X", dev_desc->idVendor, dev_desc->idProduct, dev_desc->bDeviceClass);
  if (dev_desc->idVendor == USB_CHARACHORDER_HUB_VID && dev_desc->idProduct == USB_CHARACHORDER_HUB_PID)
  {
    *bConfigurationValue = 1;
    s_seen_charachorder_hub = true;
    if (ENABLE_DEBUG_USB_LOGGING)
    {
      LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG, "Detected CharaChorder hub VID=0x%04X PID=0x%04X", dev_desc->idVendor, dev_desc->idProduct);
    }
    return true;
  }
  if (dev_desc->bDeviceClass == 0x09)
  {
    *bConfigurationValue = 1;
    return true;
  }
  if (dev_desc->bDeviceClass == 0x03 || dev_desc->bDeviceClass == 0x00)
  {
    *bConfigurationValue = 1;
    return true;
  }
  *bConfigurationValue = 1;
  return true; // allow all
}

static void cleanup_all_devices(void)
{
  usb_device_handle_t closed_handles[M4G_USB_MAX_HID_DEVICES] = {0};
  uint8_t closed_count = 0;
  for (int i = 0; i < (int)M4G_USB_MAX_HID_DEVICES; ++i)
  {
    if (s_hid_devices[i].active)
    {
      if (s_hid_devices[i].slot != M4G_INVALID_SLOT)
        m4g_bridge_reset_slot(s_hid_devices[i].slot);
      if (s_hid_devices[i].transfer)
      {
        usb_host_transfer_free(s_hid_devices[i].transfer);
        s_hid_devices[i].transfer = NULL;
      }
      if (s_hid_devices[i].interface_claimed && s_hid_devices[i].dev_hdl)
      {
        usb_host_interface_release(s_client, s_hid_devices[i].dev_hdl, s_hid_devices[i].intf_num);
      }
      if (s_hid_devices[i].dev_hdl)
      {
        bool already_closed = false;
        for (uint8_t j = 0; j < closed_count; ++j)
        {
          if (closed_handles[j] == s_hid_devices[i].dev_hdl)
          {
            already_closed = true;
            break;
          }
        }
        if (!already_closed)
        {
          usb_host_device_close(s_client, s_hid_devices[i].dev_hdl);
          if (closed_count < 2)
          {
            closed_handles[closed_count++] = s_hid_devices[i].dev_hdl;
          }
        }
      }
      memset(&s_hid_devices[i], 0, sizeof(s_hid_devices[i]));
      s_hid_devices[i].slot = M4G_INVALID_SLOT;
    }
  }
  s_active_hid_devices = 0;
  s_claimed_device_count = 0;

  // NOTE: Do NOT reset s_seen_charachorder_hub here!
  // The hub detection persists across individual device disconnects.
  // The hub itself remains connected even if HID devices disconnect/reconnect.
  // Only reset hub state on complete USB subsystem restart.

  // CharaChorder firmware combines both halves internally into ONE USB device
  // We always only need 1 device, not 2
  s_charachorder_mode = s_seen_charachorder_hub;
  s_required_hid_devices = 1;

  if (s_seen_charachorder_hub)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG, "CharaChorder hub still present, single device will handle both halves");
  }

  s_charachorder_halves_connected = 0;
  update_usb_led_state();

  // Only disable CharaChorder detection if the hub is gone
  // If hub is still present, devices will reconnect and detection will be re-enabled
  if (!s_seen_charachorder_hub)
  {
    m4g_bridge_set_charachorder_status(false, false);
  }
}

static void usb_host_client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
  (void)arg;
  switch (event_msg->event)
  {
  case USB_HOST_CLIENT_EVENT_NEW_DEV:
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG, "New device addr=%d", event_msg->new_dev.address);
    vTaskDelay(pdMS_TO_TICKS(100));
    enumerate_device(event_msg->new_dev.address);
    setup_hid_transfers();

    // Check for missing second CharaChorder half after enumeration completes
    if (s_charachorder_mode && s_charachorder_halves_connected > 0)
    {
      // Give the second half 5 seconds to appear after the first half connected
      if (s_charachorder_halves_detected == 1)
      {
        TickType_t elapsed_ticks = xTaskGetTickCount() - s_first_half_connected_time;
        uint32_t elapsed_ms = (elapsed_ticks * 1000) / configTICK_RATE_HZ;

        if (elapsed_ms > 5000)
        {
          LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, W, USB_TAG,
                       "WARNING: Only one CharaChorder half detected after %lu ms. Expected both halves. "
                       "This may cause typing issues on the right-hand side.",
                       elapsed_ms);
        }
      }
    }
    break;
  case USB_HOST_CLIENT_EVENT_DEV_GONE:
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG, "Device gone - resetting state");
    cleanup_all_devices();
    usb_host_device_free_all();
    s_restart_needed = true;
    break;
  default:
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, W, USB_TAG, "Unknown USB client event %d", event_msg->event);
    break;
  }
}

static void enumerate_device(uint8_t dev_addr)
{
  LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG, "Enumerating addr %d (claimed=%d)", dev_addr, s_claimed_device_count);
  vTaskDelay(pdMS_TO_TICKS(50));
  usb_device_handle_t dev_hdl;
  esp_err_t err = usb_host_device_open(s_client, dev_addr, &dev_hdl);
  if (err != ESP_OK)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, E, USB_TAG, "open fail: %s", esp_err_to_name(err));
    return;
  }
  const usb_device_desc_t *dev_desc;
  if (usb_host_get_device_descriptor(dev_hdl, &dev_desc) != ESP_OK)
  {
    usb_host_device_close(s_client, dev_hdl);
    return;
  }

  // Check if this is a CharaChorder device
  bool is_chara_dev = (dev_desc->idVendor == CONFIG_M4G_USB_CHARACHORDER_VENDOR_ID &&
                       dev_desc->idProduct == CONFIG_M4G_USB_CHARACHORDER_PRODUCT_ID);

  // If we already have a CharaChorder device claimed, this is the second half
  if (s_charachorder_mode && s_charachorder_halves_connected > 0 && is_chara_dev)
  {
    ++s_charachorder_halves_detected;
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG,
                 "Second CharaChorder half detected at addr %d (both halves now present - total detected: %d)",
                 dev_addr, s_charachorder_halves_detected);
    usb_host_device_close(s_client, dev_hdl);

    // Verify both halves are present
    if (s_charachorder_halves_detected >= 2)
    {
      LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG, "Both CharaChorder halves successfully connected");
    }
    return;
  }

  if (dev_desc->bDeviceClass == 0x09)
  {
    usb_host_device_close(s_client, dev_hdl);
    return;
  }
  const usb_config_desc_t *cfg;
  if (usb_host_get_active_config_descriptor(dev_hdl, &cfg) != ESP_OK)
  {
    usb_host_device_close(s_client, dev_hdl);
    return;
  }
  const usb_intf_desc_t *intf_desc;
  int intf_offset = 0;
  uint8_t hid_claims_on_device = 0;

  // Determine if this is a CharaChorder device
  bool is_chara = s_seen_charachorder_hub && is_charachorder_device(dev_desc->idVendor, dev_desc->idProduct);

  // Channel budget strategy:
  // - First CharaChorder half (left): claim keyboard + mouse (2 interfaces)
  // - Second CharaChorder half (right): claim keyboard only (1 interface)
  // This keeps us within ESP32-S3's ~8 channel limit
  uint8_t max_interfaces_for_this_device = 1;
  if (is_chara && s_charachorder_halves_connected == 0)
  {
    // First CharaChorder half - allow claiming up to 2 interfaces
    max_interfaces_for_this_device = 2;
    if (ENABLE_DEBUG_USB_LOGGING)
    {
      LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG,
                   "First CharaChorder half detected - will claim up to 2 interfaces (keyboard + mouse)");
    }
  }
  else if (is_chara)
  {
    if (ENABLE_DEBUG_USB_LOGGING)
    {
      LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG,
                   "Second CharaChorder half detected - will claim only 1 interface (keyboard)");
    }
  }

  for (int i = 0; i < cfg->bNumInterfaces && s_claimed_device_count < M4G_USB_MAX_HID_DEVICES; ++i)
  {
    intf_desc = usb_parse_interface_descriptor(cfg, i, 0, &intf_offset);
    if (!intf_desc)
      continue;

    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG,
                 "Interface %d: Class=0x%02X, Number=%d, HID claims so far=%d, max allowed=%d",
                 i, intf_desc->bInterfaceClass, intf_desc->bInterfaceNumber,
                 hid_claims_on_device, max_interfaces_for_this_device);

    if (intf_desc->bInterfaceClass == USB_CLASS_HID && s_claimed_device_count < M4G_USB_MAX_HID_DEVICES)
    {
      // Check if we've reached the interface limit for this device
      if (hid_claims_on_device >= max_interfaces_for_this_device)
      {
        LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, W, USB_TAG, "Skipping interface %d - reached max claims (%d)",
                     intf_desc->bInterfaceNumber, max_interfaces_for_this_device);
        continue;
      }

      if (usb_host_interface_claim(s_client, dev_hdl, intf_desc->bInterfaceNumber, 0) == ESP_OK)
      {
        LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG,
                     "Claimed HID interface %d - scanning %d endpoints",
                     intf_desc->bInterfaceNumber, intf_desc->bNumEndpoints);

        int ep_offset = intf_offset;
        const usb_ep_desc_t *ep_desc = NULL;
        for (int ep = 0; ep < intf_desc->bNumEndpoints; ++ep)
        {
          ep_desc = usb_parse_endpoint_descriptor_by_index(intf_desc, ep, cfg->wTotalLength, &ep_offset);
          if (ep_desc)
          {
            LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG,
                         "  Endpoint %d: addr=0x%02X, attr=0x%02X, type=%s, dir=%s",
                         ep, ep_desc->bEndpointAddress, ep_desc->bmAttributes,
                         (ep_desc->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_INT ? "INT" : "OTHER",
                         (ep_desc->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) ? "IN" : "OUT");
          }
          if (ep_desc && (ep_desc->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_INT && (ep_desc->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK))
            break;
        }
        int slot = allocate_hid_slot();
        if (slot < 0)
        {
          LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, W, USB_TAG, "No free HID slots for interface %d", intf_desc->bInterfaceNumber);
          usb_host_interface_release(s_client, dev_hdl, intf_desc->bInterfaceNumber);
          continue;
        }
        m4g_usb_hid_device_t *dev = &s_hid_devices[slot];
        memset(dev, 0, sizeof(*dev));
        dev->slot = (uint8_t)slot;
        dev->dev_hdl = dev_hdl;
        dev->client_handle = s_client;
        dev->dev_addr = dev_addr;
        dev->intf_num = intf_desc->bInterfaceNumber;
        dev->ep_addr = ep_desc ? ep_desc->bEndpointAddress : 0;
        dev->transfer = NULL;
        dev->active = true;
        dev->interface_claimed = true;
        dev->vid = dev_desc->idVendor;
        dev->pid = dev_desc->idProduct;
        dev->is_charachorder = s_seen_charachorder_hub && is_charachorder_device(dev->vid, dev->pid);

        if (ENABLE_DEBUG_USB_LOGGING)
        {
          LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG,
                       "Device check: VID=0x%04X PID=0x%04X hub_seen=%d is_chara_dev=%d => is_charachorder=%d",
                       dev->vid, dev->pid, s_seen_charachorder_hub,
                       is_charachorder_device(dev->vid, dev->pid), dev->is_charachorder);
        }

        if (dev->is_charachorder)
        {
          ++s_charachorder_halves_connected;
          ++s_charachorder_halves_detected;

          // Set timestamp when first half connects
          if (s_charachorder_halves_connected == 1)
          {
            s_first_half_connected_time = xTaskGetTickCount();
            LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG, "First CharaChorder half connected, waiting for second half...");
          }

          snprintf(dev->device_name, sizeof(dev->device_name), "CharaChorder_%u", (unsigned)s_charachorder_halves_connected);
        }
        else
        {
          snprintf(dev->device_name, sizeof(dev->device_name), "HID_%d_IF%d", dev_addr, intf_desc->bInterfaceNumber);
        }
        ++s_active_hid_devices;
        ++s_claimed_device_count;
        ++hid_claims_on_device;
        m4g_bridge_reset_slot(dev->slot);
        LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG, "Stored HID slot=%d addr=%d VID=0x%04X PID=0x%04X ep=0x%02X intf=%d active=%d claims_on_dev=%d",
                     slot, dev_addr, dev->vid, dev->pid, dev->ep_addr, intf_desc->bInterfaceNumber, s_active_hid_devices, hid_claims_on_device);
        if (!dev->ep_addr)
        {
          usb_host_interface_release(s_client, dev_hdl, dev->intf_num);
          dev->interface_claimed = false;
          dev->active = false;
          dev->vid = 0;
          dev->pid = 0;
          if (dev->slot != M4G_INVALID_SLOT)
            m4g_bridge_reset_slot(dev->slot);
          dev->slot = M4G_INVALID_SLOT;
          --s_active_hid_devices;
          --s_claimed_device_count;
          if (dev->is_charachorder && s_charachorder_halves_connected > 0)
            --s_charachorder_halves_connected;
          if (hid_claims_on_device > 0)
            --hid_claims_on_device;
          update_required_hid_devices();
        }
        else
        {
          update_required_hid_devices();
        }
      }
    }
  }
  if (hid_claims_on_device == 0)
  {
    usb_host_device_close(s_client, dev_hdl);
  }
}

// (Key chord & arrow translation moved to m4g_bridge)

// (Forwarding handled by bridge now; callback remains for optional diagnostics)

static void hid_transfer_callback(usb_transfer_t *transfer)
{
  m4g_usb_hid_device_t *dev = (m4g_usb_hid_device_t *)transfer->context;
  
  // Debug: Log every callback to see if CharaChorder sends anything
  static uint32_t callback_count = 0;
  if (++callback_count % 100 == 1) // Log every 100th callback to avoid spam
  {
    ESP_LOGI(USB_TAG, "Transfer callback #%lu (status=%d, actual_bytes=%d)", 
             callback_count, transfer->status, transfer->actual_num_bytes);
  }
  
  if (!dev)
    return;

  bool should_resubmit = true;

  // For successful transfers, copy data immediately and resubmit ASAP
  // This minimizes latency for rapid CharaChorder chord reports
  uint8_t report_buffer[64];
  size_t report_len = 0;
  bool process_report = false;
  bool is_malformed = false;

  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED)
  {
    // Copy data immediately so we can resubmit the transfer quickly
    report_len = transfer->actual_num_bytes;
    if (report_len > 0 && report_len <= sizeof(report_buffer))
    {
      memcpy(report_buffer, transfer->data_buffer, report_len);
      process_report = true;

      // Check for malformed CharaChorder reports
      if (dev->is_charachorder && report_len > 15 &&
          report_buffer[0] == 0x01 && report_buffer[4] == 0x01)
      {
        is_malformed = true;
        process_report = false; // Don't process malformed reports
      }
    }

    dev->consecutive_errors = 0;
  }
  else
  {
    const char *status_name = transfer_status_to_str(transfer->status);
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, W, USB_TAG, "Transfer issue dev=%s slot=%d status=%s(%d)", dev->device_name, dev->slot, status_name, transfer->status);

    TickType_t now = xTaskGetTickCount();
    if (now - dev->last_error_tick > pdMS_TO_TICKS(250))
    {
      dev->consecutive_errors = 0;
    }
    dev->last_error_tick = now;
    dev->consecutive_errors++;

    bool request_rescan = false;
    switch (transfer->status)
    {
    case USB_TRANSFER_STATUS_NO_DEVICE:
    case USB_TRANSFER_STATUS_CANCELED:
      request_rescan = true;
      break;
    case USB_TRANSFER_STATUS_ERROR:
    case USB_TRANSFER_STATUS_STALL:
    default:
      // Increase threshold to 5 errors for CharaChorder chord tolerance
      // Transient errors during rapid chord input shouldn't trigger reset
      if (dev->consecutive_errors >= 5)
      {
        request_rescan = true;
      }
      break;
    }

    if (request_rescan)
    {
      if (dev->slot != M4G_INVALID_SLOT)
      {
        m4g_bridge_reset_slot(dev->slot);
      }
      usb_host_transfer_free(transfer);
      dev->transfer_started = false;
      dev->transfer = NULL;
      dev->consecutive_errors = 0;
      s_rescan_requested = true;
      should_resubmit = false;
    }
  }
  if (!should_resubmit)
    return;

  transfer->num_bytes = transfer->num_bytes ? transfer->num_bytes : 64;

  // Retry transfer submission with progressive backoff for ESP_ERR_INVALID_STATE
  // This handles CharaChorder sending multiple reports in rapid succession during chord output
  esp_err_t err = ESP_FAIL;
  const int max_retries = 10; // Try up to 10 times
  const int retry_delays_ms[] = {0, 1, 2, 5, 10, 20, 50, 100, 150, 200};

  for (int retry = 0; retry < max_retries; retry++)
  {
    if (retry > 0)
    {
      // Wait before retry (progressive backoff)
      vTaskDelay(pdMS_TO_TICKS(retry_delays_ms[retry]));
    }

    err = usb_host_transfer_submit(transfer);

    if (err == ESP_OK)
    {
      // Success!
      if (retry > 0 && ENABLE_DEBUG_USB_LOGGING)
      {
        LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG,
                     "Transfer resubmit succeeded after %d retries (total delay ~%dms)",
                     retry, retry_delays_ms[retry]);
      }
      break;
    }
    else if (err == ESP_ERR_INVALID_STATE)
    {
      // Device busy - this is normal during CharaChorder chord output
      // Keep retrying with backoff
      if (retry == max_retries - 1)
      {
        // Final retry failed
        LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, W, USB_TAG,
                     "Transfer resubmit failed after %d retries - device may be resetting",
                     max_retries);
      }
      continue;
    }
    else
    {
      // Other error - give up immediately
      LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, E, USB_TAG,
                   "Transfer resubmit failed with error %s (retry %d/%d)",
                   esp_err_to_name(err), retry, max_retries);
      break;
    }
  }

  if (err != ESP_OK)
  {
    // All retries failed - clean up
    usb_host_transfer_free(transfer);
    dev->transfer_started = false;
    dev->transfer = NULL;
    s_rescan_requested = true;

    // Don't increment error counter for ESP_ERR_INVALID_STATE (transient)
    if (err != ESP_ERR_INVALID_STATE)
    {
      dev->consecutive_errors++;
    }
  }

  // NOW process the report after transfer is resubmitted
  // This ensures we're ready to receive the next report ASAP
  if (process_report)
  {
    if (is_malformed)
    {
      LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, W, USB_TAG,
                   "Ignoring malformed CharaChorder chord report (%zu bytes with ErrorRollOver)",
                   report_len);
      ESP_LOG_BUFFER_HEX_LEVEL(USB_TAG, report_buffer, report_len, ESP_LOG_WARN);
    }
    else
    {
      // Always log HID reports for debugging RIGHT side
      LOG_AND_SAVE(true, I, USB_TAG,
                   "HID report dev=%s slot=%d %zu bytes",
                   dev->device_name, dev->slot, report_len);
      if (ENABLE_DEBUG_KEYPRESS_LOGGING)
      {
        ESP_LOG_BUFFER_HEX_LEVEL(USB_TAG, report_buffer, report_len, ESP_LOG_INFO);
      }

      if (dev->slot != M4G_INVALID_SLOT)
      {
        m4g_bridge_process_usb_report(dev->slot, report_buffer, report_len, dev->is_charachorder);
      }
      else if (ENABLE_DEBUG_USB_LOGGING)
      {
        LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, W, USB_TAG,
                     "Dropping HID report with invalid slot (dev=%s)", dev->device_name);
      }
    }
  }
}

static void setup_hid_transfers(void)
{
  LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG, "Setting up transfers for %d devices", s_active_hid_devices);
  for (int i = 0; i < (int)M4G_USB_MAX_HID_DEVICES; ++i)
  {
    m4g_usb_hid_device_t *dev = &s_hid_devices[i];
    if (!dev->active || dev->ep_addr == 0 || dev->transfer_started)
      continue;
    usb_transfer_t *t;
    if (usb_host_transfer_alloc(64, 0, &t) != ESP_OK)
      continue;
    t->device_handle = dev->dev_hdl;
    t->bEndpointAddress = dev->ep_addr;
    t->callback = hid_transfer_callback;
    t->context = dev;
    t->num_bytes = 64;

    esp_err_t err = usb_host_transfer_submit(t);
    if (err != ESP_OK)
    {
      if (ENABLE_DEBUG_USB_LOGGING)
      {
        LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, W, USB_TAG,
                     "Transfer submit failed for dev=%s: %s (%d)",
                     dev->device_name, esp_err_to_name(err), err);
      }
      usb_host_transfer_free(t);
      dev->transfer_started = false;
      dev->transfer = NULL;
    }
    else
    {
      dev->transfer_started = true;
      dev->transfer = t;
    }
  }
  update_required_hid_devices();
  update_usb_led_state();
  if (s_active_hid_devices >= s_required_hid_devices)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG, "USB HID ready (active=%d required=%d)", s_active_hid_devices, s_required_hid_devices);
  }
  else
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG, "Waiting for additional HID devices (active=%d required=%d)", s_active_hid_devices, s_required_hid_devices);
  }
}

// (Removed separate event_task; merged into usb_host_unified_task)

esp_err_t m4g_usb_init(const m4g_usb_config_t *cfg, m4g_usb_hid_report_cb_t cb)
{
  (void)cfg;
  s_hid_cb = cb;

  for (int i = 0; i < (int)M4G_USB_MAX_HID_DEVICES; ++i)
  {
    s_hid_devices[i].slot = M4G_INVALID_SLOT;
  }

#if CONFIG_M4G_VBUS_ENABLE_GPIO >= 0
  // Enable VBUS switch if configured
  gpio_config_t vbus_cfg = {
      .pin_bit_mask = (1ULL << CONFIG_M4G_VBUS_ENABLE_GPIO),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE};
  gpio_config(&vbus_cfg);
  gpio_set_level(CONFIG_M4G_VBUS_ENABLE_GPIO, 1);
  vTaskDelay(pdMS_TO_TICKS(20));
  LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG, "VBUS enabled via GPIO %d", CONFIG_M4G_VBUS_ENABLE_GPIO);
#endif

  usb_host_config_t host_cfg = {
      .skip_phy_setup = false,
      .intr_flags = 0,
      .enum_filter_cb = enum_filter_cb,
  };
  esp_err_t err = usb_host_install(&host_cfg);
  if (err != ESP_OK)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, E, USB_TAG, "usb_host_install failed: %s", esp_err_to_name(err));
    return err;
  }
  LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG, "USB Host library installed (skeleton)");

  usb_host_client_config_t client_cfg = {
      .is_synchronous = false,
      .max_num_event_msg = 16,
      .async = {.client_event_callback = usb_host_client_event_cb, .callback_arg = NULL},
  };
  err = usb_host_client_register(&client_cfg, &s_client);
  if (err != ESP_OK)
  {
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, E, USB_TAG, "usb_host_client_register failed: %s", esp_err_to_name(err));
    return err;
  }
  LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG, "USB client registered (skeleton)");

  xTaskCreate(usb_host_unified_task, "m4g_usb", USB_HOST_TASK_STACK_SIZE, NULL, USB_HOST_PRIORITY, NULL);
  return ESP_OK;
}

void m4g_usb_task(void *param) { usb_host_unified_task(param); }
