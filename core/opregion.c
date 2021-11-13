/*
 * Lightweight AML Interpreter
 * Copyright (C) 2018-2021 The lai authors
 */

/* ACPI OperationRegion Implementation */
/* OperationRegions allow ACPI's AML to access I/O ports, system memory, system
 * CMOS, PCI config, and other hardware used for I/O with the chipset. */

#include <lai/core.h>

#include "aml_opcodes.h"
#include "exec_impl.h"
#include "libc.h"
#include "opregion.h"

// this assumes little endian
static void lai_buffer_put_at(uint8_t *buffer, uint64_t value, size_t bit_offset, size_t num_bits) {
    size_t progress = 0;
    while (progress < num_bits) {
        size_t in_byte_offset = (bit_offset + progress) & 7;
        size_t access_size = LAI_MIN(num_bits - progress, 8 - in_byte_offset);
        size_t mask = (1 << access_size) - 1;

        buffer[(bit_offset + progress) / 8] |= ((value >> progress) & mask) << in_byte_offset;

        progress += access_size;
    }
}

// this assumes little endian
static uint64_t lai_buffer_get_at(uint8_t *buffer, size_t bit_offset, size_t num_bits) {
    uint64_t value = 0;
    size_t progress = 0;
    while (progress < num_bits) {
        size_t in_byte_offset = (bit_offset + progress) & 7;
        size_t access_size = LAI_MIN(num_bits - progress, 8 - in_byte_offset);
        size_t mask = (1 << access_size) - 1;

        value |= (uint64_t)((buffer[(bit_offset + progress) / 8] >> in_byte_offset) & mask)
                 << progress;

        progress += access_size;
    }
    return value;
}

static size_t lai_calculate_access_width(lai_nsnode_t *field) {
    lai_nsnode_t *opregion = field->fld_region_node;

    size_t access_size;
    switch (field->fld_flags & 0xF) {
        case FIELD_BYTE_ACCESS:
            access_size = 8;
            break;
        case FIELD_WORD_ACCESS:
            access_size = 16;
            break;
        case FIELD_DWORD_ACCESS:
            access_size = 32;
            break;
        case FIELD_QWORD_ACCESS:
            access_size = 64;
            break;
        case FIELD_ANY_ACCESS: {
            _Static_assert(sizeof(int) == 4, "int is not 32 bits");
            // This rounds up to the next power of 2.
            access_size = 1;
            if (field->fld_size > 1)
                access_size = 1 << (32 - __builtin_clz(field->fld_size - 1));

            size_t max_access_width = 32;
            if (opregion->op_address_space == ACPI_OPREGION_MEMORY)
                max_access_width = 64;

            if (access_size > max_access_width)
                access_size = max_access_width;

            if (access_size < 8)
                access_size = 8;

            break;
        }
        default:
            lai_panic("invalid access size");
    }

    return access_size;
}

static lai_nsnode_t *lai_find_parent_root_of(lai_nsnode_t *node, lai_state_t *state) {
    LAI_CLEANUP_VAR lai_variable_t pci_id = LAI_VAR_INITIALIZER;
    LAI_CLEANUP_VAR lai_variable_t pcie_id = LAI_VAR_INITIALIZER;

    lai_eisaid(&pci_id, ACPI_PCI_ROOT_BUS_PNP_ID);
    lai_eisaid(&pcie_id, ACPI_PCIE_ROOT_BUS_PNP_ID);

    while (node) {
        if (lai_check_device_pnp_id(node, &pci_id, state)
            || lai_check_device_pnp_id(node, &pcie_id, state))
            return node;

        node = lai_ns_get_parent(node);
    }

    return NULL;
}

