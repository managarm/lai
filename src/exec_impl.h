/*
 * Lux ACPI Implementation
 * Copyright (C) 2019 by LAI contributors
 */

// Internal header file. Do not use outside of LAI.

#pragma once

#include <lai/core.h>

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

void lai_load_ns(lai_nsnode_t *, lai_object_t *);
void lai_store_ns(lai_nsnode_t *, lai_object_t *);

void lai_alias_operand(lai_state_t *, lai_object_t *, lai_object_t *);

void lai_load_operand(lai_state_t *, lai_object_t *, lai_object_t *);
void lai_store_operand(lai_state_t *, lai_object_t *, lai_object_t *);

