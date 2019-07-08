
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
 */

/* PCI IRQ Routing */
/* Every PCI device that is capable of generating an IRQ has an "interrupt pin"
   field in its configuration space. Contrary to what most people believe, this
   field is valid for both the PIC and the I/O APIC. The PCI local bus spec clearly
   says the "interrupt line" field everyone trusts are simply for BIOS or OS-
   -specific use. Therefore, nobody should assume it contains the real IRQ. Instead,
   the four PCI pins should be used: LNKA, LNKB, LNKC and LNKD. */

#include <lai/core.h>
#include "libc.h"
#include "eval.h"

#define PCI_PNP_ID        "PNP0A03"
#define PCIE_PNP_ID        "PNP0A08"

// This function resolves PCI IRQ routing for a specific device corresponding to
// a given bus, slot, function combination.
int lai_pci_route(acpi_resource_t *dest, uint8_t bus, uint8_t slot, uint8_t function) {
    //lai_debug("attempt to resolve PCI IRQ for device %X:%X:%X", bus, slot, function);
    LAI_CLEANUP_STATE lai_state_t state;
    lai_init_state(&state);

    // determine the interrupt pin
    uint8_t pin = (uint8_t)(laihost_pci_read(bus, slot, function, 0x3C) >> 8);
    if (!pin || pin > 4)
        return 1;

    // PCI numbers pins from 1, but ACPI numbers them from 0. Hence we
    // subtract 1 to arrive at the correct pin number.
    pin--;
    // find the PCI bus in the namespace
    lai_object_t bus_number = {0};
    lai_object_t pci_pnp_id = {0};
    lai_object_t pcie_pnp_id = {0};
    lai_eisaid(&pci_pnp_id, PCI_PNP_ID);
    lai_eisaid(&pcie_pnp_id, PCIE_PNP_ID);

    lai_nsnode_t *handle = NULL;

    struct lai_ns_iterator iter = LAI_NS_ITERATOR_INIT;
    lai_nsnode_t *node;
    while ((node = lai_ns_iterate(&iter))) {

        if (lai_check_device_pnp_id(node, &pci_pnp_id, &state) &&
                lai_check_device_pnp_id(node, &pcie_pnp_id, &state)) {
            continue;
        }

        uint64_t bbn_result = 0;

        lai_nsnode_t *bbn_handle = lai_resolve_path(node, "_BBN");
        if (bbn_handle) {
            if (lai_eval(&bus_number, bbn_handle, &state)) {
                lai_warn("failed to evaluate _BBN");
                continue;
            }
            lai_obj_get_integer(&bus_number, &bbn_result);
        }

        if (bbn_result == bus) {
            handle = node;
            break;
        }
    }

    if (!handle)
        return 1;

    // read the PCI routing table
    lai_nsnode_t *prt_handle = lai_resolve_path(handle, "_PRT");
    if (!prt_handle) {
        lai_warn("host bridge has no _PRT");
        return 1;
    }

    int status;
    lai_object_t prt = {0};
    lai_object_t prt_package = {0};
    lai_object_t prt_entry = {0};

    /* _PRT is a package of packages. Each package within the PRT is in the following format:
       0: Integer:    Address of device. Low WORD = function, high WORD = slot
       1: Integer:    Interrupt pin. 0 = LNKA, 1 = LNKB, 2 = LNKC, 3 = LNKD
       2: Name or Integer:    If name, this is the namespace device which allocates the interrupt.
        If it's an integer, then this field is ignored.
       3: Integer:    If offset 2 is a Name, this is the index within the resource descriptor
        of the specified device which contains the PCI interrupt. If offset 2 is an
        integer, this field is the ACPI GSI of this PCI IRQ. */

    if (lai_eval(&prt, prt_handle, &state)) {
        lai_warn("failed to evaluate _PRT");
        return 1;
    }

    size_t i = 0;

    for (;;) {
        // read the _PRT package
        status = lai_eval_package(&prt, i, &prt_package);
        if (status)
            return 1;

        if (prt_package.type != LAI_PACKAGE)
            return 1;

        // read the device address
        status = lai_eval_package(&prt_package, 0, &prt_entry);
        if (status)
            return 1;

        if (prt_entry.type != LAI_INTEGER)
            return 1;

        // is this the device we want?
        if ((prt_entry.integer >> 16) == slot) {
            if ((prt_entry.integer & 0xFFFF) == 0xFFFF || (prt_entry.integer & 0xFFFF) == function) {
                // is this the interrupt pin we want?
                status = lai_eval_package(&prt_package, 1, &prt_entry);
                if (status)
                    return 1;

                if (prt_entry.type != LAI_INTEGER)
                    return 1;

                if (prt_entry.integer == pin)
                    goto resolve_pin;
            }
        }

        // continue
        i++;
    }

resolve_pin:
    // here we've found what we need
    // is it a link device or a GSI?
    if (lai_eval_package(&prt_package, 2, &prt_entry))
        return 1;

    acpi_resource_t *res;
    size_t res_count;

    int prt_entry_type = lai_obj_get_type(&prt_entry);
    if (prt_entry_type == LAI_TYPE_INTEGER) {
        // Direct routing to a GSI.
        uint64_t gsi;
        if (lai_eval_package(&prt_package, 3, &prt_entry))
            return 1;
        if (lai_obj_get_integer(&prt_entry, &gsi))
            return 1;

        dest->type = ACPI_RESOURCE_IRQ;
        dest->base = gsi;
        dest->irq_flags = ACPI_IRQ_LEVEL | ACPI_IRQ_ACTIVE_HIGH | ACPI_IRQ_SHARED;

        lai_debug("PCI device %X:%X:%X is using IRQ %d", bus, slot, function, (int)dest->base);
        return 0;
    } else if (prt_entry_type == LAI_TYPE_DEVICE) {
        // GSI is determined by an Interrupt Link Device.
        lai_nsnode_t *link_handle;
        if (lai_obj_get_handle(&prt_entry, &link_handle))
            return 1;
        lai_debug("PCI interrupt link is %s", link_handle->fullpath);

        // read the resource template of the device
        res = lai_calloc(sizeof(acpi_resource_t), ACPI_MAX_RESOURCES);
        res_count = lai_read_resource(link_handle, res);

        if (!res_count)
            return 1;

        for (size_t i = 0; i < res_count; i++) {
            if (res[i].type == ACPI_RESOURCE_IRQ) {
                dest->type = ACPI_RESOURCE_IRQ;
                dest->base = res[i].base;
                dest->irq_flags = res[i].irq_flags;

                laihost_free(res);

                lai_debug("PCI device %X:%X:%X is using IRQ %d", bus, slot, function, (int)dest->base);
                return 0;
            }

            i++;
        }

        return 0;
    } else {
        lai_warn("PRT entry has unexpected type %d", prt_entry_type);
        return 1;
    }
}












