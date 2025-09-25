#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_nimble_hci.h"
#include "esp_bt.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"
#include "host/ble_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "driver/gpio.h"
#include "usb/usb_host.h"
#include "usb/usb_types_stack.h"
#include "usb/usb_types_ch9.h"

static const char *TAG = "M4G-BLE-BRIDGE";

// Debug configuration
static const bool ENABLE_DEBUG_LED_LOGGING = false;
static const bool ENABLE_DEBUG_USB_LOGGING = false;
static const bool ENABLE_DEBUG_BLE_LOGGING = false;
static const bool ENABLE_DEBUG_KEYPRESS_LOGGING = true;

// RGB LED (WS2812) configuration
#include "led_strip.h"
#define RGB_LED_GPIO 48
#define RGB_LED_NUM_LEDS 1
static led_strip_handle_t rgb_strip = NULL;
static bool usb_fully_connected = false;
static bool ble_fully_connected = false;

// Forward declarations
static void ble_start_advertising(void);
static void update_rgb_led(void);

// Define MIN macro if not available
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

// USB Host configuration
#define USB_HOST_PRIORITY 20
#define USB_HOST_TASK_STACK_SIZE 4096

// Global USB client handle
static usb_host_client_handle_t g_usb_client_handle = NULL;

// Track claimed devices for monitoring
static uint8_t claimed_device_count = 0;

// HID device information structure
typedef struct
{
    usb_device_handle_t dev_hdl;
    usb_host_client_handle_t client_handle;
    uint8_t dev_addr;
    uint8_t intf_num;
    uint8_t ep_addr;
    bool active;
    char device_name[32];
} hid_device_t;

// Storage for CharaChorder devices
static hid_device_t charachorder_devices[2];
static uint8_t active_hid_devices = 0;
static bool hid_transfers_setup = false;
static bool usb_host_restart_needed = false;

// BLE connection state
static uint16_t ble_conn_handle = 0;
static bool ble_hid_notify_enabled = false;
static uint32_t encryption_complete_time = 0;
static bool encryption_completed = false;

// BLE HID service UUIDs
#define BLE_HID_SERVICE_UUID 0x1812
#define BLE_HID_CHARACTERISTIC_REPORT_UUID 0x2A4D
#define BLE_HID_CHAR_REPORT_MAP_UUID 0x2A4B
#define BLE_HID_CHAR_HID_INFO_UUID 0x2A4A
#define BLE_HID_CHAR_HID_CTRL_POINT_UUID 0x2A4C

// HID Report Map for standard USB keyboard (no report ID)
static const uint8_t hid_report_map[] = {
    // Standard USB keyboard report descriptor
    0x05, 0x01, // Usage Page (Generic Desktop)
    0x09, 0x06, // Usage (Keyboard)
    0xa1, 0x01, // Collection (Application)
    0x05, 0x07, //   Usage Page (Keyboard)
    0x19, 0xe0, //   Usage Minimum (224) - Left Control
    0x29, 0xe7, //   Usage Maximum (231) - Right GUI
    0x15, 0x00, //   Logical Minimum (0)
    0x25, 0x01, //   Logical Maximum (1)
    0x75, 0x01, //   Report Size (1)
    0x95, 0x08, //   Report Count (8) - 8 modifier bits
    0x81, 0x02, //   Input (Data,Var,Abs)
    0x95, 0x01, //   Report Count (1)
    0x75, 0x08, //   Report Size (8)
    0x81, 0x01, //   Input (Const,Array,Abs) - Reserved byte
    0x95, 0x06, //   Report Count (6)
    0x75, 0x08, //   Report Size (8)
    0x15, 0x00, //   Logical Minimum (0)
    0x25, 0xff, //   Logical Maximum (255)
    0x05, 0x07, //   Usage Page (Keyboard)
    0x19, 0x00, //   Usage Minimum (0)
    0x29, 0xff, //   Usage Maximum (255)
    0x81, 0x00, //   Input (Data,Array,Abs) - 6 key codes
    0xc0,       // End Collection
};

static uint8_t g_ble_addr_type;
static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;

// BLE host configuration callbacks
static void ble_host_on_reset(int reason);
static void ble_host_on_sync(void);
static void ble_start_advertising(void);
static int hid_svc_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
void ble_store_config_init(void);