static void lai_get_pci_params(lai_nsnode_t *opregion, uint64_t *seg, uint64_t *bbn,
                               uint64_t *adr) {
    LAI_CLEANUP_VAR lai_variable_t bus_number = LAI_VAR_INITIALIZER;
    LAI_CLEANUP_VAR lai_variable_t seg_number = LAI_VAR_INITIALIZER;
    LAI_CLEANUP_VAR lai_variable_t address_number = LAI_VAR_INITIALIZER;

    LAI_CLEANUP_STATE lai_state_t state;
    lai_init_state(&state); // XXX: take the state as an argument instead?

    lai_nsnode_t *device = lai_ns_get_parent(opregion);
    if (!device)
        lai_panic("lai_get_pci_params: Couldn't get device");

    lai_nsnode_t *bus = lai_ns_get_parent(device);
    if (!bus)
        lai_panic("lai_get_pci_params: Couldn't get bus");

    lai_nsnode_t *root_bus = lai_find_parent_root_of(bus, &state);
    if (!root_bus)
        lai_panic("lai_get_pci_params: Couldn't get root bus");

    // PCI seg number is in the _SEG object.
    lai_nsnode_t *seg_handle = lai_resolve_search(root_bus, "_SEG");
    if (seg_handle) {
        if (lai_eval(&seg_number, seg_handle, &state))
            lai_panic("could not evaluate _SEG of OperationRegion()");
        if (seg)
            *seg = seg_number.integer;
    }

    // PCI bus number is in the _BBN object.
    lai_nsnode_t *bbn_handle = lai_resolve_search(root_bus, "_BBN");
    if (bbn_handle) {
        if (lai_eval(&bus_number, bbn_handle, &state))
            lai_panic("could not evaluate _BBN of OperationRegion()");
        if (bbn)
            *bbn = bus_number.integer;
    }

    // Device slot/function is in the _ADR object.
    lai_nsnode_t *adr_handle = lai_resolve_search(opregion, "_ADR");
    if (adr_handle) {
        if (lai_eval(&address_number, adr_handle, &state))
            lai_panic("could not evaluate _ADR of OperationRegion()");
        if (adr)
            *adr = address_number.integer;
    }
}

typedef uint8_t __attribute__((aligned(1))) mmio8_t;
typedef uint16_t __attribute__((aligned(1))) mmio16_t;
typedef uint32_t __attribute__((aligned(1))) mmio32_t;
typedef uint64_t __attribute__((aligned(1))) mmio64_t;

static uint64_t lai_perform_read(lai_nsnode_t *opregion, size_t access_size, size_t offset,
                                 uint64_t seg, uint64_t bbn, uint64_t adr) {
    struct lai_instance *instance = lai_current_instance();
    uint64_t value = 0;

    if (opregion->op_override) {
        if (instance->trace & LAI_TRACE_IO)
            lai_debug("lai_perform_read: %lu-bit read from overridden opregion at %lx (address "
                      "space %02u)",
                      access_size, opregion->op_base + offset, opregion->op_address_space);
        switch (access_size) {
            case 8:
                value = opregion->op_override->readb(opregion->op_base + offset,
                                                     opregion->op_userptr);
                break;
            case 16:
                value = opregion->op_override->readw(opregion->op_base + offset,
                                                     opregion->op_userptr);
                break;
            case 32:
                value = opregion->op_override->readd(opregion->op_base + offset,
                                                     opregion->op_userptr);
                break;
            case 64:
                value = opregion->op_override->readq(opregion->op_base + offset,
                                                     opregion->op_userptr);
                break;
            default:
                lai_panic("invalid access size");
        }
    } else {
        switch (opregion->op_address_space) {
            case ACPI_OPREGION_MEMORY: {
                if (instance->trace & LAI_TRACE_IO)
                    lai_debug("lai_perform_read: %lu-bit read from MMIO at %lx", access_size,
                              opregion->op_base + offset);
                if ((opregion->op_base + offset) & ((access_size / 8) - 1))
                    lai_warn("lai_perform_read: Unaligned %lu-bit read from MMIO at %lx",
                             access_size, opregion->op_base + offset);
                if (!laihost_map)
                    lai_panic(
                        "lai_perform_read: laihost_map needs to be implemented to read from MMIO");

                void *mmio = laihost_map(opregion->op_base + offset, access_size / 8);
                switch (access_size) {
                    case 8:
                        value = (*(volatile mmio8_t *)mmio);
                        break;
                    case 16:
                        value = (*(volatile mmio16_t *)mmio);
                        break;
                    case 32:
                        value = (*(volatile mmio32_t *)mmio);
                        break;
                    case 64:
                        value = (*(volatile mmio64_t *)mmio);
                        break;
                    default:
                        lai_panic("invalid access size");
                }
                break;
            }
            case ACPI_OPREGION_IO: {
                if (instance->trace & LAI_TRACE_IO)
                    lai_debug("lai_perform_read: %lu-bit read from I/O port at %lx", access_size,
                              opregion->op_base + offset);
                if (!laihost_inb || !laihost_inw || !laihost_ind)
                    lai_panic("lai_perform_read: The laihost_in{b,w,d} functions need to be "
                              "implemented to read from Port IO");

                switch (access_size) {
                    case 8:
                        value = laihost_inb(opregion->op_base + offset);
                        break;
                    case 16:
                        value = laihost_inw(opregion->op_base + offset);
                        break;
                    case 32:
                        value = laihost_ind(opregion->op_base + offset);
                        break;
                    default:
                        lai_panic("invalid access size");
                }
                break;
            }
            case ACPI_OPREGION_PCI: {
                uint8_t slot = (uint8_t)(adr >> 16);
                uint8_t fun = (uint8_t)(adr & 0xFF);
                if (instance->trace & LAI_TRACE_IO)
                    lai_debug("lai_perform_read: %lu-bit read from PCI config of "
                              "%04lx:%02lx:%02x.%02x at %lx",
                              access_size, seg, bbn, slot, fun, opregion->op_base + offset);
                if (!laihost_pci_readb || !laihost_pci_readw || !laihost_pci_readd)
                    lai_panic("lai_perform_read: The laihost_pci_read{b,w,d} functions need to be "
                              "implemented to read from PCI Config Space");

                switch (access_size) {
                    case 8:
                        value = laihost_pci_readb(seg, bbn, slot, fun, opregion->op_base + offset);
                        break;
                    case 16:
                        value = laihost_pci_readw(seg, bbn, slot, fun, opregion->op_base + offset);
                        break;
                    case 32:
                        value = laihost_pci_readd(seg, bbn, slot, fun, opregion->op_base + offset);
                        break;
                    default:
                        lai_panic("invalid access size");
                }
            }
        }
    }

    return value;
}

