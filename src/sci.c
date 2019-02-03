
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 by Omar Muhamed
 */

/* System Control Interrupt Initialization */

#include "lai.h"

static void acpi_init_children(char *);

volatile uint16_t acpi_last_event = 0;

// acpi_read_event(): Reads the contents of the event register
// Return:  uint16_t - contents of event register

uint16_t acpi_read_event()
{
    uint16_t a = 0, b = 0;
    if(acpi_fadt->pm1a_event_block)
    {
        a = acpi_inw(acpi_fadt->pm1a_event_block);
        acpi_outw(acpi_fadt->pm1a_event_block, a);
    }

    if(acpi_fadt->pm1b_event_block)
    {
        b = acpi_inw(acpi_fadt->pm1b_event_block);
        acpi_outw(acpi_fadt->pm1b_event_block, b);
    }

    acpi_last_event = a | b;
    return acpi_last_event;
}

// acpi_set_event(): Sets the event enable registers
// Param:   uint16_t value - value to be written

void acpi_set_event(uint16_t value)
{
    uint16_t a = acpi_fadt->pm1a_event_block + (acpi_fadt->pm1_event_length / 2);
    uint16_t b = acpi_fadt->pm1b_event_block + (acpi_fadt->pm1_event_length / 2);

    if(acpi_fadt->pm1a_event_block)
        acpi_outw(a, value);

    if(acpi_fadt->pm1b_event_block)
        acpi_outw(b, value);

    acpi_debug("wrote event register value 0x%04X\n", value);
}

// acpi_enable(): Enables ACPI SCI
// Param:   uint32_t mode - IRQ mode (ACPI spec section 5.8.1)
// Return:  int - 0 on success

int acpi_enable(uint32_t mode)
{
    acpi_nsnode_t *handle;
    acpi_state_t state;
    acpi_debug("attempt to enable ACPI...\n");

    /* first run \._SB_._INI */
    handle = acpins_resolve("\\._SB_._INI");
    if(handle) {
        acpi_init_call_state(&state, handle);
        if(!acpi_exec_method(&state))
            acpi_debug("evaluated \\._SB_._INI\n");
        acpi_finalize_state(&state);
    }

    /* _STA/_INI for all devices */
    acpi_init_children("\\._SB_");

    /* tell the firmware about the IRQ mode */
    handle = acpins_resolve("\\._PIC");
    if(handle)
    {
        acpi_init_call_state(&state, handle);
        acpi_arg(&state, 0)->type = ACPI_INTEGER;
        acpi_arg(&state, 0)->integer = mode;

        if(!acpi_exec_method(&state))
            acpi_debug("evaluated \\._PIC(%d)\n", mode);
        acpi_finalize_state(&state);
    }

    /* enable ACPI SCI */
    acpi_outb(acpi_fadt->smi_command_port, acpi_fadt->acpi_enable);
    acpi_sleep(10);

    for(int i = 0; i < 100; i++)
    {
        if(acpi_inw(acpi_fadt->pm1a_control_block) & ACPI_ENABLED)
            break;

        acpi_sleep(10);
    }

    /* set FADT event fields */
    acpi_set_event(ACPI_POWER_BUTTON | ACPI_SLEEP_BUTTON | ACPI_WAKE);
    acpi_read_event();

    acpi_debug("ACPI is now enabled.\n");
    return 0;
}

static void acpi_init_children(char *parent)
{
    acpi_nsnode_t *node;
    acpi_object_t object = {0};
    acpi_nsnode_t *handle;
    acpi_state_t state;
    char path[ACPI_MAX_NAME];
    int status;

    for(size_t i = 0; i < acpi_ns_size; i++)
    {
        node = acpins_enum(parent, i);
        if(!node) return;

        if(node->type == ACPI_NAMESPACE_DEVICE)
        {
            acpi_strcpy(path, node->path);
            acpi_strcpy(path + acpi_strlen(path), "._STA");

            status = acpi_eval(&object, path);
            if(status)
            {
                /* _STA not present, so assume 0x0F as ACPI spec says */
                object.type = ACPI_INTEGER;
                object.integer = 0x0F;
            }

            /* if device is present, evaluate its _INI */
            if(object.integer & ACPI_STA_PRESENT)
            {
                acpi_strcpy(path, node->path);
                acpi_strcpy(path + acpi_strlen(path), "._INI");
                handle = acpins_resolve(path);

                if(handle)
                {
                    acpi_init_call_state(&state, handle);
                    if(!acpi_exec_method(&state))
                        acpi_debug("evaluated %s\n", state.name);
                    acpi_finalize_state(&state);
                }
            }

            /* if functional and/or present, enumerate the children */
            if(object.integer & ACPI_STA_PRESENT ||
                object.integer & ACPI_STA_FUNCTION)
            {
                acpi_init_children(node->path);
            }
        }
    }
}

