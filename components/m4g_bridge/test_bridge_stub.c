// Simple test stub (non-IDF test harness) demonstrating duplicate suppression logic
// This file is optional and can be excluded from normal build by adding to CMakeLists with if(CONFIG_IDF_TARGET)... conditions.

#include "m4g_bridge.h"
#include <stdio.h>
#include <string.h>

// Weak symbols for BLE send so we can simulate acceptance
__attribute__((weak)) bool m4g_ble_send_keyboard_report(const uint8_t report[8])
{
  (void)report;
  return true;
}
__attribute__((weak)) bool m4g_ble_send_mouse_report(const uint8_t report[3])
{
  (void)report;
  return true;
}

int main(void)
{
  m4g_bridge_init();
  uint8_t rep1[] = {0x01, 0x00, 0x00, 0x04}; // report id 1, 'a'
  uint8_t rep2[] = {0x01, 0x00, 0x00, 0x04}; // duplicate
  uint8_t rep3[] = {0x01, 0x02, 0x00, 0x04}; // modifier change
  m4g_bridge_process_usb_report(rep1, sizeof(rep1));
  m4g_bridge_process_usb_report(rep2, sizeof(rep2)); // should suppress
  m4g_bridge_process_usb_report(rep3, sizeof(rep3)); // should send
  m4g_bridge_stats_t stats;
  m4g_bridge_get_stats(&stats);
  printf("Keyboard reports sent: %lu\n", (unsigned long)stats.keyboard_reports_sent);
  return 0;
}
