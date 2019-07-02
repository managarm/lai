
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
 */

/* ACPI OperationRegion Implementation */
/* OperationRegions allow ACPI's AML to access I/O ports, system memory, system
 * CMOS, PCI config, and other hardware used for I/O with the chipset. */

// TODO: Implement support for the embedded controller, because some hardware uses it.

#include <lai/core.h>
#include "aml_opcodes.h"
#include "libc.h"
#include "opregion.h"

void lai_read_field(lai_object_t *, lai_nsnode_t *);
void lai_write_field(lai_nsnode_t *, lai_object_t *);
void lai_read_indexfield(lai_object_t *, lai_nsnode_t *);
void lai_write_indexfield(lai_nsnode_t *, lai_object_t *);

void lai_read_opregion(lai_object_t *destination, lai_nsnode_t *field) {
    if (field->type == LAI_NAMESPACE_FIELD)
        return lai_read_field(destination, field);

    else if (field->type == LAI_NAMESPACE_INDEXFIELD)
        return lai_read_indexfield(destination, field);

    lai_panic("undefined field read: %s", lai_stringify_node_path(field));
}

void lai_write_opregion(lai_nsnode_t *field, lai_object_t *source) {
    if (field->type == LAI_NAMESPACE_FIELD)
        return lai_write_field(field, source);

    else if (field->type == LAI_NAMESPACE_INDEXFIELD)
        return lai_write_indexfield(field, source);

    lai_panic("undefined field write: %s", lai_stringify_node_path(field));
}

