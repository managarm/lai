
/*
 * Lightweight ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
 */

/* PCI IRQ Routing */
/* Every PCI device that is capable of generating an IRQ has an "interrupt pin"
   field in its configuration space. Contrary to what most people believe, this
   field is valid for both the PIC and the I/O APIC. The PCI local bus spec clearly
   says the "interrupt line" field everyone trusts are simply for BIOS or OS-
   -specific use. Therefore, nobody should assume it contains the real IRQ. Instead,
   the four PCI pins should be used: LNKA, LNKB, LNKC and LNKD. */

#include <lai/helpers/pci.h>
#include <lai/helpers/resource.h>
#include "../core/libc.h"
#include "../core/eval.h"

#define PCI_PNP_ID        "PNP0A03"
#define PCIE_PNP_ID       "PNP0A08"

int lai_pci_route(acpi_resource_t *dest, uint16_t seg, uint8_t bus, uint8_t slot, uint8_t function) {

    uint8_t pin = (uint8_t)laihost_pci_readb(seg, bus, slot, function, 0x3D);
    if (!pin || pin > 4)
        return 1;

    if (lai_pci_route_pin(dest, seg, bus, slot, function, pin))
        return 1;
    return 0;
}

lai_api_error_t lai_pci_route_pin(acpi_resource_t *dest, uint16_t seg, uint8_t bus, uint8_t slot, uint8_t function, uint8_t pin) {
    LAI_CLEANUP_STATE lai_state_t state;
    lai_init_state(&state);

    LAI_ENSURE(pin && pin <= 4);

    // PCI numbers pins from 1, but ACPI numbers them from 0. Hence we
    // subtract 1 to arrive at the correct pin number.
    pin--;
    // find the PCI bus in the namespace
    LAI_CLEANUP_VAR lai_variable_t bus_number = LAI_VAR_INITIALIZER;
    LAI_CLEANUP_VAR lai_variable_t seg_number = LAI_VAR_INITIALIZER;
    LAI_CLEANUP_VAR lai_variable_t pci_pnp_id = LAI_VAR_INITIALIZER;
    LAI_CLEANUP_VAR lai_variable_t pcie_pnp_id = LAI_VAR_INITIALIZER;
    lai_eisaid(&pci_pnp_id, PCI_PNP_ID);
    lai_eisaid(&pcie_pnp_id, PCIE_PNP_ID);

    lai_nsnode_t *handle = NULL;

    lai_nsnode_t *sb_handle = lai_resolve_path(NULL, "\\_SB_");
    LAI_ENSURE(sb_handle);
    struct lai_ns_child_iterator iter = LAI_NS_CHILD_ITERATOR_INITIALIZER(sb_handle);
    lai_nsnode_t *node;
    while ((node = lai_ns_child_iterate(&iter))) {

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

        uint64_t seg_result = 0;

        lai_nsnode_t *seg_handle = lai_resolve_path(node, "_SEG");
        if (seg_handle) {
            if (lai_eval(&seg_number, seg_handle, &state)) {
                lai_warn("failed to evaluate _SEG");
                continue;
            }
            lai_obj_get_integer(&seg_number, &seg_result);
        }

        if (bbn_result == bus && seg_result == seg) {
            handle = node;
            break;
        }
    }

    if (!handle)
        return LAI_ERROR_NO_SUCH_NODE;

    // read the PCI routing table
    lai_nsnode_t *prt_handle = lai_resolve_path(handle, "_PRT");
    if (!prt_handle) {
        lai_warn("host bridge has no _PRT");
        return LAI_ERROR_NO_SUCH_NODE;
    }

    LAI_CLEANUP_VAR lai_variable_t prt = LAI_VAR_INITIALIZER;
    LAI_CLEANUP_VAR lai_variable_t prt_package = LAI_VAR_INITIALIZER;
    LAI_CLEANUP_VAR lai_variable_t prt_entry = LAI_VAR_INITIALIZER;

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
        return LAI_ERROR_EXECUTION_FAILURE;
    }

    size_t i = 0;

    for (;;) {
        // read the _PRT package
        if (lai_obj_get_pkg(&prt, i, &prt_package))
            return LAI_ERROR_UNEXPECTED_RESULT;

        if (prt_package.type != LAI_PACKAGE)
            return LAI_ERROR_TYPE_MISMATCH;

        // read the device address
        if (lai_obj_get_pkg(&prt_package, 0, &prt_entry))
            return LAI_ERROR_UNEXPECTED_RESULT;

        if (prt_entry.type != LAI_INTEGER)
            return LAI_ERROR_TYPE_MISMATCH;

        // is this the device we want?
        if ((prt_entry.integer >> 16) == slot) {
            if ((prt_entry.integer & 0xFFFF) == 0xFFFF || (prt_entry.integer & 0xFFFF) == function) {
                // is this the interrupt pin we want?
                if (lai_obj_get_pkg(&prt_package, 1, &prt_entry))
                    return LAI_ERROR_UNEXPECTED_RESULT;

                if (prt_entry.type != LAI_INTEGER)
                    return LAI_ERROR_TYPE_MISMATCH;

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
    if (lai_obj_get_pkg(&prt_package, 2, &prt_entry))
        return LAI_ERROR_UNEXPECTED_RESULT;

    acpi_resource_t *res;
    size_t res_count;

    int prt_entry_type = lai_obj_get_type(&prt_entry);
    if (prt_entry_type == LAI_TYPE_INTEGER) {
        // Direct routing to a GSI.
        uint64_t gsi;
        if (lai_obj_get_pkg(&prt_package, 3, &prt_entry))
            return LAI_ERROR_UNEXPECTED_RESULT;
        if (lai_obj_get_integer(&prt_entry, &gsi))
            return LAI_ERROR_UNEXPECTED_RESULT;

        dest->type = ACPI_RESOURCE_IRQ;
        dest->base = gsi;
        dest->irq_flags = ACPI_IRQ_LEVEL | ACPI_IRQ_ACTIVE_HIGH | ACPI_IRQ_SHARED;

        lai_debug("PCI device %X:%X:%X:%X is using IRQ %d", seg, bus, slot, function, (int)dest->base);
        return LAI_ERROR_NONE;
    } else if (prt_entry_type == LAI_TYPE_DEVICE) {
        // GSI is determined by an Interrupt Link Device.
        lai_nsnode_t *link_handle;
        if (lai_obj_get_handle(&prt_entry, &link_handle))
            return LAI_ERROR_UNEXPECTED_RESULT;
        LAI_CLEANUP_FREE_STRING char *fullpath = lai_stringify_node_path(link_handle);
        lai_debug("PCI interrupt link is %s", fullpath);

        // read the resource template of the device
        res = lai_calloc(sizeof(acpi_resource_t), ACPI_MAX_RESOURCES);
        res_count = lai_read_resource(link_handle, res);

        if (!res_count)
            return LAI_ERROR_UNEXPECTED_RESULT;

        for (size_t i = 0; i < res_count; i++) {
            if (res[i].type == ACPI_RESOURCE_IRQ) {
                dest->type = ACPI_RESOURCE_IRQ;
                dest->base = res[i].base;
                dest->irq_flags = res[i].irq_flags;

                laihost_free(res);

                lai_debug("PCI device %X:%X:%X:%X is using IRQ %d", seg, bus, slot, function, (int)dest->base);
                return LAI_ERROR_NONE;
            }

            i++;
        }

        return LAI_ERROR_NONE;
    } else {
        lai_warn("PRT entry has unexpected type %d", prt_entry_type);
        return LAI_ERROR_TYPE_MISMATCH;
    }
}

lai_nsnode_t *lai_pci_find_device(lai_nsnode_t *bus, uint8_t slot, uint8_t function, lai_state_t *state){
    LAI_ENSURE(bus);
    LAI_ENSURE(state);
    
    uint64_t device_adr = ((slot << 16) | function);

    struct lai_ns_child_iterator iter = LAI_NS_CHILD_ITERATOR_INITIALIZER(bus);
    lai_nsnode_t *node;
    while ((node = lai_ns_child_iterate(&iter))) {
        uint64_t adr_result = 0;
        LAI_CLEANUP_VAR lai_variable_t adr = LAI_VAR_INITIALIZER;
        lai_nsnode_t *adr_handle = lai_resolve_path(node, "_ADR");
        if (adr_handle) {
            if (lai_eval(&adr, adr_handle, &state)) {
                lai_warn("failed to evaluate _ADR");
                continue;
            }
            lai_obj_get_integer(&adr, &adr_result);
        }

        if(adr_result == device_adr){
            return node;
        }
    }

    return NULL;
}

lai_nsnode_t *lai_pci_find_bus(uint16_t seg, uint8_t bus, lai_state_t *state){
    LAI_CLEANUP_VAR lai_variable_t pci_pnp_id = LAI_VAR_INITIALIZER;
    LAI_CLEANUP_VAR lai_variable_t pcie_pnp_id = LAI_VAR_INITIALIZER;
    lai_eisaid(&pci_pnp_id, PCI_PNP_ID);
    lai_eisaid(&pcie_pnp_id, PCIE_PNP_ID);

    lai_nsnode_t *sb_handle = lai_resolve_path(NULL, "\\_SB_");
    LAI_ENSURE(sb_handle);
    struct lai_ns_child_iterator iter = LAI_NS_CHILD_ITERATOR_INITIALIZER(sb_handle);
    lai_nsnode_t *node;
    while ((node = lai_ns_child_iterate(&iter))) {
        if (lai_check_device_pnp_id(node, &pci_pnp_id, &state) &&
            lai_check_device_pnp_id(node, &pcie_pnp_id, &state)) {
            continue;
        }

        LAI_CLEANUP_VAR lai_variable_t bus_number = LAI_VAR_INITIALIZER;
        uint64_t bbn_result = 0;
        lai_nsnode_t *bbn_handle = lai_resolve_path(node, "_BBN");
        if (bbn_handle) {
            if (lai_eval(&bus_number, bbn_handle, &state)) {
                lai_warn("failed to evaluate _BBN");
                continue;
            }
            lai_obj_get_integer(&bus_number, &bbn_result);
        }

        LAI_CLEANUP_VAR lai_variable_t seg_number = LAI_VAR_INITIALIZER;
        uint64_t seg_result = 0;
        lai_nsnode_t *seg_handle = lai_resolve_path(node, "_SEG");
        if (seg_handle) {
            if (lai_eval(&seg_number, seg_handle, &state)){
                lai_warn("failed to evaluate _SEG");
                continue;
            }
            lai_obj_get_integer(&seg_number, &seg_result);
        }
        
        if(seg_result == seg && bbn_result == bus){
            return node;
        }
    }

    return NULL;
}