static void lai_perform_write(lai_nsnode_t *opregion, size_t access_size, size_t offset,
                              uint64_t seg, uint64_t bbn, uint64_t adr, uint64_t value) {
    struct lai_instance *instance = lai_current_instance();
    if (opregion->op_override) {
        if (instance->trace & LAI_TRACE_IO)
            lai_debug("lai_perform_write: %lu-bit write of %lx to overridden opregion at %lx "
                      "(address space %02u)",
                      access_size, opregion->op_base + offset, value, opregion->op_address_space);
        switch (access_size) {
            case 8:
                opregion->op_override->writeb(opregion->op_base + offset, value,
                                              opregion->op_userptr);
                break;
            case 16:
                opregion->op_override->writew(opregion->op_base + offset, value,
                                              opregion->op_userptr);
                break;
            case 32:
                opregion->op_override->writed(opregion->op_base + offset, value,
                                              opregion->op_userptr);
                break;
            case 64:
                opregion->op_override->writeq(opregion->op_base + offset, value,
                                              opregion->op_userptr);
                break;
            default:
                lai_panic("invalid access size");
        }
    } else {
        switch (opregion->op_address_space) {
            case ACPI_OPREGION_MEMORY: {
                if (instance->trace & LAI_TRACE_IO)
                    lai_debug("lai_perform_write: %lu-bit write of %lx to MMIO at %lx", access_size,
                              value, opregion->op_base + offset);
                if ((opregion->op_base + offset) & ((access_size / 8) - 1))
                    lai_warn("lai_perform_write: Unaligned %lu-bit write of %lx to MMIO at %lx",
                             access_size, value, opregion->op_base + offset);
                if (!laihost_map)
                    lai_panic(
                        "lai_perform_write: laihost_map needs to be implemented to write to MMIO");

                void *mmio = laihost_map(opregion->op_base + offset, access_size / 8);
                switch (access_size) {
                    case 8:
                        (*(volatile mmio8_t *)mmio) = value;
                        break;
                    case 16:
                        (*(volatile mmio16_t *)mmio) = value;
                        break;
                    case 32:
                        (*(volatile mmio32_t *)mmio) = value;
                        break;
                    case 64:
                        (*(volatile mmio64_t *)mmio) = value;
                        break;
                    default:
                        lai_panic("invalid access size");
                }
                break;
            }
            case ACPI_OPREGION_IO: {
                if (instance->trace & LAI_TRACE_IO)
                    lai_debug("lai_perform_write: %lu-bit write of %lx to I/O port at %lx",
                              access_size, value, opregion->op_base + offset);
                if (!laihost_outb || !laihost_inw || !laihost_outd)
                    lai_panic("lai_perform_write: The laihost_out{b,w,d} functions need to be "
                              "implemented to write to Port IO");

                switch (access_size) {
                    case 8:
                        laihost_outb(opregion->op_base + offset, value);
                        break;
                    case 16:
                        laihost_outw(opregion->op_base + offset, value);
                        break;
                    case 32:
                        laihost_outd(opregion->op_base + offset, value);
                        break;
                    default:
                        lai_panic("invalid access size");
                }
                break;
            }
            case ACPI_OPREGION_PCI: {
                uint8_t slot = (uint8_t)(adr >> 16);
                uint8_t fun = (uint8_t)(adr & 0xFF);
                if (instance->trace & LAI_TRACE_IO)
                    lai_debug("lai_perform_write: %lu-bit write of %lx to PCI config of "
                              "%04lx:%02lx:%02x.%02x at %lx",
                              access_size, value, seg, bbn, slot, fun, opregion->op_base + offset);
                if (!laihost_pci_writeb || !laihost_pci_writew || !laihost_pci_writed)
                    lai_panic("lai_perform_write: The laihost_pci_write{b,w,d} functions need to "
                              "be implemented to write to PCI Config Space");

                switch (access_size) {
                    case 8:
                        laihost_pci_writeb(seg, bbn, slot, fun, opregion->op_base + offset, value);
                        break;
                    case 16:
                        laihost_pci_writew(seg, bbn, slot, fun, opregion->op_base + offset, value);
                        break;
                    case 32:
                        laihost_pci_writed(seg, bbn, slot, fun, opregion->op_base + offset, value);
                        break;
                    default:
                        lai_panic("invalid access size");
                }
            }
        }
    }
}

