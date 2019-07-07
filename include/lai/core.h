/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 by Omar Muhamed
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <acpispec/resources.h>
#include <acpispec/tables.h>
#include <lai/host.h>

// Even in freestanding environments, GCC requires memcpy(), memmove(), memset()
// and memcmp() to be present. Thus, we just use them directly.
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);

void lai_debug(const char *, ...);
void lai_warn(const char *, ...);
__attribute__((noreturn)) void lai_panic(const char *, ...);

#define LAI_STRINGIFY(x) #x
#define LAI_EXPAND_STRINGIFY(x) LAI_STRINGIFY(x)

#define LAI_ENSURE(cond) \
    do { \
        if(!(cond)) \
            lai_panic("assertion failed: " #cond " at " \
                       __FILE__ ":" LAI_EXPAND_STRINGIFY(__LINE__) "\n"); \
    } while(0)

__attribute__((always_inline))
inline void lai_namecpy(char *dest, const char *src) {
    memcpy(dest, src, 4);
}

#define ACPI_MAX_NAME               64
#define ACPI_MAX_RESOURCES          512

typedef enum lai_api_error {
    LAI_ERROR_NONE,
    LAI_ERROR_TYPE_MISMATCH,
} lai_api_error_t;

#define LAI_NAMESPACE_ROOT          1
#define LAI_NAMESPACE_NAME          2
#define LAI_NAMESPACE_ALIAS         3
#define LAI_NAMESPACE_FIELD         4
#define LAI_NAMESPACE_METHOD        5
#define LAI_NAMESPACE_DEVICE        6
#define LAI_NAMESPACE_INDEXFIELD    7
#define LAI_NAMESPACE_MUTEX         8
#define LAI_NAMESPACE_PROCESSOR     9
#define LAI_NAMESPACE_BUFFER_FIELD  10
#define LAI_NAMESPACE_THERMALZONE   11
#define LAI_NAMESPACE_EVENT         12
#define LAI_NAMESPACE_POWER_RES     13

// ----------------------------------------------------------------------------
// Types for lai_object_t.
// ----------------------------------------------------------------------------
// Value types: integer, string, buffer, package.
#define LAI_INTEGER            1
#define LAI_STRING             2
#define LAI_BUFFER             3
#define LAI_PACKAGE            4
// Handle type: this is used to represent device (and other) namespace nodes.
#define LAI_HANDLE             5
#define LAI_LAZY_HANDLE        6
// Reference types: obtained from RefOp() or Index().
#define LAI_STRING_INDEX       7
#define LAI_BUFFER_INDEX       8
#define LAI_PACKAGE_INDEX      9
// ----------------------------------------------------------------------------
// Internal data types of the interpreter.
// ----------------------------------------------------------------------------
// Name types: unresolved names and names of certain objects.
#define LAI_NULL_NAME         10
#define LAI_UNRESOLVED_NAME   11
#define LAI_RESOLVED_NAME     12
#define LAI_ARG_NAME          13
#define LAI_LOCAL_NAME        14
#define LAI_DEBUG_NAME        15

typedef int lai_rc_t;

__attribute__((always_inline))
inline void lai_rc_ref(lai_rc_t *rc_ptr) {
    lai_rc_t nrefs = (*rc_ptr)++;
    LAI_ENSURE(nrefs > 0);
}

__attribute__((always_inline))
inline int lai_rc_unref(lai_rc_t *rc_ptr) {
    lai_rc_t nrefs = --(*rc_ptr);
    LAI_ENSURE(nrefs >= 0);
    return !nrefs;
}

struct lai_aml_segment {
    acpi_aml_t *table;
    // Index of the table (e.g., for SSDTs).
    size_t index;
};