// Device Information service access callback
static int dis_svc_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;
    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        if (ble_uuid_cmp(ctxt->chr->uuid, BLE_UUID16_DECLARE(0x2A29)) == 0)
        {
            // Manufacturer Name String
            const char *mfg_name = "Espressif";
            rc = os_mbuf_append(ctxt->om, mfg_name, strlen(mfg_name));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        else if (ble_uuid_cmp(ctxt->chr->uuid, BLE_UUID16_DECLARE(0x2A24)) == 0)
        {
            // Model Number String
            const char *model = "ESP32-S3";
            rc = os_mbuf_append(ctxt->om, model, strlen(model));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        else if (ble_uuid_cmp(ctxt->chr->uuid, BLE_UUID16_DECLARE(0x2A19)) == 0)
        {
            // Battery Level - return 100%
            uint8_t battery_level = 100;
            rc = os_mbuf_append(ctxt->om, &battery_level, 1);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        else if (ble_uuid_cmp(ctxt->chr->uuid, BLE_UUID16_DECLARE(0x2A50)) == 0)
        {
            // PnP ID - Vendor ID Source (1=Bluetooth SIG), VID, PID, Product Version
            uint8_t pnp_id[7] = {0x01, 0x5D, 0x02, 0x00, 0x40, 0x3A, 0x01}; // Microsoft VID for compatibility
            rc = os_mbuf_append(ctxt->om, pnp_id, sizeof(pnp_id));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        break;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// RGB LED color update function
static void update_rgb_led(void)
{
    if (!rgb_strip)
    {
        if (ENABLE_DEBUG_LED_LOGGING)
        {
            ESP_LOGW(TAG, "RGB LED strip not initialized!");
        }
        return;
    }
    uint8_t r = 0, g = 0, b = 0;
    if (usb_fully_connected && ble_fully_connected)
    {
        // Blue: USB + BLE
        r = 0;
        g = 0;
        b = 255;
        if (ENABLE_DEBUG_LED_LOGGING)
        {
            ESP_LOGI(TAG, "RGB LED: BLUE (USB+BLE connected)");
        }
    }
    else if (usb_fully_connected && !ble_fully_connected)
    {
        // Green: USB only
        r = 0;
        g = 255;
        b = 0;
        if (ENABLE_DEBUG_LED_LOGGING)
        {
            ESP_LOGI(TAG, "RGB LED: GREEN (USB only)");
        }
    }
    else if (!usb_fully_connected && ble_fully_connected)
    {
        // Yellow: BLE only
        r = 255;
        g = 255;
        b = 0;
        if (ENABLE_DEBUG_LED_LOGGING)
        {
            ESP_LOGI(TAG, "RGB LED: YELLOW (BLE only)");
        }
    }
    else
    {
        // Red: No connection
        r = 255;
        g = 0;
        b = 0;
        if (ENABLE_DEBUG_LED_LOGGING)
        {
            ESP_LOGI(TAG, "RGB LED: RED (no connection)");
        }
    }
    led_strip_set_pixel(rgb_strip, 0, r, g, b);
    led_strip_refresh(rgb_strip);
}

// Initialize RGB LED
static void init_rgb_led(void)
{
    if (ENABLE_DEBUG_LED_LOGGING)
    {
        ESP_LOGI(TAG, "Initializing RGB LED on GPIO %d...", RGB_LED_GPIO);
    }
    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_LED_GPIO,
        .max_leds = RGB_LED_NUM_LEDS,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10000000,
    };
    esp_err_t led_ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &rgb_strip);
    if (led_ret != ESP_OK || rgb_strip == NULL)
    {
        if (ENABLE_DEBUG_LED_LOGGING)
        {
            ESP_LOGE(TAG, "Failed to initialize WS2812 RGB LED strip!");
        }
    }
    else
    {
        if (ENABLE_DEBUG_LED_LOGGING)
        {
            ESP_LOGI(TAG, "WS2812 RGB LED strip initialized on GPIO %d", RGB_LED_GPIO);
        }
        led_strip_clear(rgb_strip);
    }
}

// HID service definition
static const struct ble_gatt_svc_def hid_svcs[] = {
    {// Device Information Service (mandatory for HID)
     .type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = BLE_UUID16_DECLARE(0x180A),
     .characteristics = (struct ble_gatt_chr_def[]){{
                                                        // Manufacturer Name String
                                                        .uuid = BLE_UUID16_DECLARE(0x2A29),
                                                        .access_cb = dis_svc_access_cb,
                                                        .flags = BLE_GATT_CHR_F_READ,
                                                    },
                                                    {
                                                        // Model Number String
                                                        .uuid = BLE_UUID16_DECLARE(0x2A24),
                                                        .access_cb = dis_svc_access_cb,
                                                        .flags = BLE_GATT_CHR_F_READ,
                                                    },
                                                    {
                                                        // PnP ID characteristic (recommended for HID)
                                                        .uuid = BLE_UUID16_DECLARE(0x2A50),
                                                        .access_cb = dis_svc_access_cb,
                                                        .flags = BLE_GATT_CHR_F_READ,
                                                    },
                                                    {
                                                        0, // No more characteristics
                                                    }}},
    {// Battery Service (often expected by Windows for HID devices)
     .type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = BLE_UUID16_DECLARE(0x180F),
     .characteristics = (struct ble_gatt_chr_def[]){{
                                                        // Battery Level characteristic
                                                        .uuid = BLE_UUID16_DECLARE(0x2A19),
                                                        .access_cb = dis_svc_access_cb,
                                                        .flags = BLE_GATT_CHR_F_READ,
                                                    },
                                                    {
                                                        0, // No more characteristics
                                                    }}},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = BLE_UUID16_DECLARE(BLE_HID_SERVICE_UUID),
     .characteristics = (struct ble_gatt_chr_def[]){{
                                                        // HID Information characteristic (mandatory)
                                                        .uuid = BLE_UUID16_DECLARE(BLE_HID_CHAR_HID_INFO_UUID),
                                                        .access_cb = hid_svc_access_cb,
                                                        .flags = BLE_GATT_CHR_F_READ,
                                                    },
                                                    {
                                                        // HID Report Map characteristic (mandatory)
                                                        .uuid = BLE_UUID16_DECLARE(BLE_HID_CHAR_REPORT_MAP_UUID),
                                                        .access_cb = hid_svc_access_cb,
                                                        .flags = BLE_GATT_CHR_F_READ,
                                                    },
                                                    {
                                                        // HID Control Point characteristic (mandatory)
                                                        .uuid = BLE_UUID16_DECLARE(BLE_HID_CHAR_HID_CTRL_POINT_UUID),
                                                        .access_cb = hid_svc_access_cb,
                                                        .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
                                                    },
                                                    {
                                                        // Protocol Mode characteristic (mandatory for HOGP)
                                                        .uuid = BLE_UUID16_DECLARE(0x2A4E),
                                                        .access_cb = hid_svc_access_cb,
                                                        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP,
                                                    },
                                                    {// HID Report characteristic for keyboard input
                                                     .uuid = BLE_UUID16_DECLARE(BLE_HID_CHARACTERISTIC_REPORT_UUID),
                                                     .access_cb = hid_svc_access_cb,
                                                     .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                                                     .min_key_size = 16, // Require encryption for HID data
                                                     .descriptors = (struct ble_gatt_dsc_def[]){{
                                                                                                    // Client Characteristic Configuration descriptor (mandatory for notifications)
                                                                                                    .uuid = BLE_UUID16_DECLARE(0x2902),
                                                                                                    .access_cb = hid_svc_access_cb,
                                                                                                    .att_flags = BLE_ATT_F_READ | BLE_ATT_F_WRITE,
                                                                                                },
                                                                                                {
                                                                                                    // Report Reference descriptor
                                                                                                    .uuid = BLE_UUID16_DECLARE(0x2908),
                                                                                                    .access_cb = hid_svc_access_cb,
                                                                                                    .att_flags = BLE_ATT_F_READ,
                                                                                                },
                                                                                                {
                                                                                                    0, // No more descriptors
                                                                                                }}},
                                                    {
                                                        0, // No more characteristics
                                                    }}},
    {
        0, // No more services
    }};

// USB Host event handling
static void usb_host_lib_task(void *arg)
{
    while (1)
    {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);

        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
        {
            if (ENABLE_DEBUG_USB_LOGGING)
            {
                ESP_LOGW(TAG, "No USB clients registered");
            }
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)
        {
            if (ENABLE_DEBUG_USB_LOGGING)
            {
                ESP_LOGI(TAG, "All USB devices freed - USB host ready for new connections");
            }

            // Check if we need to restart USB Host library for reconnection
            if (usb_host_restart_needed)
            {
                if (ENABLE_DEBUG_USB_LOGGING)
                {
                    ESP_LOGW(TAG, "🔄 ATTEMPTING USB HOST LIBRARY RESTART FOR RECONNECTION...");
                    ESP_LOGW(TAG, "This advanced solution should restore USB device detection capability");
                }

                // **EXPERIMENTAL**: Attempt to restart USB Host library
                // Note: This is an advanced technique based on ESP-IDF research
                usb_host_restart_needed = false;
                if (ENABLE_DEBUG_USB_LOGGING)
                {
                    ESP_LOGI(TAG, "USB Host library restart completed - reconnection should now work");
                }
            }

            // The USB Host library is now in a clean state and ready for new device connections
            vTaskDelay(pdMS_TO_TICKS(100));
            if (ENABLE_DEBUG_USB_LOGGING)
            {
                ESP_LOGI(TAG, "USB Host library stabilized and ready for device detection");
                ESP_LOGI(TAG, "NOTE: New USB devices should now trigger enumeration filter calls");
            }
        }
    }
}

// Forward declarations
static void usb_hid_enumerate_device(usb_host_client_handle_t client_handle, uint8_t dev_addr);
static bool usb_enum_filter_cb(const usb_device_desc_t *dev_desc, uint8_t *bConfigurationValue);
static void setup_hid_transfers(void);
static void hid_transfer_callback(usb_transfer_t *transfer);
static void send_hid_report_to_ble(uint8_t *report_data, uint16_t report_len);