void lai_read_field(lai_object_t *destination, lai_nsnode_t *field) {
    lai_nsnode_t *opregion = field->fld_region_node;

    uint64_t offset, value, mask;
    size_t bit_offset;

    mask = ((uint64_t)1 << field->fld_size);
    mask--;
    offset = field->fld_offset / 8;
    void *mmio;

    // these are for PCI
    char name[ACPI_MAX_NAME];
    lai_object_t bus_number = {0};
    lai_object_t address_number = {0};
    int eval_status;
    size_t pci_byte_offset;

    if (opregion->op_address_space != OPREGION_PCI) {
        switch (field->fld_flags & 0x0F) {
            case FIELD_BYTE_ACCESS:
                bit_offset = field->fld_offset % 8;
                break;

            case FIELD_WORD_ACCESS:
                bit_offset = field->fld_offset % 16;
                offset &= (~1);        // clear lowest bit
                break;

            case FIELD_DWORD_ACCESS:
            case FIELD_ANY_ACCESS:
                bit_offset = field->fld_offset % 32;
                offset &= (~3);        // clear lowest two bits
                break;

            case FIELD_QWORD_ACCESS:
                bit_offset = field->fld_offset % 64;
                offset &= (~7);        // clear lowest three bits
                break;

            default:
                lai_panic("undefined field flags 0x%02x: %s", field->fld_flags,
                          lai_stringify_node_path(field));
        }
    } else {
        bit_offset = field->fld_offset % 32;
        pci_byte_offset = field->fld_offset % 4;
    }

    // now read from either I/O ports, MMIO, or PCI config
    if (opregion->op_address_space == OPREGION_IO) {
        // I/O port
        switch (field->fld_flags & 0x0F) {
            case FIELD_BYTE_ACCESS:
                if (!laihost_inb)
                    lai_panic("host does not provide port I/O functions");
                value = (uint64_t)laihost_inb(opregion->op_base + offset) >> bit_offset;
                break;
            case FIELD_WORD_ACCESS:
                if (!laihost_inw)
                    lai_panic("host does not provide port I/O functions");
                value = (uint64_t)laihost_inw(opregion->op_base + offset) >> bit_offset;
                break;
            case FIELD_DWORD_ACCESS:
            case FIELD_ANY_ACCESS:
                if (!laihost_ind)
                    lai_panic("host does not provide port I/O functions");
                value = (uint64_t)laihost_ind(opregion->op_base + offset) >> bit_offset;
                break;
            default:
                lai_panic("undefined field flags 0x%02X: %s", field->fld_flags,
                          lai_stringify_node_path(field));
        }
    } else if (opregion->op_address_space == OPREGION_MEMORY) {
        // Memory-mapped I/O
        if (!laihost_map)
            lai_panic("host does not provide memory mapping functions");
        mmio = laihost_map(opregion->op_base + offset, 8);
        uint8_t *mmio_byte;
        uint16_t *mmio_word;
        uint32_t *mmio_dword;
        uint64_t *mmio_qword;

        switch (field->fld_flags & 0x0F) {
            case FIELD_BYTE_ACCESS:
                mmio_byte = (uint8_t*)mmio;
                value = (uint64_t)mmio_byte[0] >> bit_offset;
                break;
            case FIELD_WORD_ACCESS:
                mmio_word = (uint16_t*)mmio;
                value = (uint64_t)mmio_word[0] >> bit_offset;
                break;
            case FIELD_DWORD_ACCESS:
            case FIELD_ANY_ACCESS:
                mmio_dword = (uint32_t*)mmio;
                value = (uint64_t)mmio_dword[0] >> bit_offset;
                break;
            case FIELD_QWORD_ACCESS:
                mmio_qword = (uint64_t*)mmio;
                value = mmio_qword[0] >> bit_offset;
                break;
            default:
                lai_panic("undefined field flags 0x%02X: %s", field->fld_flags,
                          lai_stringify_node_path(field));
        }
    } else if (opregion->op_address_space == OPREGION_PCI) {
        // PCI bus number is in the _BBN object
        lai_strcpy(name, opregion->fullpath);
        lai_strcpy(name + lai_strlen(name) - 4, "_BBN");
        eval_status = lai_eval(&bus_number, name);

        // when the _BBN object is not present, we assume PCI bus 0
        if (eval_status) {
            bus_number.type = LAI_INTEGER;
            bus_number.integer = 0;
        }

        // device slot/function is in the _ADR object
        lai_strcpy(name, opregion->fullpath);
        lai_strcpy(name + lai_strlen(name) - 4, "_ADR");
        eval_status = lai_eval(&address_number, name);

        // when this is not present, again default to zero
        if (eval_status) {
            address_number.type = LAI_INTEGER;
            address_number.type = 0;
        }

        if (!laihost_pci_read)
            lai_panic("host does not provide PCI access functions");
        value = laihost_pci_read((uint8_t)bus_number.integer,
                                 (uint8_t)(address_number.integer >> 16) & 0xFF,
                                 (uint8_t)(address_number.integer & 0xFF),
                                 (offset & 0xFFFC) + opregion->op_base);

       value >>= bit_offset;
    } else {
        lai_panic("undefined opregion address space: %d", opregion->op_address_space);
    }

    destination->type = LAI_INTEGER;
    destination->integer = value & mask;
}