void lai_read_field_internal(uint8_t *destination, lai_nsnode_t *field) {
    lai_nsnode_t *opregion = field->fld_region_node;

    size_t access_size = lai_calculate_access_width(field);

    uint64_t offset = (field->fld_offset & ~(access_size - 1)) / 8;

    uint64_t seg = 0; // When _SEG is not present, we default to Segment Group 0
    uint64_t bbn = 0; // When _BBN is not present, we assume PCI bus 0.
    uint64_t adr = 0; // When _ADR is not present, again, default to zero.

    if (opregion->op_address_space == ACPI_OPREGION_PCI)
        lai_get_pci_params(opregion, &seg, &bbn, &adr);

    size_t progress = 0;
    while (progress < field->fld_size) {
        uint64_t bit_offset = (field->fld_offset + progress) & (access_size - 1);
        size_t access_bits = LAI_MIN(field->fld_size - progress, access_size - bit_offset);
        uint64_t mask = (UINT64_C(1) << access_bits) - 1ull;

        uint64_t value = lai_perform_read(opregion, access_size, offset, seg, bbn, adr);

        value = (value >> bit_offset) & mask;

        lai_buffer_put_at(destination, value, progress, access_bits);

        progress += access_bits;
        offset += access_size / 8;
    }
}

void lai_write_field_internal(uint8_t *source, lai_nsnode_t *field) {
    lai_nsnode_t *opregion = field->fld_region_node;

    size_t access_size = lai_calculate_access_width(field);

    uint64_t offset = (field->fld_offset & ~(access_size - 1)) / 8;

    uint64_t seg = 0; // When _SEG is not present, we default to Segment Group 0
    uint64_t bbn = 0; // When _BBN is not present, we assume PCI bus 0.
    uint64_t adr = 0; // When _ADR is not present, again, default to zero.

    if (opregion->op_address_space == ACPI_OPREGION_PCI)
        lai_get_pci_params(opregion, &seg, &bbn, &adr);

    size_t progress = 0;
    while (progress < field->fld_size) {
        uint64_t bit_offset = (field->fld_offset + progress) & (access_size - 1);
        size_t access_bits = LAI_MIN(field->fld_size - progress, access_size - bit_offset);
        size_t mask = ((UINT64_C(1) << access_bits) - 1) << bit_offset;

        size_t write_flag = (field->fld_flags >> 5) & 0x0F;

        uint64_t value;
        if (write_flag == FIELD_PRESERVE)
            value = lai_perform_read(opregion, access_size, offset, seg, bbn, adr);
        else if (write_flag == FIELD_WRITE_ONES)
            value = 0xFFFFFFFFFFFFFFFF;
        else if (write_flag == FIELD_WRITE_ZEROES)
            value = 0;
        else
            lai_panic("Invalid field write flag");

        value &= ~mask;

        uint64_t new_val = lai_buffer_get_at(source, progress, access_bits);
        value |= (new_val << bit_offset) & mask;

        lai_perform_write(opregion, access_size, offset, seg, bbn, adr, value);

        progress += access_bits;
        offset += access_size / 8;
    }
}