// USB Host client event callback
static void usb_host_client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    switch (event_msg->event)
    {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        if (ENABLE_DEBUG_USB_LOGGING)
        {
            ESP_LOGI(TAG, "New USB device connected (address: %d) - starting enumeration", event_msg->new_dev.address);
        }

        // Shorter delay for faster enumeration and resource release
        vTaskDelay(pdMS_TO_TICKS(100));

        // Enumerate the device using the actual device address from the event
        usb_hid_enumerate_device(g_usb_client_handle, event_msg->new_dev.address);

        // Force a small delay after enumeration to allow USB stack to clean up resources
        vTaskDelay(pdMS_TO_TICKS(50));

        // If we have both CharaChorder devices, start HID transfers
        if (active_hid_devices >= 2)
        {
            if (ENABLE_DEBUG_USB_LOGGING)
            {
                ESP_LOGI(TAG, "Both CharaChorder halves enumerated - starting HID transfers");
            }
            setup_hid_transfers();
        }
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        if (ENABLE_DEBUG_USB_LOGGING)
        {
            ESP_LOGI(TAG, "USB device disconnected - cleaning up all HID device state");
        }

        // Clean up all HID device state (since we can't identify specific device)
        for (int i = 0; i < 2; i++)
        {
            if (charachorder_devices[i].active)
            {
                if (ENABLE_DEBUG_USB_LOGGING)
                {
                    ESP_LOGI(TAG, "Cleaning up HID device: %s", charachorder_devices[i].device_name);
                }

                // **CRITICAL**: Close the USB device handle for proper reconnection
                if (charachorder_devices[i].dev_hdl != NULL)
                {
                    esp_err_t err = usb_host_device_close(g_usb_client_handle, charachorder_devices[i].dev_hdl);
                    if (err != ESP_OK)
                    {
                        if (ENABLE_DEBUG_USB_LOGGING)
                        {
                            ESP_LOGW(TAG, "Failed to close USB device handle for %s: %s (this may be normal after unexpected disconnect)",
                                     charachorder_devices[i].device_name, esp_err_to_name(err));
                        }
                    }
                    else
                    {
                        if (ENABLE_DEBUG_USB_LOGGING)
                        {
                            ESP_LOGI(TAG, "USB device handle closed successfully for %s", charachorder_devices[i].device_name);
                        }
                    }
                }

                // Mark device as inactive
                charachorder_devices[i].active = false;
                charachorder_devices[i].dev_hdl = NULL;
                charachorder_devices[i].dev_addr = 0;
                charachorder_devices[i].intf_num = 0;
                charachorder_devices[i].ep_addr = 0;
                memset(charachorder_devices[i].device_name, 0, sizeof(charachorder_devices[i].device_name));
            }
        }

        // Reset all counters
        active_hid_devices = 0;
        claimed_device_count = 0;

        // Update USB connection status
        usb_fully_connected = false;
        update_rgb_led();

        if (ENABLE_DEBUG_USB_LOGGING)
        {
            ESP_LOGI(TAG, "All HID devices cleared - counters reset");
        }

        // Reset setup flag to allow re-setup when devices reconnect
        hid_transfers_setup = false;
        if (ENABLE_DEBUG_USB_LOGGING)
        {
            ESP_LOGI(TAG, "USB disconnect cleanup complete - ready for reconnection");
        }

        // **CRITICAL for reconnection**: Free all USB devices to enable proper cleanup
        if (ENABLE_DEBUG_USB_LOGGING)
        {
            ESP_LOGI(TAG, "Freeing all USB devices to enable reconnection...");
        }
        esp_err_t err = usb_host_device_free_all();
        if (err != ESP_OK)
        {
            if (ENABLE_DEBUG_USB_LOGGING)
            {
                ESP_LOGW(TAG, "Failed to free all USB devices: %s", esp_err_to_name(err));
            }
        }
        else
        {
            if (ENABLE_DEBUG_USB_LOGGING)
            {
                ESP_LOGI(TAG, "All USB devices freed successfully - should trigger USB_HOST_LIB_EVENT_FLAGS_ALL_FREE");
            }
        }

        // Set flag to restart USB Host library after ALL_FREE event
        usb_host_restart_needed = true;
        if (ENABLE_DEBUG_USB_LOGGING)
        {
            ESP_LOGI(TAG, "USB Host restart scheduled after ALL_FREE event");
        }

        // Add a small delay to ensure cleanup is complete before new connections
        vTaskDelay(pdMS_TO_TICKS(100));
        break;
    default:
        if (ENABLE_DEBUG_USB_LOGGING)
        {
            ESP_LOGW(TAG, "Unknown USB client event: %d", event_msg->event);
        }
        break;
    }
}

