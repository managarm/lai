
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 by Omar Muhamed
 */

/* Windozz-specific OS functions */

#pragma once

#define MODULE				"acpi"

#include <stddef.h>
#include <stdint.h>
#include <debug.h>
#include <mutex.h>

#define acpi_debug			DEBUG
#define acpi_warn			WARN

#define acpi_panic(...)		ERROR(__VA_ARGS__); \
							while(1);

typedef mutex_t acpi_lock_t;
