
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
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
#define LAI_IMMEDIATE_WORD_MODE 5

// Allocate a new package.
int lai_create_pkg(lai_object_t *, size_t);

void lai_load_ns(lai_nsnode_t *, lai_object_t *);
void lai_store_ns(lai_nsnode_t *, lai_object_t *);

void lai_alias_operand(lai_state_t *, lai_object_t *, lai_object_t *);
void lai_load_operand(lai_state_t *, lai_object_t *, lai_object_t *);
void lai_store_operand(lai_state_t *, lai_object_t *, lai_object_t *);
void lai_eval_operand(lai_object_t *, lai_state_t *, uint8_t *);

void lai_free_object(lai_object_t *);
void lai_move_object(lai_object_t *, lai_object_t *);
void lai_copy_object(lai_object_t *, lai_object_t *);

void lai_exec_sleep(void *, lai_state_t *);

lai_nsnode_t *lai_exec_resolve(char *);
