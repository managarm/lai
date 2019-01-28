
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018 by Omar Mohammad
 */

/* Windozz-specific OS functions */

#pragma once

#define MODULE				"acpi"

#include <stddef.h>
#include <stdint.h>
#include <debug.h>
#include <mutex.h>

#define acpi_printf			DEBUG

#define acpi_panic(...)		ERROR(__VA_ARGS__); \
							while(1);

typedef mutex_t acpi_lock_t;
