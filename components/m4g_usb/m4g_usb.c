#include "m4g_usb.h"
#include "m4g_logging.h"
#include "m4g_led.h"
#include "m4g_ble.h"    // Direct BLE forwarding (legacy path - now via bridge)
#include "m4g_bridge.h" // Bridge for translation
#include "usb/usb_host.h"
#include "usb/usb_types_stack.h"
#include "usb/usb_types_ch9.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
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

static usb_host_client_handle_t s_client = NULL;
static m4g_usb_hid_report_cb_t s_hid_cb = NULL;
static uint8_t s_active_hid_devices = 0;
static bool s_rescan_requested = false;

// HID device representation (mirrors prior main.c struct)
typedef struct
{
  usb_device_handle_t dev_hdl;
  usb_host_client_handle_t client_handle;
  uint8_t dev_addr;
  uint8_t intf_num;
  uint8_t ep_addr;
  bool active;
  char device_name[32];
  bool transfer_started;
  bool interface_claimed;
  usb_transfer_t *transfer;
} m4g_usb_hid_device_t;

static m4g_usb_hid_device_t s_hid_devices[2];
static uint8_t s_claimed_device_count = 0;
static bool s_restart_needed = false;

// Forward declarations
static void usb_host_client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg);
static void enumerate_device(uint8_t dev_addr);
static void setup_hid_transfers(void);
static void hid_transfer_callback(usb_transfer_t *transfer);
static bool enum_filter_cb(const usb_device_desc_t *dev_desc, uint8_t *bConfigurationValue);

bool m4g_usb_is_connected(void) { return s_active_hid_devices > 0; }
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
  }
}

static bool enum_filter_cb(const usb_device_desc_t *dev_desc, uint8_t *bConfigurationValue)
{
  LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG, "Enum filter: VID=0x%04X, PID=0x%04X, Class=0x%02X", dev_desc->idVendor, dev_desc->idProduct, dev_desc->bDeviceClass);
  if (dev_desc->idVendor == 0x1A40 && dev_desc->idProduct == 0x0101)
  {
    *bConfigurationValue = 1;
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
  for (int i = 0; i < 2; ++i)
  {
    if (s_hid_devices[i].active)
    {
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
        usb_host_device_close(s_client, s_hid_devices[i].dev_hdl);
      }
      memset(&s_hid_devices[i], 0, sizeof(s_hid_devices[i]));
    }
  }
  s_active_hid_devices = 0;
  s_claimed_device_count = 0;
  m4g_led_set_usb_connected(false);
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
  bool hid_claimed = false;
  for (int i = 0; i < cfg->bNumInterfaces && !hid_claimed; ++i)
  {
    intf_desc = usb_parse_interface_descriptor(cfg, i, 0, &intf_offset);
    if (!intf_desc)
      continue;
    if (intf_desc->bInterfaceClass == USB_CLASS_HID && s_active_hid_devices < 2)
    {
      if (usb_host_interface_claim(s_client, dev_hdl, intf_desc->bInterfaceNumber, 0) == ESP_OK)
      {
        const usb_ep_desc_t *ep_desc = NULL;
        for (int ep = 0; ep < intf_desc->bNumEndpoints; ++ep)
        {
          ep_desc = usb_parse_endpoint_descriptor_by_index(intf_desc, ep, cfg->wTotalLength, &intf_offset);
          if (ep_desc && (ep_desc->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_INT && (ep_desc->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK))
            break;
        }
        m4g_usb_hid_device_t *dev = &s_hid_devices[s_active_hid_devices];
        dev->dev_hdl = dev_hdl;
        dev->client_handle = s_client;
        dev->dev_addr = dev_addr;
        dev->intf_num = intf_desc->bInterfaceNumber;
        dev->ep_addr = ep_desc ? ep_desc->bEndpointAddress : 0;
        dev->active = true;
        dev->interface_claimed = true;
        dev->transfer = NULL;
        snprintf(dev->device_name, sizeof(dev->device_name), "HID_%d", dev_addr);
        ++s_active_hid_devices;
        ++s_claimed_device_count;
        hid_claimed = true;
        LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG, "Stored HID dev addr=%d ep=0x%02X active=%d", dev_addr, dev->ep_addr, s_active_hid_devices);
        if (!dev->ep_addr)
        {
          usb_host_interface_release(s_client, dev_hdl, dev->intf_num);
          dev->interface_claimed = false;
          dev->active = false;
          --s_active_hid_devices;
          --s_claimed_device_count;
          hid_claimed = false;
        }
      }
    }
  }
  if (!hid_claimed)
  {
    usb_host_device_close(s_client, dev_hdl);
  }
}

// (Key chord & arrow translation moved to m4g_bridge)

// (Forwarding handled by bridge now; callback remains for optional diagnostics)

static void hid_transfer_callback(usb_transfer_t *transfer)
{
  m4g_usb_hid_device_t *dev = (m4g_usb_hid_device_t *)transfer->context;
  if (transfer->status == USB_TRANSFER_STATUS_COMPLETED)
  {
    if (ENABLE_DEBUG_KEYPRESS_LOGGING)
    {
      LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, I, USB_TAG, "HID report dev=%s %d bytes", dev->device_name, transfer->actual_num_bytes);
      ESP_LOG_BUFFER_HEX_LEVEL(USB_TAG, transfer->data_buffer, transfer->actual_num_bytes, ESP_LOG_INFO);
    }
    // Removed unused kb_report buffer (was unused after delegation to bridge)
    if (transfer->actual_num_bytes > 0)
    {
      // Delegate raw report to bridge (which will emit BLE reports as needed)
      m4g_bridge_process_usb_report(transfer->data_buffer, transfer->actual_num_bytes);
    }
  }
  else
  {
    LOG_AND_SAVE(ENABLE_DEBUG_KEYPRESS_LOGGING, W, USB_TAG, "Transfer error status=%d", transfer->status);
  }
  vTaskDelay(pdMS_TO_TICKS(1));
  if (usb_host_transfer_submit(transfer) != ESP_OK)
  {
    usb_host_transfer_free(transfer);
    dev->transfer_started = false;
    dev->transfer = NULL;
  }
}

static void setup_hid_transfers(void)
{
  LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG, "Setting up transfers for %d devices", s_active_hid_devices);
  for (int i = 0; i < s_active_hid_devices; i++)
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
    if (usb_host_transfer_submit(t) != ESP_OK)
    {
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
  if (s_active_hid_devices > 0)
  {
    m4g_led_set_usb_connected(true);
    LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG, "USB fully connected");
  }
}

// (Removed separate event_task; merged into usb_host_unified_task)

esp_err_t m4g_usb_init(const m4g_usb_config_t *cfg, m4g_usb_hid_report_cb_t cb)
{
  (void)cfg;
  s_hid_cb = cb;

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
    LOG_AND_SAVE(true, E, USB_TAG, "usb_host_install failed: %s", esp_err_to_name(err));
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
    LOG_AND_SAVE(true, E, USB_TAG, "usb_host_client_register failed: %s", esp_err_to_name(err));
    return err;
  }
  LOG_AND_SAVE(ENABLE_DEBUG_USB_LOGGING, I, USB_TAG, "USB client registered (skeleton)");

  xTaskCreate(usb_host_unified_task, "m4g_usb", USB_HOST_TASK_STACK_SIZE, NULL, USB_HOST_PRIORITY, NULL);
  return ESP_OK;
}

void m4g_usb_task(void *param) { usb_host_unified_task(param); }
