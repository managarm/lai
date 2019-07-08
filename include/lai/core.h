/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 by Omar Muhamed
 */

#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <acpispec/resources.h>
#include <acpispec/tables.h>
#include <lai/host.h>
#include <lai/internal-exec.h>
#include <lai/internal-ns.h>
#include <lai/internal-util.h>

#define ACPI_MAX_RESOURCES          512

typedef enum lai_api_error {
    LAI_ERROR_NONE,
    LAI_ERROR_TYPE_MISMATCH,
} lai_api_error_t;

void lai_init_state(lai_state_t *);
void lai_finalize_state(lai_state_t *);

#define LAI_CLEANUP_STATE __attribute__((cleanup(lai_finalize_state)))

struct lai_ns_iterator {
    size_t i;
};

#define LAI_NS_ITERATOR_INIT {0}

__attribute__((always_inline))
inline lai_variable_t *lai_retvalue(lai_state_t *state) {
    return &state->retvalue;
}

// TODO: The following functions are not API functions.
extern acpi_fadt_t *lai_fadt;
extern size_t lai_ns_size;
extern volatile uint16_t lai_last_event;

// The remaining of these functions are OS independent!
// ACPI namespace functions
lai_nsnode_t *lai_create_root(void);
void lai_create_namespace(void);
char *lai_stringify_node_path(lai_nsnode_t *);
lai_nsnode_t *lai_resolve_path(lai_nsnode_t *, const char *);
lai_nsnode_t *lai_resolve_search(lai_nsnode_t *, const char *);
lai_nsnode_t *lai_legacy_resolve(char *);
lai_nsnode_t *lai_get_device(size_t);
int lai_check_device_pnp_id(lai_nsnode_t *, lai_variable_t *, lai_state_t *);
lai_nsnode_t *lai_enum(char *, size_t);
void lai_eisaid(lai_variable_t *, char *);
size_t lai_read_resource(lai_nsnode_t *, acpi_resource_t *);
lai_nsnode_t *lai_ns_iterate(struct lai_ns_iterator *);

// Access and manipulation of lai_variable_t.

enum lai_object_type {
    LAI_TYPE_NONE,
    LAI_TYPE_INTEGER,
    LAI_TYPE_STRING,
    LAI_TYPE_BUFFER,
    LAI_TYPE_PACKAGE,
    LAI_TYPE_DEVICE,
};

enum lai_object_type lai_obj_get_type(lai_variable_t *object);
lai_api_error_t lai_obj_get_integer(lai_variable_t *, uint64_t *);
lai_api_error_t lai_obj_get_handle(lai_variable_t *, lai_nsnode_t **);

// Evaluation of namespace nodes (including control methods).

int lai_eval_args(lai_variable_t *, lai_nsnode_t *, lai_state_t *, int, lai_variable_t *);
int lai_eval_largs(lai_variable_t *, lai_nsnode_t *, lai_state_t *, ...);
int lai_eval_vargs(lai_variable_t *, lai_nsnode_t *, lai_state_t *, va_list);
int lai_eval(lai_variable_t *, lai_nsnode_t *, lai_state_t *);

// ACPI Control Methods
int lai_populate(lai_nsnode_t *, struct lai_aml_segment *, lai_state_t *);

// Generic Functions
int lai_enable_acpi(uint32_t);
int lai_disable_acpi();
uint16_t lai_get_sci_event(void);
void lai_set_sci_event(uint16_t);
int lai_enter_sleep(uint8_t);
int lai_pci_route(acpi_resource_t *, uint8_t, uint8_t, uint8_t);

// LAI debugging functions.

// Trace all opcodes. This will produce *very* verbose output.
void lai_enable_tracing(int enable);

