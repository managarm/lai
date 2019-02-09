
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 by Omar Muhamed
 */

/* ACPI Namespace Management During Control Method Execution */

#include "lai.h"
#include "ns_impl.h"

// acpi_exec_name(): Creates a Name() object in a Method's private namespace
// Param:    void *data - data
// Param:    acpi_state_t *state - AML VM state
// Return:    size_t - size in bytes for skipping

void acpi_exec_name(void *data, acpi_nsnode_t *parent, acpi_state_t *state)
{
    state->pc++; // Skip over NAME_OP.
    uint8_t *code = data;

    char path[ACPI_MAX_NAME];
    state->pc += acpins_resolve_path(parent, path, code + state->pc);

    acpi_nsnode_t *handle;
    handle = acpins_resolve(path);
    if(!handle)
    {
        // Create it if it does not already exist.
        handle = acpins_create_nsnode_or_die();
        handle->type = ACPI_NAMESPACE_NAME;
        acpi_strcpy(handle->path, path);
        acpins_install_nsnode(handle);
    }

    acpi_eval_operand(&handle->object, state, code);
}

// acpi_exec_bytefield(): Creates a ByteField object
// Param:    void *data - data
// Param:    acpi_state_t *state - AML VM state

void acpi_exec_bytefield(void *data, acpi_nsnode_t *parent, acpi_state_t *state)
{
    uint8_t *code = data;
    state->pc += acpins_create_bytefield(parent, code + state->pc);    // dirty af solution but good enough for now
}


// acpi_exec_wordfield(): Creates a WordField object
// Param:    void *data - data
// Param:    acpi_state_t *state - AML VM state

void acpi_exec_wordfield(void *data, acpi_nsnode_t *parent, acpi_state_t *state)
{
    uint8_t *code = data;
    state->pc += acpins_create_wordfield(parent, code + state->pc);    // dirty af solution but good enough for now
}


// acpi_exec_dwordfield(): Creates a DwordField object
// Param:    void *data - data
// Param:    acpi_state_t *state - AML VM state

void acpi_exec_dwordfield(void *data, acpi_nsnode_t *parent, acpi_state_t *state)
{
    uint8_t *code = data;
    state->pc += acpins_create_dwordfield(parent, code + state->pc);    // dirty af solution but good enough for now
}