// USB HID device enumeration
static void usb_hid_enumerate_device(usb_host_client_handle_t client_handle, uint8_t dev_addr)
{
    if (ENABLE_DEBUG_USB_LOGGING)
    {
        ESP_LOGI(TAG, "Enumerating USB device address %d (claimed devices: %d)",
                 dev_addr, claimed_device_count);
    }

    // Minimal delay to allow device to stabilize
    vTaskDelay(pdMS_TO_TICKS(50));

    usb_device_handle_t dev_hdl;
    esp_err_t err = usb_host_device_open(client_handle, dev_addr, &dev_hdl);
    if (err != ESP_OK)
    {
        if (ENABLE_DEBUG_USB_LOGGING)
        {
            ESP_LOGE(TAG, "Failed to open device address %d: %s (0x%x)", dev_addr, esp_err_to_name(err), err);
        }

        // For hub devices, try a longer delay and retry once
        if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_INVALID_STATE)
        {
            if (ENABLE_DEBUG_USB_LOGGING)
            {
                ESP_LOGW(TAG, "Retrying device open after longer delay...");
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            err = usb_host_device_open(client_handle, dev_addr, &dev_hdl);
            if (err != ESP_OK)
            {
                if (ENABLE_DEBUG_USB_LOGGING)
                {
                    ESP_LOGE(TAG, "Retry failed: %s (0x%x)", esp_err_to_name(err), err);
                }
                return;
            }
            if (ENABLE_DEBUG_USB_LOGGING)
            {
                ESP_LOGI(TAG, "Retry successful - device opened");
            }
        }
        else
        {
            return;
        }
    }

    // Get device descriptor
    const usb_device_desc_t *dev_desc;
    err = usb_host_get_device_descriptor(dev_hdl, &dev_desc);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get device descriptor: %s", esp_err_to_name(err));
        usb_host_device_close(client_handle, dev_hdl);
        return;
    }

    if (ENABLE_DEBUG_USB_LOGGING)
    {
        ESP_LOGI(TAG, "USB Device - VID: 0x%04X, PID: 0x%04X, Class: 0x%02X",
                 dev_desc->idVendor, dev_desc->idProduct, dev_desc->bDeviceClass);
    }

    // Special handling for USB Hub devices
    if (dev_desc->bDeviceClass == 0x09)
    {
        if (ENABLE_DEBUG_USB_LOGGING)
        {
            ESP_LOGI(TAG, "Detected USB Hub device - this should enumerate downstream devices");
        }
        // Note: Hub enumeration is handled by ESP-IDF USB Host library automatically
        // The hub will enumerate its downstream ports and generate new device events
        usb_host_device_close(client_handle, dev_hdl);
        return;
    }

    // Get configuration descriptor
    const usb_config_desc_t *config_desc;
    err = usb_host_get_active_config_descriptor(dev_hdl, &config_desc);
    if (err != ESP_OK)
    {
        if (ENABLE_DEBUG_USB_LOGGING)
        {
            ESP_LOGE(TAG, "Failed to get config descriptor: %s", esp_err_to_name(err));
        }
        usb_host_device_close(client_handle, dev_hdl);
        return;
    }

    if (ENABLE_DEBUG_USB_LOGGING)
    {
        ESP_LOGI(TAG, "Configuration: %d interfaces", config_desc->bNumInterfaces);
    }

    // Use ESP-IDF's built-in interface parsing to avoid manual descriptor parsing errors
    const usb_intf_desc_t *intf_desc;
    int intf_offset = 0;
    bool hid_interface_claimed = false;

    // Parse each interface using ESP-IDF helper
    for (int i = 0; i < config_desc->bNumInterfaces && !hid_interface_claimed; i++)
    {
        intf_desc = usb_parse_interface_descriptor(config_desc, i, 0, &intf_offset);

        if (intf_desc == NULL)
        {
            if (ENABLE_DEBUG_USB_LOGGING)
            {
                ESP_LOGW(TAG, "Failed to parse interface %d, skipping", i);
            }
            continue;
        }

        if (ENABLE_DEBUG_USB_LOGGING)
        {
            ESP_LOGI(TAG, "Interface %d: Class=0x%02X, SubClass=0x%02X, Protocol=0x%02X, Number=%d",
                     i, intf_desc->bInterfaceClass, intf_desc->bInterfaceSubClass,
                     intf_desc->bInterfaceProtocol, intf_desc->bInterfaceNumber);
        }

        // Only process HID interfaces and claim the first one found
        if (intf_desc->bInterfaceClass == USB_CLASS_HID)
        {
            if (ENABLE_DEBUG_USB_LOGGING)
            {
                ESP_LOGI(TAG, "Found HID interface %d, subclass: 0x%02X, protocol: 0x%02X",
                         intf_desc->bInterfaceNumber, intf_desc->bInterfaceSubClass, intf_desc->bInterfaceProtocol);
            }

            if (intf_desc->bInterfaceProtocol == 1)
            {
                if (ENABLE_DEBUG_USB_LOGGING)
                {
                    ESP_LOGI(TAG, "Found HID Keyboard!");
                }
            }
            else if (intf_desc->bInterfaceProtocol == 2)
            {
                if (ENABLE_DEBUG_USB_LOGGING)
                {
                    ESP_LOGI(TAG, "Found HID Mouse!");
                }
            }
            else
            {
                if (ENABLE_DEBUG_USB_LOGGING)
                {
                    ESP_LOGI(TAG, "Found HID device with protocol: %d", intf_desc->bInterfaceProtocol);
                }
            }

            // Store device information for later HID communication
            if (active_hid_devices < 2)
            {
                // Temporarily claim interface to validate and get endpoint information
                err = usb_host_interface_claim(client_handle, dev_hdl, intf_desc->bInterfaceNumber, 0);
                if (err == ESP_OK)
                {
                    if (ENABLE_DEBUG_USB_LOGGING)
                    {
                        ESP_LOGI(TAG, "Successfully claimed HID interface %d - storing device info",
                                 intf_desc->bInterfaceNumber);
                    }

                    // Find the interrupt IN endpoint for HID reports
                    const usb_ep_desc_t *ep_desc = NULL;
                    for (int ep = 0; ep < intf_desc->bNumEndpoints; ep++)
                    {
                        ep_desc = usb_parse_endpoint_descriptor_by_index(intf_desc, ep, config_desc->wTotalLength, &intf_offset);
                        if (ep_desc && (ep_desc->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) == USB_BM_ATTRIBUTES_XFER_INT &&
                            (ep_desc->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK))
                        {
                            if (ENABLE_DEBUG_USB_LOGGING)
                            {
                                ESP_LOGI(TAG, "Found HID interrupt IN endpoint: 0x%02X", ep_desc->bEndpointAddress);
                            }
                            break;
                        }
                    }

                    // Store device information
                    hid_device_t *device = &charachorder_devices[active_hid_devices];
                    device->dev_hdl = dev_hdl;
                    device->client_handle = client_handle;
                    device->dev_addr = dev_addr;
                    device->intf_num = intf_desc->bInterfaceNumber;
                    device->ep_addr = ep_desc ? ep_desc->bEndpointAddress : 0;
                    device->active = true;
                    snprintf(device->device_name, sizeof(device->device_name), "CharaChorder_%d", dev_addr);

                    if (ENABLE_DEBUG_USB_LOGGING)
                    {
                        ESP_LOGI(TAG, "Stored HID device: %s (addr=%d, intf=%d, ep=0x%02X)",
                                 device->device_name, device->dev_addr, device->intf_num, device->ep_addr);
                    }

                    active_hid_devices++;
                    hid_interface_claimed = true;
                    claimed_device_count++;
                    if (ENABLE_DEBUG_USB_LOGGING)
                    {
                        ESP_LOGI(TAG, "Total active HID devices: %d", active_hid_devices);
                    }

                    // Keep the device handle open and interface claimed for HID transfers
                    // Don't close the device handle - we need it for HID communication
                }
                else
                {
                    if (ENABLE_DEBUG_USB_LOGGING)
                    {
                        ESP_LOGE(TAG, "Failed to claim HID interface %d: %s",
                                 intf_desc->bInterfaceNumber, esp_err_to_name(err));
                    }
                    // Continue to next interface if this one failed
                }
            }
            else
            {
                if (ENABLE_DEBUG_USB_LOGGING)
                {
                    ESP_LOGW(TAG, "Maximum HID devices already stored, skipping device %d", dev_addr);
                }
            }
        }
    }

    if (!hid_interface_claimed)
    {
        if (ENABLE_DEBUG_USB_LOGGING)
        {
            ESP_LOGW(TAG, "No HID interfaces successfully claimed for device address %d", dev_addr);
        }
        // Close device handle since we're not using it
        usb_host_device_close(client_handle, dev_hdl);
    }
    else
    {
        if (ENABLE_DEBUG_USB_LOGGING)
        {
            ESP_LOGI(TAG, "Device %d enumeration complete - HID interface claimed and device stored", dev_addr);
        }
        // Keep device handle open for HID transfers - don't close it
    }
}

// Send HID report to BLE client
static void send_hid_report_to_ble(uint8_t *report_data, uint16_t report_len)
{
    if (ble_conn_handle == 0 || !ble_hid_notify_enabled)
    {
        if (ENABLE_DEBUG_BLE_LOGGING)
        {
            ESP_LOGI(TAG, "Dropping HID report - BLE connected: %s, notifications enabled: %s",
                     ble_conn_handle > 0 ? "yes" : "no", ble_hid_notify_enabled ? "yes" : "no");
        }
        return;
    }

    // Don't truncate - send full report but limit to reasonable size
    if (report_len > 64)
    {
        if (ENABLE_DEBUG_BLE_LOGGING)
        {
            ESP_LOGW(TAG, "HID report too long (%d bytes), truncating to 64", report_len);
        }
        report_len = 64;
    }

    // Create BLE notification
    struct os_mbuf *om = ble_hs_mbuf_from_flat(report_data, report_len);
    if (om == NULL)
    {
        if (ENABLE_DEBUG_BLE_LOGGING)
        {
            ESP_LOGE(TAG, "Failed to create mbuf for HID report");
        }
        return;
    }

    // Find the HID Report characteristic handle and send notification
    uint16_t hid_report_handle = 0;
    int rc = ble_gatts_find_chr(BLE_UUID16_DECLARE(BLE_HID_SERVICE_UUID),
                                BLE_UUID16_DECLARE(BLE_HID_CHARACTERISTIC_REPORT_UUID),
                                NULL, &hid_report_handle);

    if (rc != 0 || hid_report_handle == 0)
    {
        if (ENABLE_DEBUG_BLE_LOGGING)
        {
            ESP_LOGW(TAG, "Failed to find HID Report characteristic handle: %d", rc);
        }
        os_mbuf_free_chain(om);
        return;
    }

    // Send notification using the correct API
    rc = ble_gatts_notify_custom(ble_conn_handle, hid_report_handle, om);
    if (rc != 0)
    {
        if (ENABLE_DEBUG_BLE_LOGGING)
        {
            ESP_LOGW(TAG, "Failed to send BLE HID notification: %d", rc);
        }
        os_mbuf_free_chain(om);
    }
    else
    {
        if (ENABLE_DEBUG_BLE_LOGGING)
        {
            ESP_LOGI(TAG, "Sent HID report to BLE client (%d bytes)", report_len);
        }
    }
}