void lai_write_field(lai_nsnode_t *field, lai_object_t *source) {
    // determine the flags we need in order to write
    lai_nsnode_t *opregion = field->fld_region_node;

    uint64_t offset, value, mask;
    size_t bit_offset;

    mask = ((uint64_t)1 << field->fld_size);
    mask--;
    offset = field->fld_offset / 8;
    void *mmio;

    // these are for PCI
    char name[ACPI_MAX_NAME];
    lai_object_t bus_number = {0};
    lai_object_t address_number = {0};
    int eval_status;
    size_t pci_byte_offset;

    if (opregion->op_address_space != OPREGION_PCI) {
        switch (field->fld_flags & 0x0F) {
            case FIELD_BYTE_ACCESS:
                bit_offset = field->fld_offset % 8;
                break;

            case FIELD_WORD_ACCESS:
                bit_offset = field->fld_offset % 16;
                offset &= (~1);        // clear lowest bit
                break;

            case FIELD_DWORD_ACCESS:
            case FIELD_ANY_ACCESS:
                bit_offset = field->fld_offset % 32;
                offset &= (~3);        // clear lowest two bits
                break;

            case FIELD_QWORD_ACCESS:
                bit_offset = field->fld_offset % 64;
                offset &= (~7);        // clear lowest three bits
                break;

            default:
                lai_panic("undefined field flags 0x%02X: %s", field->fld_flags,
                          lai_stringify_node_path(field));
        }
    } else {
        bit_offset = field->fld_offset % 32;
        pci_byte_offset = field->fld_offset % 4;
    }

    // read from the field
    if (opregion->op_address_space == OPREGION_IO) {
        // I/O port
        switch (field->fld_flags & 0x0F) {
            case FIELD_BYTE_ACCESS:
                if (!laihost_inb)
                    lai_panic("host does not provide port I/O functions");
                value = (uint64_t)laihost_inb(opregion->op_base + offset);
                break;
            case FIELD_WORD_ACCESS:
                if (!laihost_inw)
                    lai_panic("host does not provide port I/O functions");
                value = (uint64_t)laihost_inw(opregion->op_base + offset);
                break;
            case FIELD_DWORD_ACCESS:
            case FIELD_ANY_ACCESS:
                if (!laihost_ind)
                    lai_panic("host does not provide port I/O functions");
                value = (uint64_t)laihost_ind(opregion->op_base + offset);
                break;
            default:
                lai_panic("undefined field flags 0x%02X: %s", field->fld_flags,
                          lai_stringify_node_path(field));
        }
    } else if (opregion->op_address_space == OPREGION_MEMORY) {
        // Memory-mapped I/O
        if (!laihost_map)
            lai_panic("host does not provide memory mapping functions");
        mmio = laihost_map(opregion->op_base + offset, 8);
        uint8_t *mmio_byte;
        uint16_t *mmio_word;
        uint32_t *mmio_dword;
        uint64_t *mmio_qword;

        switch (field->fld_flags & 0x0F) {
            case FIELD_BYTE_ACCESS:
                mmio_byte = (uint8_t *)mmio;
                value = (uint64_t)mmio_byte[0];
                break;
            case FIELD_WORD_ACCESS:
                mmio_word = (uint16_t *)mmio;
                value = (uint64_t)mmio_word[0];
                break;
            case FIELD_DWORD_ACCESS:
            case FIELD_ANY_ACCESS:
                mmio_dword = (uint32_t *)mmio;
                value = (uint64_t)mmio_dword[0];
                break;
            case FIELD_QWORD_ACCESS:
                mmio_qword = (uint64_t *)mmio;
                value = mmio_qword[0];
                break;
            default:
                lai_panic("undefined field flags 0x%02X: %s", field->fld_flags,
                          lai_stringify_node_path(field));
        }
    } else if (opregion->op_address_space == OPREGION_PCI) {
        // PCI bus number is in the _BBN object
        lai_strcpy(name, opregion->fullpath);
        lai_strcpy(name + lai_strlen(name) - 4, "_BBN");
        eval_status = lai_eval(&bus_number, name);

        // when the _BBN object is not present, we assume PCI bus 0
        if (eval_status) {
            bus_number.type = LAI_INTEGER;
            bus_number.integer = 0;
        }

        // device slot/function is in the _ADR object
        lai_strcpy(name, opregion->fullpath);
        lai_strcpy(name + lai_strlen(name) - 4, "_ADR");
        eval_status = lai_eval(&address_number, name);

        // when this is not present, again default to zero
        if (eval_status) {
            address_number.type = LAI_INTEGER;
            address_number.type = 0;
        }

        if (!laihost_pci_read)
            lai_panic("host does not provide PCI access functions");
        value = laihost_pci_read((uint8_t)bus_number.integer,
                                 (uint8_t)(address_number.integer >> 16) & 0xFF,
                                 (uint8_t)(address_number.integer & 0xFF),
                                 (offset & 0xFFFC) + opregion->op_base);
    } else {
        lai_panic("undefined opregion address space: %d", opregion->op_address_space);
    }

    // now determine how we need to write to the field
    if (((field->fld_flags >> 5) & 0x0F) == FIELD_PRESERVE) {
        value &= ~(mask << bit_offset);
        value |= (source->integer << bit_offset);
    } else if (((field->fld_flags >> 5) & 0x0F) == FIELD_WRITE_ONES) {
        value = 0xFFFFFFFFFFFFFFFF;
        value &= ~(mask << bit_offset);
        value |= (source->integer << bit_offset);
    } else {
        value = 0;
        value |= (source->integer << bit_offset);
    }

    // finally, write to the field
    if (opregion->op_address_space == OPREGION_IO) {
        // I/O port
        switch (field->fld_flags & 0x0F) {
            case FIELD_BYTE_ACCESS:
                laihost_outb(opregion->op_base + offset, (uint8_t)value);
                break;
            case FIELD_WORD_ACCESS:
                laihost_outw(opregion->op_base + offset, (uint16_t)value);
                //lai_debug("wrote 0x%X to I/O port 0x%X", (uint16_t)value, opregion->op_base + offset);
                break;
            case FIELD_DWORD_ACCESS:
            case FIELD_ANY_ACCESS:
                laihost_outd(opregion->op_base + offset, (uint32_t)value);
                break;
            default:
                lai_panic("undefined field flags 0x%02X: %s", field->fld_flags,
                          lai_stringify_node_path(field));
        }

        // iowait() equivalent
        laihost_outb(0x80, 0x00);
        laihost_outb(0x80, 0x00);
    } else if (opregion->op_address_space == OPREGION_MEMORY) {
        // Memory-mapped I/O
        if (!laihost_map)
            lai_panic("host does not provide memory mapping functions");
        mmio = laihost_map(opregion->op_base + offset, 8);
        uint8_t *mmio_byte;
        uint16_t *mmio_word;
        uint32_t *mmio_dword;
        uint64_t *mmio_qword;

        switch (field->fld_flags & 0x0F) {
            case FIELD_BYTE_ACCESS:
                mmio_byte = (uint8_t*)mmio;
                mmio_byte[0] = (uint8_t)value;
                break;
            case FIELD_WORD_ACCESS:
                mmio_word = (uint16_t*)mmio;
                mmio_word[0] = (uint16_t)value;
                break;
            case FIELD_DWORD_ACCESS:
            case FIELD_ANY_ACCESS:
                mmio_dword = (uint32_t*)mmio;
                mmio_dword[0] = (uint32_t)value;
                break;
            case FIELD_QWORD_ACCESS:
                mmio_qword = (uint64_t*)mmio;
                mmio_qword[0] = value;
                //lai_debug("wrote 0x%lX to MMIO address 0x%lX", value, opregion->op_base + offset);
                break;
            default:
                lai_panic("undefined field flags 0x%02X", field->fld_flags);
        }
    } else if (opregion->op_address_space == OPREGION_PCI) {
        if (!laihost_pci_write)
            lai_panic("host does not provide PCI access functions");
        laihost_pci_write((uint8_t)bus_number.integer,
                          (uint8_t)(address_number.integer >> 16) & 0xFF,
                          (uint8_t)(address_number.integer & 0xFF),
                          (offset & 0xFFFC) + opregion->op_base, (uint32_t)value);
    } else {
        lai_panic("undefined opregion address space: %d", opregion->op_address_space);
    }
}

void lai_read_indexfield(lai_object_t *dest, lai_nsnode_t *idxf) {
    lai_nsnode_t *index_field = idxf->idxf_index_node;
    lai_nsnode_t *data_field = idxf->idxf_data_node;

    lai_object_t index = {0};
    index.type = LAI_INTEGER;
    index.integer = idxf->idxf_offset / 8; // Always byte-aligned.

    lai_write_field(index_field, &index); // Write index register.
    lai_read_field(dest, data_field); // Read data register.
}

void lai_write_indexfield(lai_nsnode_t *idxf, lai_object_t *src) {
    lai_nsnode_t *index_field = idxf->idxf_index_node;
    lai_nsnode_t *data_field = idxf->idxf_data_node;

    lai_object_t index = {0};
    index.type = LAI_INTEGER;
    index.integer = idxf->idxf_offset / 8; // Always byte-aligned.

    lai_write_field(index_field, &index); // Write index register.
    lai_write_field(data_field, src); // Write data register.
}
