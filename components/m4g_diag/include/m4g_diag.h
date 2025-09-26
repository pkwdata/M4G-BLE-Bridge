#pragma once
#include "esp_err.h"

// Run one-time startup diagnostics (sanity checks, logging environment summary)
esp_err_t m4g_diag_run_startup_checks(void);

// Optional periodic diagnostic status task (creates its own FreeRTOS task if enabled)
void m4g_diag_start_periodic_task(void);