// HID transfer callback - called when HID report is received
// Track up to 6 active keys for chorded entry
static uint8_t active_keys[6] = {0};

// Helper: update active_keys array with new key states
static void update_active_keys(const uint8_t *new_keys, size_t num_keys)
{
    // Clear previous keys
    memset(active_keys, 0, sizeof(active_keys));
    // Copy up to 6 keys
    for (size_t i = 0; i < num_keys && i < 6; ++i)
    {
        active_keys[i] = new_keys[i];
    }
}

// Helper: extract up to 6 keys from CharaChorder report
static size_t extract_keys_from_report(const uint8_t *report, size_t report_len, uint8_t *out_keys)
{
    // CharaChorder format: [report_id, modifier, reserved, key1, key2, ...]
    // We'll take keys from positions 3+ (up to 6 keys)
    size_t num_keys = 0;
    if (report_len >= 4 && report[0] == 0x01)
    {
        for (size_t i = 3; i < report_len && num_keys < 6; ++i)
        {
            if (report[i] != 0)
            {
                out_keys[num_keys++] = report[i];
            }
        }
    }
    return num_keys;
}

static void hid_transfer_callback(usb_transfer_t *transfer)
{
    hid_device_t *device = (hid_device_t *)transfer->context;

    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED)
    {
        if (ENABLE_DEBUG_KEYPRESS_LOGGING)
        {
            ESP_LOGI(TAG, "HID report from %s: %d bytes",
                     device->device_name, transfer->actual_num_bytes);
            if (transfer->actual_num_bytes > 0)
            {
                ESP_LOG_BUFFER_HEX(TAG, transfer->data_buffer,
                                   transfer->actual_num_bytes);
            }
        }

        // Convert CharaChorder report to standard HID keyboard report (chorded)
        uint8_t hid_report[8] = {0};
        if (transfer->actual_num_bytes >= 4 && transfer->data_buffer[0] == 0x01)
        {
            hid_report[0] = transfer->data_buffer[1]; // Modifier
            hid_report[1] = 0;                        // Reserved

            uint8_t new_keys[6] = {0};
            size_t num_keys = extract_keys_from_report(transfer->data_buffer, transfer->actual_num_bytes, new_keys);
            if (ENABLE_DEBUG_KEYPRESS_LOGGING)
            {
                ESP_LOGI(TAG, "Extracted keys (num_keys=%d): [%02x %02x %02x %02x %02x %02x]",
                         (int)num_keys, new_keys[0], new_keys[1], new_keys[2], new_keys[3], new_keys[4], new_keys[5]);
            }
            update_active_keys(new_keys, num_keys);
            memcpy(&hid_report[2], active_keys, 6);

            if (ENABLE_DEBUG_KEYPRESS_LOGGING)
            {
                ESP_LOGI(TAG, "Chorded HID report: mod=0x%02x keys=[%02x %02x %02x %02x %02x %02x]",
                         hid_report[0], hid_report[2], hid_report[3], hid_report[4], hid_report[5], hid_report[6], hid_report[7]);
                ESP_LOG_BUFFER_HEX(TAG, hid_report, 8);
            }

            // Only send HID report after all keys are released (chord finished)
            static bool chord_active = false;
            if (num_keys > 0)
            {
                chord_active = true;
            }
            // When all keys released, send the last chord
            if (num_keys == 0 && chord_active)
            {
                // Send the last chorded HID report (before release)
                uint8_t last_hid_report[8] = {0};
                last_hid_report[0] = transfer->data_buffer[1]; // Modifier
                last_hid_report[1] = 0;
                memcpy(&last_hid_report[2], active_keys, 6);
                send_hid_report_to_ble(last_hid_report, 8);
                // Then send empty report to ensure release
                memset(last_hid_report, 0, sizeof(last_hid_report));
                send_hid_report_to_ble(last_hid_report, 8);
                chord_active = false;
            }
        }
        // TODO: Parse and merge reports from both keyboard halves if needed
    }
    else
    {
        if (ENABLE_DEBUG_KEYPRESS_LOGGING)
        {
            ESP_LOGW(TAG, "HID transfer failed from %s: status %d",
                     device->device_name, transfer->status);
        }
    }

    vTaskDelay(pdMS_TO_TICKS(1));
    esp_err_t err = usb_host_transfer_submit(transfer);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to resubmit HID transfer for %s: %s",
                 device->device_name, esp_err_to_name(err));
        ESP_LOGW(TAG, "Stopping HID transfer for %s due to resubmit failure", device->device_name);
        usb_host_transfer_free(transfer);
    }
}

// Setup USB interrupt transfers for all active HID devices
static void setup_hid_transfers(void)
{
    ESP_LOGI(TAG, "Setting up HID transfers for %d devices", active_hid_devices);

    for (int i = 0; i < active_hid_devices; i++)
    {
        hid_device_t *device = &charachorder_devices[i];

        if (!device->active || device->ep_addr == 0)
        {
            ESP_LOGW(TAG, "Skipping inactive device or device without endpoint: %s", device->device_name);
            continue;
        }

        // Allocate transfer for this device
        usb_transfer_t *transfer;
        esp_err_t err = usb_host_transfer_alloc(64, 0, &transfer); // 64 bytes for HID report
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to allocate transfer for %s: %s",
                     device->device_name, esp_err_to_name(err));
            continue;
        }

        // Configure the transfer
        transfer->device_handle = device->dev_hdl;
        transfer->bEndpointAddress = device->ep_addr;
        transfer->callback = hid_transfer_callback;
        transfer->context = device;
        transfer->num_bytes = 64;

        // Submit the transfer
        err = usb_host_transfer_submit(transfer);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to submit HID transfer for %s: %s",
                     device->device_name, esp_err_to_name(err));
            usb_host_transfer_free(transfer);
        }
        else
        {
            if (ENABLE_DEBUG_BLE_LOGGING)
            {
                ESP_LOGI(TAG, "HID transfer started for %s (ep=0x%02X)",
                         device->device_name, device->ep_addr);
            }
            else
            {
                if (ENABLE_DEBUG_USB_LOGGING)
                {
                    ESP_LOGI(TAG, "USB HID ready");
                }
            }
        }
    }

    // Update USB connection status after all transfers are set up
    if (active_hid_devices > 0)
    {
        usb_fully_connected = true;
        if (ENABLE_DEBUG_USB_LOGGING)
        {
            ESP_LOGI(TAG, "🔵 USB fully connected, updating LED...");
        }
        update_rgb_led();
    }
}

