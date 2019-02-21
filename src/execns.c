
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 by Omar Muhamed
 */

/* ACPI Namespace Management During Control Method Execution */

#include <lai/core.h>
#include "ns_impl.h"

// lai_exec_name(): Creates a Name() object in a Method's private namespace
// Param:    void *data - data
// Param:    lai_state_t *state - AML VM state
// Return:    size_t - size in bytes for skipping

void lai_exec_name(void *data, lai_nsnode_t *parent, lai_state_t *state)
{
    state->pc++; // Skip over NAME_OP.
    uint8_t *code = data;

    char path[ACPI_MAX_NAME];
    state->pc += acpins_resolve_path(parent, path, code + state->pc);

    lai_nsnode_t *handle;
    handle = acpins_resolve(path);
    if(!handle)
    {
        // Create it if it does not already exist.
        handle = acpins_create_nsnode_or_die();
        handle->type = LAI_NAMESPACE_NAME;
        lai_strcpy(handle->path, path);
        acpins_install_nsnode(handle);
    }

    lai_eval_operand(&handle->object, state, code);
}

// lai_exec_bytefield(): Creates a ByteField object
// Param:    void *data - data
// Param:    lai_state_t *state - AML VM state

void lai_exec_bytefield(void *data, lai_nsnode_t *parent, lai_state_t *state)
{
    uint8_t *code = data;
    state->pc += acpins_create_bytefield(parent, code + state->pc);    // dirty af solution but good enough for now
}


// lai_exec_wordfield(): Creates a WordField object
// Param:    void *data - data
// Param:    lai_state_t *state - AML VM state

void lai_exec_wordfield(void *data, lai_nsnode_t *parent, lai_state_t *state)
{
    uint8_t *code = data;
    state->pc += acpins_create_wordfield(parent, code + state->pc);    // dirty af solution but good enough for now
}


// lai_exec_dwordfield(): Creates a DwordField object
// Param:    void *data - data
// Param:    lai_state_t *state - AML VM state

void lai_exec_dwordfield(void *data, lai_nsnode_t *parent, lai_state_t *state)
{
    uint8_t *code = data;
    state->pc += acpins_create_dwordfield(parent, code + state->pc);    // dirty af solution but good enough for now
}

