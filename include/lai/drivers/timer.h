/*
 * Lightweight ACPI Implementation
 * Copyright (C) 2018-2021 the lai authors
 */

#pragma once

#include <lai/core.h>

#ifdef __cplusplus
extern "C" {
#endif

lai_api_error_t lai_start_pm_timer();
lai_api_error_t lai_stop_pm_timer();
lai_api_error_t lai_busy_wait_pm_timer(uint64_t);

#ifdef __cplusplus
}
#endif