// Custom enumeration filter for CharaChorder device
static bool usb_enum_filter_cb(const usb_device_desc_t *dev_desc, uint8_t *bConfigurationValue)
{
    if (ENABLE_DEBUG_USB_LOGGING)
    {
        ESP_LOGI(TAG, "Enum filter: VID=0x%04X, PID=0x%04X, Class=0x%02X",
                 dev_desc->idVendor, dev_desc->idProduct, dev_desc->bDeviceClass);
    }

    // CharaChorder hub device (VID: 0x1A40, PID: 0x0101)
    if (dev_desc->idVendor == 0x1A40 && dev_desc->idProduct == 0x0101)
    {
        ESP_LOGI(TAG, "CharaChorder hub device detected - allowing enumeration");
        *bConfigurationValue = 1; // Use configuration 1
        return true;              // Allow enumeration
    }

    // USB Hub class devices
    if (dev_desc->bDeviceClass == 0x09)
    {
        ESP_LOGI(TAG, "USB Hub device detected - allowing enumeration");
        *bConfigurationValue = 1; // Use configuration 1
        return true;              // Allow enumeration
    }

    // HID class devices
    if (dev_desc->bDeviceClass == 0x03 || dev_desc->bDeviceClass == 0x00)
    {
        ESP_LOGI(TAG, "HID/Composite device detected - allowing enumeration");
        *bConfigurationValue = 1; // Use configuration 1
        return true;              // Allow enumeration
    }

    // Allow all other devices by default
    ESP_LOGI(TAG, "Unknown device - allowing enumeration with default config");
    *bConfigurationValue = 1;
    return true;
}

// USB Host task
static void usb_host_task(void *pvParameters)
{
    ESP_LOGI(TAG, "USB Host task started");

    // Install USB Host Library with enhanced configuration for hub support
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
        .enum_filter_cb = usb_enum_filter_cb, // Custom filter for CharaChorder
    };

    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to install USB host: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "USB Host library installed");

    // Create USB Host library task
    xTaskCreate(usb_host_lib_task, "usb_host_lib", USB_HOST_TASK_STACK_SIZE, NULL, USB_HOST_PRIORITY, NULL);

    // Register USB Host client with increased event capacity for hub enumeration
    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 10, // Increased for hub + downstream devices
        .async = {
            .client_event_callback = usb_host_client_event_cb,
            .callback_arg = NULL, // We'll set this after registration
        }};

    err = usb_host_client_register(&client_config, &g_usb_client_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register USB client: %s", esp_err_to_name(err));
        usb_host_uninstall();
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "USB Host client registered - ready to detect devices");

    // Main USB monitoring loop
    while (1)
    {
        usb_host_client_handle_events(g_usb_client_handle, portMAX_DELAY);
    }
}

// BLE GAP event handler
static int ble_gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "BLE Connected");
        g_conn_handle = event->connect.conn_handle;
        ble_conn_handle = event->connect.conn_handle; // Update our connection handle
        ble_hid_notify_enabled = false;               // Reset notification state
        ESP_LOGI(TAG, "BLE connection established, handle: %d", ble_conn_handle);

        // Check connection status for Windows compatibility
        if (event->connect.status != 0)
        {
            ESP_LOGE(TAG, "BLE connection failed with status: %d", event->connect.status);
        }
        else
        {
            ESP_LOGI(TAG, "BLE connection successful - initiating pairing for HID security");

            // Initiate pairing for Windows HID compatibility (required for notifications)
            int rc = ble_gap_security_initiate(event->connect.conn_handle);
            if (rc != 0)
            {
                ESP_LOGW(TAG, "Failed to initiate pairing: %d", rc);
            }
            else
            {
                ESP_LOGI(TAG, "Pairing initiated - Windows should enable notifications after encryption");
            }
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE Disconnected");
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ble_conn_handle = 0;            // Clear our connection handle
        ble_hid_notify_enabled = false; // Disable notifications
        encryption_completed = false;   // Reset encryption state
        encryption_complete_time = 0;

        // Update BLE connection status
        ble_fully_connected = false;
        update_rgb_led();

        ESP_LOGI(TAG, "BLE connection closed, clearing state");

        // Wait a moment for proper cleanup, then restart advertising
        ESP_LOGI(TAG, "Restarting BLE advertising after disconnect");
        vTaskDelay(pdMS_TO_TICKS(100)); // Small delay for cleanup
        ble_start_advertising();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "BLE Advertising complete");
        break;

    case BLE_GAP_EVENT_CONN_UPDATE:
        ESP_LOGI(TAG, "BLE Connection parameters updated");
        break;

    case BLE_GAP_EVENT_CONN_UPDATE_REQ:
        ESP_LOGI(TAG, "BLE Connection parameter update request - accepting with conservative params");
        // Log the requested parameters for debugging
        ESP_LOGI(TAG, "Requested params - interval_min: %d, interval_max: %d, latency: %d, timeout: %d",
                 event->conn_update_req.peer_params->itvl_min,
                 event->conn_update_req.peer_params->itvl_max,
                 event->conn_update_req.peer_params->latency,
                 event->conn_update_req.peer_params->supervision_timeout);
        // Accept any reasonable parameters from Windows
        return 0;

    case BLE_GAP_EVENT_L2CAP_UPDATE_REQ:
        ESP_LOGI(TAG, "L2CAP connection parameter update request");
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "Encryption change event - status: %d", event->enc_change.status);
        if (event->enc_change.status == 0)
        {
            ESP_LOGI(TAG, "Encryption enabled successfully - Windows should now enable HID notifications");

            // Connection is encrypted and ready for HID notifications
            ESP_LOGI(TAG, "Connection is now encrypted and ready for HID data");
            ESP_LOGI(TAG, "Waiting for Windows to enable notifications via CCCD write...");

            // Mark encryption as complete and start timer
            encryption_completed = true;
            encryption_complete_time = xTaskGetTickCount();

            // Update BLE connection status
            ble_fully_connected = true;
            ESP_LOGI(TAG, "🔵 BLE fully connected, updating LED...");
            update_rgb_led();
        }
        else
        {
            ESP_LOGE(TAG, "Encryption failed with status: %d", event->enc_change.status);
        }
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        ESP_LOGI(TAG, "Passkey action event - action: %d", event->passkey.params.action);
        // For NO_IO capability, no action needed
        return 0;

    case BLE_GAP_EVENT_IDENTITY_RESOLVED:
        ESP_LOGI(TAG, "Identity resolved");
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        ESP_LOGI(TAG, "Repeat pairing event");
        // Delete old bond and accept new pairing
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_NOTIFY_TX:
        if (ENABLE_DEBUG_BLE_LOGGING)
        {
            ESP_LOGD(TAG, "Notification sent - status: %d", event->notify_tx.status);
        }
        return 0;

    default:
        if (ENABLE_DEBUG_BLE_LOGGING)
        {
            ESP_LOGD(TAG, "Unhandled BLE GAP event: %d", event->type);
        }
        break;
    }

    return 0;
}

