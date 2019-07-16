/*
 * Lightweight ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
 */

/* Sleeping Functions */
/* As of now, it's only for S5 (shutdown) sleep, because other sleeping states
 * need NVS and other things I still need to research */

#include <lai/helpers/sleep.h>
#include "../core/libc.h"
#include "../core/eval.h"

// lai_enter_sleep(): Enters a sleeping state
// Param:    uint8_t state - 0-5 to correspond with states S0-S5
// Return:    int - 0 on success

int lai_enter_sleep(uint8_t sleep_state)
{
    if(!laihost_inw || !laihost_outw)
        lai_panic("lai_enter_sleep() requires port I/O");

    LAI_CLEANUP_STATE lai_state_t state;
    lai_init_state(&state);

    const char *sleep_object;
    switch (sleep_state) {
        case 0: sleep_object = "\\_S0"; break;
        case 1: sleep_object = "\\_S1"; break;
        case 2: sleep_object = "\\_S2"; break;
        case 3: sleep_object = "\\_S3"; break;
        case 4: sleep_object = "\\_S4"; break;
        case 5: sleep_object = "\\_S5"; break;
        default:
            lai_panic("undefined sleep state S%d", sleep_state);
    }

    // get sleeping package
    lai_nsnode_t *handle = lai_resolve_path(NULL, sleep_object);
    if(!handle) {
        lai_debug("sleep state S%d is not supported.", sleep_state);
        return 1;
    }

    LAI_CLEANUP_VAR lai_variable_t package = LAI_VAR_INITIALIZER;
    LAI_CLEANUP_VAR lai_variable_t slp_typa = LAI_VAR_INITIALIZER;
    LAI_CLEANUP_VAR lai_variable_t slp_typb = LAI_VAR_INITIALIZER;
    int eval_status;
    eval_status = lai_eval(&package, handle, &state);
    if(eval_status) {
        lai_debug("sleep state S%d is not supported.", sleep_state);
        return 1;
    }

    lai_debug("entering sleep state S%d...", sleep_state);

    // ACPI spec says we should call _PTS() and _GTS() before actually sleeping
    // Who knows, it might do some required firmware-specific stuff
    handle = lai_resolve_path(NULL, "\\_PTS");

    if(handle) {
        lai_init_state(&state);

        // pass the sleeping type as an argument
        LAI_CLEANUP_VAR lai_variable_t sleep_object = LAI_VAR_INITIALIZER;
        sleep_object.type = LAI_INTEGER;
        sleep_object.integer = sleep_state & 0xFF;

        lai_debug("execute _PTS(%d)", sleep_state);
        lai_eval_largs(NULL, handle, &state, &sleep_object, NULL);
        lai_finalize_state(&state);
    }

    handle = lai_resolve_path(NULL, "\\_GTS");

    if(handle) {
        lai_init_state(&state);

        // pass the sleeping type as an argument
        LAI_CLEANUP_VAR lai_variable_t sleep_object = LAI_VAR_INITIALIZER;
        sleep_object.type = LAI_INTEGER;
        sleep_object.integer = sleep_state & 0xFF;

        lai_debug("execute _GTS(%d)", sleep_state);
        lai_eval_largs(NULL, handle, &state, &sleep_object, NULL);
        lai_finalize_state(&state);
    }

    lai_obj_get_pkg(&package, 0, &slp_typa);
    lai_obj_get_pkg(&package, 1, &slp_typb);

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


