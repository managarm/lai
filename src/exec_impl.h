/*
 * Lux ACPI Implementation
 * Copyright (C) 2019 by LAI contributors
 */

// Internal header file. Do not use outside of LAI.

#pragma once

#include <lai.h>

// Evaluate constant data (and keep result).
//     Primitive objects are parsed.
//     Names are left unresolved.
//     Operations (e.g. Add()) are not allowed.
#define LAI_DATA_MODE 1
// Evaluate dynamic data (and keep result).
//     Primitive objects are parsed.
//     Names are resolved. Methods are executed.
//     Operations are allowed and executed.
#define LAI_OBJECT_MODE 2
// Like LAI_OBJECT_MODE, but discard the result.
#define LAI_EXEC_MODE 3
#define LAI_TARGET_MODE 4

void acpi_load_ns(acpi_nsnode_t *, acpi_object_t *);
void acpi_store_ns(acpi_nsnode_t *, acpi_object_t *);

void acpi_alias_operand(acpi_state_t *, acpi_object_t *, acpi_object_t *);

void acpi_load_operand(acpi_state_t *, acpi_object_t *, acpi_object_t *);
void acpi_store_operand(acpi_state_t *, acpi_object_t *, acpi_object_t *);