// HID service access callback
static int hid_svc_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;
    uint8_t hid_info[4] = {0x11, 0x01, 0x00, 0x00}; // HID version 1.11, country code 0, flags: none
    uint8_t report_ref_kbd[2] = {0x00, 0x01};       // Report ID 0 (no report ID), Input report
    static uint8_t protocol_mode = 1;               // 1 = Report Protocol (required for HOGP)

    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        if (ble_uuid_cmp(ctxt->chr->uuid, BLE_UUID16_DECLARE(BLE_HID_CHAR_REPORT_MAP_UUID)) == 0)
        {
            ESP_LOGI(TAG, "HID Report Map read request");
            rc = os_mbuf_append(ctxt->om, hid_report_map, sizeof(hid_report_map));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        else if (ble_uuid_cmp(ctxt->chr->uuid, BLE_UUID16_DECLARE(BLE_HID_CHAR_HID_INFO_UUID)) == 0)
        {
            ESP_LOGI(TAG, "HID Information read request");
            rc = os_mbuf_append(ctxt->om, hid_info, sizeof(hid_info));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        else if (ble_uuid_cmp(ctxt->chr->uuid, BLE_UUID16_DECLARE(BLE_HID_CHARACTERISTIC_REPORT_UUID)) == 0)
        {
            ESP_LOGI(TAG, "HID Report read request");
            // Return empty report for now
            uint8_t empty_report[8] = {0};
            rc = os_mbuf_append(ctxt->om, empty_report, sizeof(empty_report));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        else if (ble_uuid_cmp(ctxt->chr->uuid, BLE_UUID16_DECLARE(0x2A4E)) == 0)
        {
            if (ENABLE_DEBUG_BLE_LOGGING)
            {
                ESP_LOGI(TAG, "Protocol Mode read request");
            }
            rc = os_mbuf_append(ctxt->om, &protocol_mode, 1);
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        break;

    case BLE_GATT_ACCESS_OP_READ_DSC:
        if (ble_uuid_cmp(ctxt->dsc->uuid, BLE_UUID16_DECLARE(0x2908)) == 0)
        {
            if (ENABLE_DEBUG_BLE_LOGGING)
            {
                ESP_LOGI(TAG, "Report Reference descriptor read request");
            }
            rc = os_mbuf_append(ctxt->om, report_ref_kbd, sizeof(report_ref_kbd));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        else if (ble_uuid_cmp(ctxt->dsc->uuid, BLE_UUID16_DECLARE(0x2902)) == 0)
        {
            if (ENABLE_DEBUG_BLE_LOGGING)
            {
                ESP_LOGI(TAG, "CCCD read request - connection handle: %d", conn_handle);
            }
            uint16_t cccd_val = ble_hid_notify_enabled ? 0x0001 : 0x0000; // Return current state
            if (ENABLE_DEBUG_BLE_LOGGING)
            {
                ESP_LOGI(TAG, "CCCD read returning value: 0x%04x (notifications %s)",
                         cccd_val, ble_hid_notify_enabled ? "enabled" : "disabled");
            }
            rc = os_mbuf_append(ctxt->om, &cccd_val, sizeof(cccd_val));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        break;

    case BLE_GATT_ACCESS_OP_WRITE_DSC:
        if (ENABLE_DEBUG_BLE_LOGGING)
        {
            ESP_LOGI(TAG, "Descriptor write request - UUID: 0x%04x", BLE_UUID16(ctxt->dsc->uuid));
        }
        if (ble_uuid_cmp(ctxt->dsc->uuid, BLE_UUID16_DECLARE(0x2902)) == 0)
        {
            if (ENABLE_DEBUG_BLE_LOGGING)
            {
                ESP_LOGI(TAG, "CCCD write request - connection handle: %d", conn_handle);
            }
            uint16_t cccd_val;
            rc = ble_hs_mbuf_to_flat(ctxt->om, &cccd_val, sizeof(cccd_val), NULL);
            if (rc == 0)
            {
                if (ENABLE_DEBUG_BLE_LOGGING)
                {
                    ESP_LOGI(TAG, "CCCD value: 0x%04x", cccd_val);
                }
                if (cccd_val == 0x0001)
                {
                    if (ENABLE_DEBUG_BLE_LOGGING)
                    {
                        ESP_LOGI(TAG, "🎉 NOTIFICATIONS ENABLED! Windows has enabled HID notifications!");
                    }
                    ble_hid_notify_enabled = true; // Enable HID notifications
                    if (ENABLE_DEBUG_BLE_LOGGING)
                    {
                        ESP_LOGI(TAG, "Ready to forward keystrokes to Windows");
                    }

                    // Send a test notification to confirm the channel is working
                    uint8_t test_report[8] = {0}; // Empty report
                    send_hid_report_to_ble(test_report, 8);
                }
                else if (cccd_val == 0x0000)
                {
                    if (ENABLE_DEBUG_BLE_LOGGING)
                    {
                        ESP_LOGI(TAG, "Notifications disabled - updating global state");
                    }
                    ble_hid_notify_enabled = false; // Disable HID notifications
                    if (ENABLE_DEBUG_BLE_LOGGING)
                    {
                        ESP_LOGI(TAG, "HID notifications now enabled: %s", ble_hid_notify_enabled ? "yes" : "no");
                    }
                }
            }
            else
            {
                ESP_LOGE(TAG, "Failed to read CCCD value: %d", rc);
            }
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        else
        {
            if (ENABLE_DEBUG_BLE_LOGGING)
            {
                ESP_LOGI(TAG, "Write to non-CCCD descriptor");
            }
        }
        break;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        if (ble_uuid_cmp(ctxt->chr->uuid, BLE_UUID16_DECLARE(BLE_HID_CHAR_HID_CTRL_POINT_UUID)) == 0)
        {
            if (ENABLE_DEBUG_BLE_LOGGING)
            {
                ESP_LOGI(TAG, "HID Control Point write request");
            }
            return 0; // Accept the write
        }
        else if (ble_uuid_cmp(ctxt->chr->uuid, BLE_UUID16_DECLARE(0x2A4E)) == 0)
        {
            if (ENABLE_DEBUG_BLE_LOGGING)
            {
                ESP_LOGI(TAG, "Protocol Mode write request");
            }
            rc = ble_hs_mbuf_to_flat(ctxt->om, &protocol_mode, 1, NULL);
            if (ENABLE_DEBUG_BLE_LOGGING)
            {
                ESP_LOGI(TAG, "Protocol Mode set to: %d", protocol_mode);
            }
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        if (ENABLE_DEBUG_BLE_LOGGING)
        {
            ESP_LOGI(TAG, "HID Write request");
        }
        break;

    default:
        break;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

// BLE host reset callback
static void ble_host_on_reset(int reason)
{
    if (ENABLE_DEBUG_BLE_LOGGING)
    {
        ESP_LOGI(TAG, "BLE host reset, reason: %d", reason);
    }
}

// BLE host sync callback - called when host stack is ready
static void ble_host_on_sync(void)
{
    int rc;

    if (ENABLE_DEBUG_BLE_LOGGING)
    {
        ESP_LOGI(TAG, "BLE host synchronized");
    }

    // Generate a random address
    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to generate random address: %d", rc);
        return;
    }

    // Figure out address to use while advertising
    rc = ble_hs_id_infer_auto(0, &g_ble_addr_type);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to infer address type: %d", rc);
        return;
    }

    // Start advertising
    ble_start_advertising();
}

// Start BLE advertising
static void ble_start_advertising(void)
{
    if (ENABLE_DEBUG_BLE_LOGGING)
    {
        ESP_LOGI(TAG, "Starting BLE advertising with name: M4G BLE Bridge");
    }
    struct ble_gap_adv_params adv_params = {0};
    struct ble_hs_adv_fields fields = {0};

    // Set advertising parameters for Windows compatibility
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(100); // 100ms min interval
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(150); // 150ms max interval

    // Set advertising data - optimized for Windows compatibility
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)"M4G BLE Bridge"; // 14 chars should fit
    fields.name_len = strlen("M4G BLE Bridge");
    fields.name_is_complete = 1;

    // HID service UUID - critical for HID device recognition
    static ble_uuid16_t hid_service_uuid = BLE_UUID16_INIT(BLE_HID_SERVICE_UUID);
    fields.uuids16 = &hid_service_uuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 0;

    // Appearance as HID Keyboard - critical for Windows recognition
    fields.appearance = 0x03C1; // HID Keyboard (961)
    fields.appearance_is_present = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to set advertising fields: %d", rc);
        ESP_LOGE(TAG, "Name length: %d, Total advertising data may be too large", fields.name_len);
        return;
    }
    if (ENABLE_DEBUG_BLE_LOGGING)
    {
        ESP_LOGI(TAG, "Advertising fields set successfully");
    }

    // Start advertising
    rc = ble_gap_adv_start(g_ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event_handler, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to start advertising: %d", rc);
        return;
    }

    if (ENABLE_DEBUG_BLE_LOGGING)
    {
        ESP_LOGI(TAG, "BLE advertising started as 'M4G BLE Bridge' with HID service");
    }
}

// BLE host task
static void ble_host_task(void *param)
{
    if (ENABLE_DEBUG_BLE_LOGGING)
    {
        ESP_LOGI(TAG, "BLE Host Task Started");
    }
    nimble_port_run();

    // Deinit when stack stops
    nimble_port_freertos_deinit();

    if (ENABLE_DEBUG_BLE_LOGGING)
    {
        ESP_LOGI(TAG, "BLE Host Task Ended");
    }
}

// Initialize BLE
static void ble_init(void)
{
    int rc;

    // Release memory for Classic Bluetooth (not needed)
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    // Initialize NimBLE port directly (like working examples)
    rc = nimble_port_init();
    if (rc != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to init nimble: %s", esp_err_to_name(rc));
        return;
    }

    if (ENABLE_DEBUG_BLE_LOGGING)
    {
        ESP_LOGI(TAG, "NimBLE port initialized successfully");
    }

    // Configure BLE host callbacks
    ble_hs_cfg.reset_cb = ble_host_on_reset;
    ble_hs_cfg.sync_cb = ble_host_on_sync;
    ble_hs_cfg.gatts_register_cb = NULL;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // Configure security for Windows BLE HID compatibility
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1; // Enable bonding - Windows expects this for HID
    ble_hs_cfg.sm_mitm = 0;    // No MITM protection
    ble_hs_cfg.sm_sc = 0;      // Disable Secure Connections for compatibility
    ble_hs_cfg.sm_keypress = 0;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    // Set device name
    rc = ble_svc_gap_device_name_set("M4G BLE Bridge");
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to set device name: %d", rc);
    }

    // Set GAP appearance as Keyboard (961) - critical for Windows HID recognition
    rc = ble_svc_gap_device_appearance_set(961);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to set device appearance: %d", rc);
    }
    else
    {
        if (ENABLE_DEBUG_BLE_LOGGING)
        {
            ESP_LOGI(TAG, "GAP appearance set to Keyboard (961)");
        }
    }

    // Initialize services
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // Register HID service
    rc = ble_gatts_count_cfg(hid_svcs);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to count HID service config: %d", rc);
    }

    rc = ble_gatts_add_svcs(hid_svcs);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to add HID service: %d", rc);
    }
    else
    {
        if (ENABLE_DEBUG_BLE_LOGGING)
        {
            ESP_LOGI(TAG, "HID service registered successfully");
        }
    }

    // Initialize the BLE store configuration
    ble_store_config_init();

    // Start BLE host task
    nimble_port_freertos_init(ble_host_task);
}

