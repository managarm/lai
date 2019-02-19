
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 by Omar Muhamed
 */

/* System Control Interrupt Initialization */

#include <lai/core.h>

static void lai_init_children(char *);

volatile uint16_t lai_last_event = 0;

// lai_read_event(): Reads the contents of the event register
// Return:  uint16_t - contents of event register

uint16_t lai_read_event()
{
    uint16_t a = 0, b = 0;
    if(lai_fadt->pm1a_event_block)
    {
        a = lai_inw(lai_fadt->pm1a_event_block);
        lai_outw(lai_fadt->pm1a_event_block, a);
    }

    if(lai_fadt->pm1b_event_block)
    {
        b = lai_inw(lai_fadt->pm1b_event_block);
        lai_outw(lai_fadt->pm1b_event_block, b);
    }

    lai_last_event = a | b;
    return lai_last_event;
}

// lai_set_event(): Sets the event enable registers
// Param:   uint16_t value - value to be written

void lai_set_event(uint16_t value)
{
    uint16_t a = lai_fadt->pm1a_event_block + (lai_fadt->pm1_event_length / 2);
    uint16_t b = lai_fadt->pm1b_event_block + (lai_fadt->pm1_event_length / 2);

    if(lai_fadt->pm1a_event_block)
        lai_outw(a, value);

    if(lai_fadt->pm1b_event_block)
        lai_outw(b, value);

    lai_debug("wrote event register value 0x%04X\n", value);
}

// lai_enable_acpi(): Enables ACPI SCI
// Param:   uint32_t mode - IRQ mode (ACPI spec section 5.8.1)
// Return:  int - 0 on success

int lai_enable_acpi(uint32_t mode)
{
    lai_nsnode_t *handle;
    lai_state_t state;
    lai_debug("attempt to enable ACPI...\n");

    /* first run \._SB_._INI */
    handle = acpins_resolve("\\._SB_._INI");
    if(handle) {
        lai_init_state(&state);
        if(!lai_exec_method(handle, &state))
            lai_debug("evaluated \\._SB_._INI\n");
        lai_finalize_state(&state);
    }

    /* _STA/_INI for all devices */
    lai_init_children("\\._SB_");

    /* tell the firmware about the IRQ mode */
    handle = acpins_resolve("\\._PIC");
    if(handle)
    {
        lai_init_state(&state);
        lai_arg(&state, 0)->type = LAI_INTEGER;
        lai_arg(&state, 0)->integer = mode;

        if(!lai_exec_method(handle, &state))
            lai_debug("evaluated \\._PIC(%d)\n", mode);
        lai_finalize_state(&state);
    }

    /* enable ACPI SCI */
    lai_outb(lai_fadt->smi_command_port, lai_fadt->acpi_enable);
    lai_sleep(10);

    for(int i = 0; i < 100; i++)
    {
        if(lai_inw(lai_fadt->pm1a_control_block) & ACPI_ENABLED)
            break;

        lai_sleep(10);
    }

    /* set FADT event fields */
    lai_set_event(ACPI_POWER_BUTTON | ACPI_SLEEP_BUTTON | ACPI_WAKE);
    lai_read_event();

    lai_debug("ACPI is now enabled.\n");
    return 0;
}

static void lai_init_children(char *parent)
{
    lai_nsnode_t *node;
    lai_object_t object = {0};
    lai_nsnode_t *handle;
    lai_state_t state;
    char path[ACPI_MAX_NAME];
    int status;

    for(size_t i = 0; i < lai_ns_size; i++)
    {
        node = acpins_enum(parent, i);
        if(!node) return;

        if(node->type == LAI_NAMESPACE_DEVICE)
        {
            lai_strcpy(path, node->path);
            lai_strcpy(path + lai_strlen(path), "._STA");

            status = lai_eval(&object, path);
            if(status)
            {
                /* _STA not present, so assume 0x0F as ACPI spec says */
                object.type = LAI_INTEGER;
                object.integer = 0x0F;
            }

            /* if device is present, evaluate its _INI */
            if(object.integer & ACPI_STA_PRESENT)
            {
                lai_strcpy(path, node->path);
                lai_strcpy(path + lai_strlen(path), "._INI");
                handle = acpins_resolve(path);

                if(handle)
                {
                    lai_init_state(&state);
                    if(!lai_exec_method(handle, &state))
                        lai_debug("evaluated %s\n", path);
                    lai_finalize_state(&state);
                }
            }

            /* if functional and/or present, enumerate the children */
            if(object.integer & ACPI_STA_PRESENT ||
                object.integer & ACPI_STA_FUNCTION)
            {
                lai_init_children(node->path);
            }
        }
    }
}

