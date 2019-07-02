
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
 */

/* System Control Interrupt Initialization */

#include <lai/core.h>
#include "libc.h"
#include "exec_impl.h"

static void lai_init_children(char *);

volatile uint16_t lai_last_event = 0;

// read contents of event registers.
uint16_t lai_get_sci_event(void) {
    if (!laihost_inw || !laihost_outw)
        lai_panic("lai_read_event() requires port I/O");

    uint16_t a = 0, b = 0;
    if (lai_fadt->pm1a_event_block) {
        a = laihost_inw(lai_fadt->pm1a_event_block);
        laihost_outw(lai_fadt->pm1a_event_block, a);
    }

    if (lai_fadt->pm1b_event_block) {
        b = laihost_inw(lai_fadt->pm1b_event_block);
        laihost_outw(lai_fadt->pm1b_event_block, b);
    }

    lai_last_event = a | b;
    return lai_last_event;
}

// set event enable registers
void lai_set_sci_event(uint16_t value) {
    if (!laihost_inw || !laihost_outw)
        lai_panic("lai_set_event() requires port I/O");

    uint16_t a = lai_fadt->pm1a_event_block + (lai_fadt->pm1_event_length / 2);
    uint16_t b = lai_fadt->pm1b_event_block + (lai_fadt->pm1_event_length / 2);

    if (lai_fadt->pm1a_event_block)
        laihost_outw(a, value);

    if (lai_fadt->pm1b_event_block)
        laihost_outw(b, value);

    lai_debug("wrote event register value 0x%04X", value);
}

// lai_enable_acpi(): Enables ACPI SCI
// Param:   uint32_t mode - IRQ mode (ACPI spec section 5.8.1)
// Return:  int - 0 on success

int lai_enable_acpi(uint32_t mode) {
    lai_nsnode_t *handle;
    lai_state_t state;
    lai_debug("attempt to enable ACPI...");

    if (!laihost_inw || !laihost_outb)
        lai_panic("lai_enable_acpi() requires port I/O");
    if (!laihost_sleep)
        lai_panic("host does not provide timer functions required by lai_enable_acpi()");

    /* first run \._SB_._INI */
    handle = lai_resolve("\\._SB_._INI");
    if (handle) {
        lai_init_state(&state);
        if (!lai_exec_method(handle, &state))
            lai_debug("evaluated \\._SB_._INI");
        lai_finalize_state(&state);
    }

    /* _STA/_INI for all devices */
    lai_init_children("\\._SB_");

    /* tell the firmware about the IRQ mode */
    handle = lai_resolve("\\._PIC");
    if (handle) {
        lai_init_state(&state);
        lai_arg(&state, 0)->type = LAI_INTEGER;
        lai_arg(&state, 0)->integer = mode;

        if (!lai_exec_method(handle, &state))
            lai_debug("evaluated \\._PIC(%d)", mode);
        lai_finalize_state(&state);
    }

    /* enable ACPI SCI */
    laihost_outb(lai_fadt->smi_command_port, lai_fadt->acpi_enable);
    laihost_sleep(10);

    for (size_t i = 0; i < 100; i++) {
        if (laihost_inw(lai_fadt->pm1a_control_block) & ACPI_ENABLED)
            break;

        laihost_sleep(10);
    }

    /* set FADT event fields */
    lai_set_sci_event(ACPI_POWER_BUTTON | ACPI_SLEEP_BUTTON | ACPI_WAKE);
    lai_get_sci_event();

    lai_debug("ACPI is now enabled.");
    return 0;
}

static int evaluate_sta(lai_nsnode_t *node) {
    // If _STA not present, assume 0x0F as ACPI spec says.
    int sta = 0x0F;

    char path[ACPI_MAX_NAME];
    lai_strcpy(path, node->fullpath);
    lai_strcpy(path + lai_strlen(path), "._STA");

    lai_nsnode_t *handle = lai_resolve(path);
    if (handle) {
        lai_state_t state;
        lai_init_state(&state);
        if (lai_eval_node(handle, &state))
            lai_panic("could not evaluate _STA");
        sta = lai_retvalue(&state)->integer;
        lai_finalize_state(&state);
    }

    return sta;
}

static void lai_init_children(char *parent) {
    lai_nsnode_t *node;
    lai_nsnode_t *handle;
    char path[ACPI_MAX_NAME];

    for (size_t i = 0; i < lai_ns_size; i++) {
        node = lai_enum(parent, i);
        if (!node) return;

        if (node->type == LAI_NAMESPACE_DEVICE) {
            int sta = evaluate_sta(node);

            /* if device is present, evaluate its _INI */
            if (sta & ACPI_STA_PRESENT) {
                lai_strcpy(path, node->fullpath);
                lai_strcpy(path + lai_strlen(path), "._INI");
                handle = lai_resolve(path);

                if (handle) {
                    lai_state_t state;
                    lai_init_state(&state);
                    if (!lai_exec_method(handle, &state))
                        lai_debug("evaluated %s", path);
                    lai_finalize_state(&state);
                }
            }

            /* if functional and/or present, enumerate the children */
            if (sta & ACPI_STA_PRESENT || sta & ACPI_STA_FUNCTION)
                lai_init_children(node->fullpath);
        }
    }
}