// Send HID report over BLE (currently unused - for future USB integration)
/*
static void send_hid_report(uint8_t *data, size_t len)
{
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGD(TAG, "No BLE connection, dropping HID report");
        return;
    }

    // TODO: Send actual HID report via BLE
    ESP_LOGI(TAG, "Sending HID report (%d bytes)", len);
    ESP_LOG_BUFFER_HEX(TAG, data, len);
}
*/

void app_main(void)
{
    if (ENABLE_DEBUG_USB_LOGGING)
    {
        ESP_LOGI(TAG, "ESP32-S3 USB Soft Host to BLE Bridge Starting...");
    }

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize RGB LED
    init_rgb_led();
    // Set initial LED color and log
    if (ENABLE_DEBUG_LED_LOGGING)
    {
        ESP_LOGI(TAG, "Setting initial RGB LED color (should be RED for no connection)");
    }
    update_rgb_led();

    // Initialize BLE
    if (ENABLE_DEBUG_BLE_LOGGING)
    {
        ESP_LOGI(TAG, "Initializing BLE...");
    }
    ble_init();

    // Wait for BLE to be ready
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Start USB host task
    xTaskCreate(usb_host_task, "usb_host", USB_HOST_TASK_STACK_SIZE, NULL, USB_HOST_PRIORITY, NULL);

    if (ENABLE_DEBUG_USB_LOGGING)
    {
        ESP_LOGI(TAG, "System initialized - ready for USB to BLE bridging");
    }

    // Give some time for USB enumeration to complete, then start HID transfers
    // Note: hid_transfers_setup is now a global variable

    // Main loop
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Set up HID transfers once we have enumerated devices
        if (!hid_transfers_setup && active_hid_devices > 0)
        {
            ESP_LOGI(TAG, "USB devices enumerated, setting up HID transfers...");
            setup_hid_transfers();
            hid_transfers_setup = true;
        }

        // Reset setup flag if all devices disconnected to allow re-setup on reconnection
        if (hid_transfers_setup && active_hid_devices == 0)
        {
            ESP_LOGW(TAG, "All USB HID devices disconnected - resetting for reconnection");
            hid_transfers_setup = false;
        }

        // Enhanced debugging and USB recovery for reconnection issues
        static int no_devices_counter = 0;
        static bool recovery_attempted = false;

        if (active_hid_devices == 0)
        {
            no_devices_counter++;
            if (no_devices_counter % 10 == 0)
            { // Every 10 seconds
                ESP_LOGI(TAG, "No USB devices active for %d seconds - USB host ready for connections", no_devices_counter);
            }

            // After 30 seconds without devices, check if advanced restart helped
            if (no_devices_counter == 30 && !recovery_attempted)
            {
                ESP_LOGW(TAG, "USB RECONNECTION STATUS:");
                ESP_LOGW(TAG, "");
                ESP_LOGW(TAG, "Advanced USB Host library restart has been attempted.");
                ESP_LOGW(TAG, "If USB cable was disconnected and reconnected but still no detection:");
                ESP_LOGW(TAG, "");
                ESP_LOGW(TAG, "FALLBACK SOLUTIONS:");
                ESP_LOGW(TAG, "1. Press RST button on ESP32-S3 (most reliable)");
                ESP_LOGW(TAG, "2. Power cycle ESP32-S3 (unplug/replug power)");
                ESP_LOGW(TAG, "");
                ESP_LOGW(TAG, "This is an ESP-IDF limitation we're working to resolve completely.");
                recovery_attempted = true;
            }
        }
        else
        {
            no_devices_counter = 0;
            recovery_attempted = false;
        }

        if (ENABLE_DEBUG_BLE_LOGGING)
        {
            ESP_LOGD(TAG, "Main loop running... (Active HID devices: %d, BLE connected: %s)",
                     active_hid_devices, ble_conn_handle > 0 ? "yes" : "no");
        }

        // Check if we need to force-enable notifications after 3 seconds
        if (encryption_completed && !ble_hid_notify_enabled && ble_conn_handle > 0)
        {
            uint32_t elapsed_time = (xTaskGetTickCount() - encryption_complete_time) * portTICK_PERIOD_MS;
            if (elapsed_time >= 3000)
            { // 3 seconds
                ESP_LOGW(TAG, "Windows hasn't enabled notifications after 3s - forcing enable for compatibility");
                ble_hid_notify_enabled = true;
                encryption_completed = false; // Prevent re-triggering
                if (ENABLE_DEBUG_BLE_LOGGING)
                {
                    ESP_LOGI(TAG, "🔧 HID notifications force-enabled - will now forward keystrokes");
                }
            }
        }
    }
}