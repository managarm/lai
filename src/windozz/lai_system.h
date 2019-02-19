
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 by Omar Muhamed
 */

/* Windozz-specific OS functions */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <debug.h>
#include <mutex.h>

#define lai_debug(...)            debug_printf(LEVEL_DEBUG, "acpi", __VA_ARGS__)
#define lai_warn(...)            debug_printf(LEVEL_WARN, "acpi", __VA_ARGS__)

#define lai_panic(...) \
    do { \
        debug_printf(LEVEL_ERROR, "acpi", __VA_ARGS__); \
        while(1) \
            ; \
    } while(0)

typedef mutex_t lai_lock_t;
