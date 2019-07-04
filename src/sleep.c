
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
 */

/* Sleeping Functions */
/* As of now, it's only for S5 (shutdown) sleep, because other sleeping states
 * need NVS and other things I still need to research */

#include <lai/core.h>
#include "libc.h"
#include "eval.h"

// lai_enter_sleep(): Enters a sleeping state
// Param:    uint8_t state - 0-5 to correspond with states S0-S5
// Return:    int - 0 on success

int lai_enter_sleep(uint8_t state)
{
    if(!laihost_inw || !laihost_outw)
        lai_panic("lai_enter_sleep() requires port I/O");

    const char *sleep_object;
    switch (state) {
        case 0: sleep_object = "\\_S0"; break;
        case 1: sleep_object = "\\_S1"; break;
        case 2: sleep_object = "\\_S2"; break;
        case 3: sleep_object = "\\_S3"; break;
        case 4: sleep_object = "\\_S4"; break;
        case 5: sleep_object = "\\_S5"; break;
        default:
            lai_panic("undefined sleep state S%d", state);
    }

    // get sleeping package
    lai_nsnode_t *handle = lai_resolve_path(NULL, sleep_object);
    if(!handle) {
        lai_debug("sleep state S%d is not supported.", state);
        return 1;
    }

    lai_object_t package = {0};
    lai_object_t slp_typa = {0};
    lai_object_t slp_typb = {0};
    int eval_status;
    eval_status = lai_legacy_eval(&package, handle->fullpath);
    if(eval_status) {
        lai_debug("sleep state S%d is not supported.", state);
        return 1;
    }

    lai_debug("entering sleep state S%d...", state);

    // ACPI spec says we should call _PTS() and _GTS() before actually sleeping
    // Who knows, it might do some required firmware-specific stuff
    lai_state_t acpi_state;
    handle = lai_resolve_path(NULL, "\\_PTS");

    if(handle) {
        lai_init_state(&acpi_state);

        // pass the sleeping type as an argument
        lai_arg(&acpi_state, 0)->type = LAI_INTEGER;
        lai_arg(&acpi_state, 0)->integer = (uint64_t)state & 0xFF;

        lai_debug("execute _PTS(%d)", state);
        lai_exec_method(handle, &acpi_state);
        lai_finalize_state(&acpi_state);
    }

    handle = lai_resolve_path(NULL, "\\_GTS");

    if(handle) {
        lai_init_state(&acpi_state);

        // pass the sleeping type as an argument
        lai_arg(&acpi_state, 0)->type = LAI_INTEGER;
        lai_arg(&acpi_state, 0)->integer = (uint64_t)state & 0xFF;

        lai_debug("execute _GTS(%d)", state);
        lai_exec_method(handle, &acpi_state);
        lai_finalize_state(&acpi_state);
    }

    lai_eval_package(&package, 0, &slp_typa);
    lai_eval_package(&package, 1, &slp_typb);

    // and go to sleep
    uint16_t data;
    data = laihost_inw(lai_fadt->pm1a_control_block);
    data &= 0xE3FF;
    data |= (slp_typa.integer << 10) | ACPI_SLEEP;
    laihost_outw(lai_fadt->pm1a_control_block, data);

    if(lai_fadt->pm1b_control_block) {
        data = laihost_inw(lai_fadt->pm1b_control_block);
        data &= 0xE3FF;
        data |= (slp_typb.integer << 10) | ACPI_SLEEP;
        laihost_outw(lai_fadt->pm1b_control_block, data);
    }

    /* poll the wake status */
    while(!(lai_last_event & ACPI_WAKE))
        ;

    return 0;
}