typedef struct lai_object_t
{
    int type;
    uint64_t integer;        // for Name()

    union {
        struct lai_string_head *string_ptr;
        struct lai_buffer_head *buffer_ptr;
        struct lai_pkg_head *pkg_ptr;
        struct {
            struct lai_nsnode_t *unres_ctx_handle;
            const uint8_t *unres_aml;
        };
    };

    struct lai_nsnode_t *handle;

    int index;
} lai_object_t;

struct lai_string_head {
    lai_rc_t rc;
    char content[];
};

struct lai_buffer_head {
    lai_rc_t rc;
    size_t size;
    uint8_t content[];
};

struct lai_pkg_head {
    lai_rc_t rc;
    unsigned int size;
    struct lai_object_t elems[];
};

// Allows access to the contents of a string.
__attribute__((always_inline))
inline char *lai_exec_string_access(lai_object_t *str) {
    LAI_ENSURE(str->type == LAI_STRING);
    return str->string_ptr->content;
}

// Returns the size of a string.
size_t lai_exec_string_length(lai_object_t *str);

// Returns the size of a buffer.
__attribute__((always_inline))
inline size_t lai_exec_buffer_size(lai_object_t *buffer) {
    LAI_ENSURE(buffer->type == LAI_BUFFER);
    return buffer->buffer_ptr->size;
}

// Allows access to the contents of a buffer.
__attribute__((always_inline))
inline void *lai_exec_buffer_access(lai_object_t *buffer) {
    LAI_ENSURE(buffer->type == LAI_BUFFER);
    return buffer->buffer_ptr->content;
}

// Returns the size of a package.
__attribute__((always_inline))
inline size_t lai_exec_pkg_size(lai_object_t *object) {
    // TODO: Ensure that this is a package.
    return object->pkg_ptr->size;
}

// Helper functions for lai_exec_pkg_load()/lai_exec_pkg_store(), for internal interpreter use.
void lai_exec_pkg_var_load(lai_object_t *out, struct lai_pkg_head *head, size_t i);
void lai_exec_pkg_var_store(lai_object_t *in, struct lai_pkg_head *head, size_t i);

// Load/store values from/to packages.
__attribute__((always_inline))
inline void lai_exec_pkg_load(lai_object_t *out, lai_object_t *pkg, size_t i) {
    LAI_ENSURE(pkg->type == LAI_PACKAGE);
    return lai_exec_pkg_var_load(out, pkg->pkg_ptr, i);
}
__attribute__((always_inline))
inline void lai_exec_pkg_store(lai_object_t *in, lai_object_t *pkg, size_t i) {
    LAI_ENSURE(pkg->type == LAI_PACKAGE);
    return lai_exec_pkg_var_store(in, pkg->pkg_ptr, i);
}

typedef struct lai_nsnode_t
{
    char name[4];
    char fullpath[ACPI_MAX_NAME];    // full path of object
    int type;
    struct lai_nsnode_t *parent;
    struct lai_aml_segment *amls;
    void *pointer;            // valid for scopes, methods, etc.
    size_t size;            // valid for scopes, methods, etc.

    lai_object_t object;        // for Name()

    uint8_t op_address_space;    // for OpRegions only
    uint64_t op_base;        // for OpRegions only
    uint64_t op_length;        // for OpRegions only

    uint8_t method_flags;        // for Methods only, includes ARG_COUNT in lowest three bits
    // Allows the OS to override methods. Mainly useful for _OSI, _OS and _REV.
    int (*method_override)(lai_object_t *args, lai_object_t *result);

    // TODO: Find a good mechanism for locks.
    //lai_lock_t mutex;        // for Mutex

    uint8_t cpu_id;            // for Processor

    union {
        struct lai_nsnode_t *al_target; // LAI_NAMESPACE_ALIAS.

        struct { // LAI_NAMESPACE_FIELD.
            struct lai_nsnode_t *fld_region_node;
            uint64_t fld_offset; // In bits.
            size_t fld_size;     // In bits.
            uint8_t fld_flags;
        };
        struct { // LAI_NAMESPACE_INDEX_FIELD.
            uint64_t idxf_offset; // In bits.
            struct lai_nsnode_t *idxf_index_node;
            struct lai_nsnode_t *idxf_data_node;
            uint8_t idxf_flags;
            uint8_t idxf_size;
        };
        struct { // LAI_NAMESPACE_BUFFER_FIELD.
            struct lai_nsnode_t *bf_node;
            uint64_t bf_offset; // In bits.
            uint64_t bf_size;   // In bits.
        };
    };
} lai_nsnode_t;

