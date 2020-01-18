/*
 * Lightweight ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
 */

#pragma once

#include <lai/core.h>

#ifdef __cplusplus
extern "C" {
#endif

void lai_start_pm_timer();
void lai_stop_pm_timer();
void lai_busy_wait_pm_timer(uint64_t);

#ifdef __cplusplus
}
#endif