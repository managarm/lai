/*
 * Lightweight ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
 */

#pragma once

#include <lai/internal-util.h>

// ----------------------------------------------------------------------------
// struct lai_variable.
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

typedef struct lai_variable_t
{
    int type;
    uint64_t integer;        // for Name()

    union {
        struct lai_string_head *string_ptr;
        struct lai_buffer_head *buffer_ptr;
        struct lai_pkg_head *pkg_ptr;
        struct {
            struct lai_nsnode *unres_ctx_handle;
            const uint8_t *unres_aml;
        };
    };

    struct lai_nsnode *handle;

    int index;
} lai_variable_t;

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
    struct lai_variable_t elems[];
};

// Allows access to the contents of a string.
__attribute__((always_inline))
inline char *lai_exec_string_access(lai_variable_t *str) {
    LAI_ENSURE(str->type == LAI_STRING);
    return str->string_ptr->content;
}

// Returns the size of a string.
size_t lai_exec_string_length(lai_variable_t *str);

// Returns the size of a buffer.
__attribute__((always_inline))
inline size_t lai_exec_buffer_size(lai_variable_t *buffer) {
    LAI_ENSURE(buffer->type == LAI_BUFFER);
    return buffer->buffer_ptr->size;
}

// Allows access to the contents of a buffer.
__attribute__((always_inline))
inline void *lai_exec_buffer_access(lai_variable_t *buffer) {
    LAI_ENSURE(buffer->type == LAI_BUFFER);
    return buffer->buffer_ptr->content;
}

// Returns the size of a package.
__attribute__((always_inline))
inline size_t lai_exec_pkg_size(lai_variable_t *object) {
    // TODO: Ensure that this is a package.
    return object->pkg_ptr->size;
}

// Helper functions for lai_exec_pkg_load()/lai_exec_pkg_store(), for internal interpreter use.
void lai_exec_pkg_var_load(lai_variable_t *out, struct lai_pkg_head *head, size_t i);
void lai_exec_pkg_var_store(lai_variable_t *in, struct lai_pkg_head *head, size_t i);

// Load/store values from/to packages.
__attribute__((always_inline))
inline void lai_exec_pkg_load(lai_variable_t *out, lai_variable_t *pkg, size_t i) {
    LAI_ENSURE(pkg->type == LAI_PACKAGE);
    return lai_exec_pkg_var_load(out, pkg->pkg_ptr, i);
}
__attribute__((always_inline))
inline void lai_exec_pkg_store(lai_variable_t *in, lai_variable_t *pkg, size_t i) {
    LAI_ENSURE(pkg->type == LAI_PACKAGE);
    return lai_exec_pkg_var_store(in, pkg->pkg_ptr, i);
}

// ----------------------------------------------------------------------------
// struct lai_operand.
// ----------------------------------------------------------------------------

// Name types: unresolved names and names of certain objects.
#define LAI_OPERAND_OBJECT    1
#define LAI_NULL_NAME         2
#define LAI_UNRESOLVED_NAME   3
#define LAI_RESOLVED_NAME     4
#define LAI_ARG_NAME          5
#define LAI_LOCAL_NAME        6
#define LAI_DEBUG_NAME        7

// While struct lai_object can store all AML *objects*, this struct can store all expressions
// that occur as operands in AML. In particular, this includes objects and references to names.
struct lai_operand {
    int tag;
    union {
        lai_variable_t object;
        int index; // Index of ARGx and LOCALx.
        struct {
            struct lai_nsnode *unres_ctx_handle;
            const uint8_t *unres_aml;
        };
        struct lai_nsnode *handle;
    };
};

// ----------------------------------------------------------------------------
// struct lai_state.
// ----------------------------------------------------------------------------

#define LAI_POPULATE_CONTEXT_STACKITEM 1
#define LAI_METHOD_CONTEXT_STACKITEM   2
#define LAI_LOOP_STACKITEM             3
#define LAI_COND_STACKITEM             4
#define LAI_PKG_INITIALIZER_STACKITEM  5
#define LAI_NODE_STACKITEM             6 // Parse a namespace leaf node (i.e., not a scope).
#define LAI_OP_STACKITEM               7 // Parse an operator.
// This implements lai_eval_operand(). // TODO: Eventually remove
// lai_eval_operand() by moving all parsing functionality into lai_exec_run().
#define LAI_EVALOPERAND_STACKITEM     10

struct lai_invocation {
    lai_variable_t arg[7];
    lai_variable_t local[8];

	// Stores a list of all namespace nodes created by this method.
    struct lai_list per_method_list;
};

struct lai_ctxitem {
    struct lai_aml_segment *amls;
    uint8_t *code;
    struct lai_nsnode *handle; // Context handle for relative AML names.
    struct lai_invocation *invocation;
};

// We say that a stackitem is block-like if it is defined by a certain range of AML code.
// For each active block-like stackitem, we store a program counter (PC) and PC limit.
// Block-like stackitems are:
// - LAI_POPULATE_CONTEXT_STACKITEM
// - LAI_METHOD_CONTEXT_STACKITEM
// - LAI_LOOP_STACKITEM
// - LAI_COND_STACKITEM
// - LAI_PKG_INITIALIZER_STACKITEM

typedef struct lai_stackitem_ {
    int kind;
    // For block-like stackitems:
    int pc;
    int limit;
    int outer_block;
    // For stackitem accepting arguments.
    int opstack_frame;
    // Specific to each type of stackitem:
    union {
        struct {
            int loop_pred; // Loop predicate PC.
        };
        struct {
            int pkg_index;
            uint8_t pkg_want_result;
        };
        struct {
            int op_opcode;
            uint8_t op_arg_modes[8];
            uint8_t op_want_result;
        };
        struct {
            int node_opcode;
            uint8_t node_arg_modes[8];
        };
    };
} lai_stackitem_t;

typedef struct lai_state_t {
    int ctxstack_ptr; // Stack to track the current context.
    int stack_ptr;    // Stack to track the current execution state.
    int opstack_ptr;
    struct lai_ctxitem ctxstack[8];
    lai_stackitem_t stack[16];
    struct lai_operand opstack[16];
    int innermost_block; // Index of the last block-like stackitem.
} lai_state_t;