#define LAI_POPULATE_CONTEXT_STACKITEM 1
#define LAI_METHOD_CONTEXT_STACKITEM   2
#define LAI_LOOP_STACKITEM             3
#define LAI_COND_STACKITEM             4
#define LAI_PKG_INITIALIZER_STACKITEM  5
#define LAI_NODE_STACKITEM             6 // Parse a namespace leaf node (i.e., not a scope).
#define LAI_OP_STACKITEM               7 // Parse an operator.
// This implements lai_eval_operand(). // TODO: Eventually remove
// lai_eval_operand() by moving all parsing functionality into lai_exec_run().
#define LAI_EVALOPERAND_STACKITEM 10

typedef struct lai_stackitem_ {
    int kind;
    int opstack_frame;
    union {
        struct {
            lai_nsnode_t *ctx_handle;
            int ctx_limit;
        };
        struct {
            int loop_pred; // Loop predicate PC.
            int loop_end; // End of loop PC.
        };
        struct {
            int cond_taken; // Whether the conditional was true or not.
            int cond_end; // End of conditional PC.
        };
        struct {
            int pkg_index;
            int pkg_end;
            uint8_t pkg_result_mode;
        };
        struct {
            int op_opcode;
            uint8_t op_arg_modes[8];
            uint8_t op_result_mode;
        };
        struct {
            int node_opcode;
            uint8_t node_arg_modes[8];
        };
    };
} lai_stackitem_t;

typedef struct lai_state_t
{
    int pc;
    int limit;
    lai_object_t retvalue;
    lai_object_t arg[7];
    lai_object_t local[8];

    // Stack to track the current execution state.
    int stack_ptr;
    int opstack_ptr;
    lai_stackitem_t stack[16];
    lai_object_t opstack[16];
    int context_ptr; // Index of the last CONTEXT_STACKITEM.
} lai_state_t;

void lai_init_state(lai_state_t *);
void lai_finalize_state(lai_state_t *);

__attribute__((always_inline))
inline lai_object_t *lai_retvalue(lai_state_t *state) {
    return &state->retvalue;
}

__attribute__((always_inline))
inline lai_object_t *lai_arg(lai_state_t *state, int n) {
    return &state->arg[n];
}

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
lai_nsnode_t *lai_get_deviceid(size_t, lai_object_t *);
lai_nsnode_t *lai_enum(char *, size_t);
void lai_eisaid(lai_object_t *, char *);
size_t lai_read_resource(lai_nsnode_t *, acpi_resource_t *);

// Access and manipulation of lai_object_t.

enum lai_object_type {
    LAI_TYPE_NONE,
    LAI_TYPE_INTEGER,
    LAI_TYPE_STRING,
    LAI_TYPE_BUFFER,
    LAI_TYPE_PACKAGE,
    LAI_TYPE_DEVICE,
};

enum lai_object_type lai_obj_get_type(lai_object_t *object);
lai_api_error_t lai_obj_get_integer(lai_object_t *, uint64_t *);
lai_api_error_t lai_obj_get_handle(lai_object_t *, lai_nsnode_t **);

// ACPI Control Methods
int lai_populate(lai_nsnode_t *, struct lai_aml_segment *, lai_state_t *);
int lai_exec_method(lai_nsnode_t *, lai_state_t *);
int lai_eval_node(lai_nsnode_t *, lai_state_t *);
int lai_eval(lai_object_t *, lai_nsnode_t *);

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

