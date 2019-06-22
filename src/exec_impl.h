
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

struct lai_name_data {
    char path[ACPI_MAX_NAME];
};

// Allocate a new package.
int lai_create_string(lai_object_t *, size_t);
int lai_create_c_string(lai_object_t *, const char *);
int lai_create_buffer(lai_object_t *, size_t);
int lai_create_pkg(lai_object_t *, size_t);

void lai_load(lai_state_t *, lai_object_t *, lai_object_t *);
void lai_store(lai_state_t *, lai_object_t *, lai_object_t *);

void lai_get_objectref(lai_state_t *, lai_object_t *, lai_object_t *);
void lai_get_objectref_clone(lai_state_t *, lai_object_t *, lai_object_t *);
void lai_get_integer(lai_state_t *, lai_object_t *, lai_object_t *);

void lai_free_object(lai_object_t *);
void lai_move_object(lai_object_t *, lai_object_t *);
void lai_clone_object(lai_object_t *, lai_object_t *);

void lai_exec_sleep(struct lai_aml_segment *, void *, lai_state_t *);

lai_nsnode_t *lai_exec_resolve(char *);
