# nRF52840 + MAX3421E Integration Plan (Saved Reference)

Status: Draft (no code merged yet)

Last Updated: 2025-09-26

## Objective

Add an alternate platform target using an nRF52840 (Zephyr RTOS + Zephyr BLE stack) paired with an external MAX3421E USB host controller, while preserving the existing ESP32-S3 native USB host build.

## Strategy Summary

1. Introduce a platform abstraction layer (PAL) so core translation/bridge logic is platform-neutral.
2. Factor existing logic into `bridge_core` (descriptor parsing, translation, duplicate suppression, stats) and `platform/<target>` backends.
3. Provide two backends initially: `esp32s3` (current) and `nrf52840` (new).
4. Implement a minimal custom HID-only host stack for MAX3421E (no full generic USB host) with optional later hub support.
5. Stage development: first BLE-only on nRF (no USB) to validate PAL, then USB keyboard (single device), then optional hub + multi-device support.

## Directory Layout (Proposed)

```text
bridge_core/
    m4g_bridge.c/.h
    m4g_descriptor.c/.h
    m4g_stats.c/.h
    m4g_logging_portable.c
platform/esp32s3/
    platform_impl.c
    ble_impl.c
    usb_impl.c
    led_impl.c
platform/nrf52840/
    platform_impl.c
    ble_impl.c
    max3421e_spi.c
    usb_host_hid.c
    led_impl.c
include/
    m4g_platform.h
    m4g_bridge.h
    m4g_config_shared.h
boards/
    esp32s3_devkit.conf
    nrf52840_dk.conf
```

## Platform Abstraction (Initial API Draft)

```c
// m4g_platform.h (sketch)
void m4g_platform_init(void);
uint32_t m4g_millis(void);

bool m4g_ble_is_connected(void);
int  m4g_ble_send_input_report(const uint8_t *data, size_t len);

void m4g_usb_init(void);

void m4g_usb_poll(void);              // For MAX3421E polling backend

void m4g_led_set_status(enum m4g_led_state s);

void m4g_power_idle_opportunity(void); // Called periodically to enter low-power if possible

void m4g_log(int level, const char *fmt, ...); // Unified logging front
```

## New Kconfig Additions

```kconfig
choice M4G_TARGET
    prompt "Target Platform"
    default M4G_TARGET_ESP32S3
config M4G_TARGET_ESP32S3
    bool "ESP32-S3 (native host)"
config M4G_TARGET_NRF52840
    bool "nRF52840 + MAX3421E"
endchoice

config M4G_MAX_DEVICES
    int "Max simultaneous HID devices"          
    range 1 4
    default 2
    depends on M4G_TARGET_NRF52840

config M4G_MAX3421E_SPI_FREQ
    int "MAX3421E SPI frequency (Hz)"
    default 8000000
    depends on M4G_TARGET_NRF52840

config M4G_MAX3421E_INT_GPIO
    int "MAX3421E INT GPIO"
    depends on M4G_TARGET_NRF52840

config M4G_MAX3421E_CS_GPIO
    int "MAX3421E CS GPIO"
    depends on M4G_TARGET_NRF52840

config M4G_MAX3421E_VBUS_EN_GPIO
    int "VBUS enable GPIO (-1 if none)"
    default -1
    depends on M4G_TARGET_NRF52840
```

## Development Phases

| Phase | Goal | Key Deliverables |
|-------|------|------------------|
| 0 | Preparation | Extract PAL header, move neutral code, keep ESP build green |
| 1 | nRF BLE Skeleton | Zephyr app: BLE HID service + logging + LED stub |
| 2 | MAX3421E Low-Level | SPI driver, reset/PLL init, IRQ handling |
| 3 | Single HID Device | Minimal enumeration (device descriptor → config → HID) + report IN transfers |
| 4 | Full Bridge Integration | Feed reports through existing translation + BLE notify |
| 5 | Optional Hub (Lean) | One-level hub enumeration, multiple HID devices (cap by M4G_MAX_DEVICES) |
| 6 | Power Optimization | Idle timing, selective polling, low-power entry hooks |

## MAX3421E HID Host Minimal Flow

1. Power up + reset MAX3421E.
2. Wait PLL lock, configure host mode & SOF generation.
3. Detect device connect (VBUS session + J/K state).
4. Issue port reset, assign address.
5. Fetch descriptors (device, configuration, parse interfaces).
6. Identify HID keyboard/mouse interface (boot or report protocol).
7. Set protocol / idle (optional).
8. Begin polling interrupt IN endpoint at bInterval.
9. Forward packets to bridge → BLE.

## Hub (Optional Lean Implementation)

Constraints: single hub depth, HID devices only, limited endpoint count.

Components:

- Hub descriptor fetch
- Hub descriptor fetch
- Port power (if required)
- Connection change bitmap polling or interrupt endpoint
- Port reset & speed detect
- Device table & address allocator
- Round-robin interrupt IN scheduling (respect bInterval)

## Risk & Mitigation

| Risk | Mitigation |
|------|------------|
| MAX3421E timing quirks | Start with conservative delays, add configurable timeouts |
| Zephyr & ESP divergence | Keep PAL thin; forbid core code from including platform headers |
| HID composite edge cases | Support first matching interface only initially |
| Hub complexity creep | Kconfig gate HUB support; assert if unsupported device class appears |

## Testing Strategy

- Host-side unit tests for translation & duplicate suppression (native build).
- ESP regression build every phase.
- Zephyr test: BLE only (Phase 1) → add USB incrementally.
- Loopback test harness for MAX3421E register access (mock SPI) in PC build (optional).
- Stress test: rapid connect/disconnect sequences.

## Stretch Goals / Later

- Multi-keyboard merge (OR key states) instead of single primary.
- Dynamic runtime config over BLE GATT (enable/disable arrow→mouse, etc.).
- Latency metrics characteristic.
- Power domain control / deep sleep for nRF when idle.

## Decision Log (to fill as development proceeds)

| Date | Decision | Rationale |
|------|----------|-----------|
| (pending) | Use custom minimal HID host instead of porting full stack | Smaller footprint, faster delivery |

---

This document is a frozen baseline; update it as architectural decisions are made.
