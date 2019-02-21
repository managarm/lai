
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 by Omar Muhamed
 */

/* Sleeping Functions */
/* As of now, it's only for S5 (shutdown) sleep, because other sleeping states
 * need NVS and other things I still need to research */

#include <lai/core.h>

// lai_enter_sleep(): Enters a sleeping state
// Param:    uint8_t state - 0-5 to correspond with states S0-S5
// Return:    int - 0 on success

int lai_enter_sleep(uint8_t state)
{
    if(state > 5)
    {
        lai_debug("undefined sleep state S%d\n", state);
        return 1;
    }

    uint8_t sleep_object[] = "_Sx_";
    sleep_object[2] = state + '0';

    // get sleeping package
    lai_nsnode_t *handle = acpins_resolve((char*)sleep_object);
    if(!handle)
    {
        lai_debug("sleep state S%d is not supported.\n", state);
        return 1;
    }

    lai_object_t package = {0};
    lai_object_t slp_typa = {0};
    lai_object_t slp_typb = {0};
    int eval_status;
    eval_status = lai_eval(&package, handle->path);
    if(eval_status != 0)
    {
        lai_debug("sleep state S%d is not supported.\n", state);
        return 1;
    }

    lai_debug("entering sleep state S%d...\n", state);

    // ACPI spec says we should call _PTS() and _GTS() before actually sleeping
    // Who knows, it might do some required firmware-specific stuff
    lai_state_t acpi_state;
    handle = acpins_resolve("_PTS");

    if(handle)
    {
        lai_init_state(&acpi_state);

        // pass the sleeping type as an argument
        lai_arg(&acpi_state, 0)->type = LAI_INTEGER;
        lai_arg(&acpi_state, 0)->integer = (uint64_t)state & 0xFF;

        lai_debug("execute _PTS(%d)\n", state);
        lai_exec_method(handle, &acpi_state);
        lai_finalize_state(&acpi_state);
    }

    handle = acpins_resolve("_GTS");

    if(handle)
    {
        lai_init_state(&acpi_state);

        // pass the sleeping type as an argument
        lai_arg(&acpi_state, 0)->type = LAI_INTEGER;
        lai_arg(&acpi_state, 0)->integer = (uint64_t)state & 0xFF;

        lai_debug("execute _GTS(%d)\n", state);
        lai_exec_method(handle, &acpi_state);
        lai_finalize_state(&acpi_state);
    }

    lai_eval_package(&package, 0, &slp_typa);
    lai_eval_package(&package, 1, &slp_typb);

    // and go to sleep
    uint16_t data;
    data = lai_inw(lai_fadt->pm1a_control_block);
    data &= 0xE3FF;
    data |= (slp_typa.integer << 10) | ACPI_SLEEP;
    lai_outw(lai_fadt->pm1a_control_block, data);

    if(lai_fadt->pm1b_control_block != 0)
    {
        data = lai_inw(lai_fadt->pm1b_control_block);
        data &= 0xE3FF;
        data |= (slp_typb.integer << 10) | ACPI_SLEEP;
        lai_outw(lai_fadt->pm1b_control_block, data);
    }

    /* poll the wake status */
    while(!(lai_last_event & ACPI_WAKE));

    return 0;
}


