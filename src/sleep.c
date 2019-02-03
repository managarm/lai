
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 by Omar Muhamed
 */

/* Sleeping Functions */
/* As of now, it's only for S5 (shutdown) sleep, because other sleeping states
 * need NVS and other things I still need to research */

#include "lai.h"

// acpi_enter_sleep(): Enters a sleeping state
// Param:    uint8_t state - 0-5 to correspond with states S0-S5
// Return:    int - 0 on success

int acpi_enter_sleep(uint8_t state)
{
    if(state > 5)
    {
        acpi_debug("undefined sleep state S%d\n", state);
        return 1;
    }

    uint8_t sleep_object[] = "_Sx_";
    sleep_object[2] = state + '0';

    // get sleeping package
    acpi_nsnode_t *handle = acpins_resolve((char*)sleep_object);
    if(!handle)
    {
        acpi_debug("sleep state S%d is not supported.\n", state);
        return 1;
    }

    acpi_object_t package, slp_typa, slp_typb;
    int eval_status;
    eval_status = acpi_eval(&package, handle->path);
    if(eval_status != 0)
    {
        acpi_debug("sleep state S%d is not supported.\n", state);
        return 1;
    }

    acpi_debug("entering sleep state S%d...\n", state);

    // ACPI spec says we should call _PTS() and _GTS() before actually sleeping
    // Who knows, it might do some required firmware-specific stuff
    acpi_state_t acpi_state;
    handle = acpins_resolve("_PTS");

    if(handle)
    {
        acpi_init_call_state(&acpi_state, handle);

        // pass the sleeping type as an argument
        acpi_arg(&acpi_state, 0)->type = ACPI_INTEGER;
        acpi_arg(&acpi_state, 0)->integer = (uint64_t)state & 0xFF;

        acpi_debug("execute _PTS(%d)\n", state);
        acpi_exec_method(&acpi_state);
        acpi_finalize_state(&acpi_state);
    }

    handle = acpins_resolve("_GTS");

    if(handle)
    {
        acpi_init_call_state(&acpi_state, handle);

        // pass the sleeping type as an argument
        acpi_arg(&acpi_state, 0)->type = ACPI_INTEGER;
        acpi_arg(&acpi_state, 0)->integer = (uint64_t)state & 0xFF;

        acpi_debug("execute _GTS(%d)\n", state);
        acpi_exec_method(&acpi_state);
        acpi_finalize_state(&acpi_state);
    }

    acpi_eval_package(&package, 0, &slp_typa);
    acpi_eval_package(&package, 1, &slp_typb);

    // and go to sleep
    uint16_t data;
    data = acpi_inw(acpi_fadt->pm1a_control_block);
    data &= 0xE3FF;
    data |= (slp_typa.integer << 10) | ACPI_SLEEP;
    acpi_outw(acpi_fadt->pm1a_control_block, data);

    if(acpi_fadt->pm1b_control_block != 0)
    {
        data = acpi_inw(acpi_fadt->pm1b_control_block);
        data &= 0xE3FF;
        data |= (slp_typb.integer << 10) | ACPI_SLEEP;
        acpi_outw(acpi_fadt->pm1b_control_block, data);
    }

    /* poll the wake status */
    while(!(acpi_last_event & ACPI_WAKE));

    return 0;
}