void lai_read_field(lai_variable_t *destination, lai_nsnode_t *field) {
    uint64_t bytes = (field->fld_size + 7) / 8;
    LAI_CLEANUP_VAR lai_variable_t var = LAI_VAR_INITIALIZER;

    if (bytes > 8) {
        lai_create_buffer(&var, bytes);
        lai_read_field_internal(var.buffer_ptr->content, field);
    } else {
        uint8_t buf[bytes];
        memset(buf, 0, bytes);
        lai_read_field_internal(buf, field);

        uint64_t value = 0;
        for (size_t i = 0; i < bytes; i++) {
            value |= (uint64_t)buf[i] << (i * 8);
        }

        var.type = LAI_INTEGER;
        var.integer = value;
    }

    lai_var_move(destination, &var);
}

void lai_write_field(lai_nsnode_t *field, lai_variable_t *source) {
    if (source->type == LAI_BUFFER) {
        lai_write_field_internal(source->buffer_ptr->content, field);
    } else if (source->type == LAI_INTEGER) {
        uint8_t buf[8];
        memset(buf, 0, 8);

        for (size_t i = 0; i < 8; i++) {
            buf[i] = (source->integer >> (i * 8)) & 0xFF;
        }

        lai_write_field_internal(buf, field);
    } else {
        lai_panic("Invalid variable type %u in lai_write_field", source->type);
    }
}

/* FIXME: IndexField I/O is broken, as it needs to write to the index
 *        to change the offset. Take care of that for that in
 *        lai_{read,write}_field_internal? */

void lai_read_indexfield(lai_variable_t *dest, lai_nsnode_t *idxf) {
    lai_nsnode_t *index_field = idxf->idxf_index_node;
    lai_nsnode_t *data_field = idxf->idxf_data_node;

    lai_variable_t index = {0};
    index.type = LAI_INTEGER;
    index.integer = idxf->idxf_offset / 8; // Always byte-aligned.

    lai_write_field(index_field, &index); // Write index register.
    lai_read_field(dest, data_field); // Read data register.
}

void lai_write_indexfield(lai_nsnode_t *idxf, lai_variable_t *src) {
    lai_nsnode_t *index_field = idxf->idxf_index_node;
    lai_nsnode_t *data_field = idxf->idxf_data_node;

    lai_variable_t index = {0};
    index.type = LAI_INTEGER;
    index.integer = idxf->idxf_offset / 8; // Always byte-aligned.

    lai_write_field(index_field, &index); // Write index register.
    lai_write_field(data_field, src); // Write data register.
}

void lai_read_opregion(lai_variable_t *destination, lai_nsnode_t *field) {
    if (field->type == LAI_NAMESPACE_FIELD)
        lai_read_field(destination, field);
    else if (field->type == LAI_NAMESPACE_INDEXFIELD)
        lai_read_indexfield(destination, field);
    else
        lai_panic("undefined field read: %s", lai_stringify_node_path(field));
}

void lai_write_opregion(lai_nsnode_t *field, lai_variable_t *source) {
    if (field->type == LAI_NAMESPACE_FIELD)
        lai_write_field(field, source);
    else if (field->type == LAI_NAMESPACE_INDEXFIELD)
        lai_write_indexfield(field, source);
    else
        lai_panic("undefined field write: %s", lai_stringify_node_path(field));
}
