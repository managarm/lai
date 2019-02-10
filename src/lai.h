
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 by Omar Muhamed
 */

#pragma once

#include <lai_system.h>
#include "aml_opcodes.h"

#define ACPI_MAX_NAME            64
#define ACPI_MAX_RESOURCES        512

#define ACPI_GAS_MMIO            0
#define ACPI_GAS_IO            1
#define ACPI_GAS_PCI            2

#define ACPI_MAX_PACKAGE_ENTRIES    256    // for Package() because the size is 8 bits, VarPackage() is unlimited

#define ACPI_NAMESPACE_NAME        1
#define ACPI_NAMESPACE_ALIAS        2
#define ACPI_NAMESPACE_SCOPE        3
#define ACPI_NAMESPACE_FIELD        4
#define ACPI_NAMESPACE_METHOD        5
#define ACPI_NAMESPACE_DEVICE        6
#define ACPI_NAMESPACE_INDEXFIELD    7
#define ACPI_NAMESPACE_MUTEX        8
#define ACPI_NAMESPACE_PROCESSOR    9
#define ACPI_NAMESPACE_BUFFER_FIELD    10
#define ACPI_NAMESPACE_THERMALZONE    11

#define ACPI_INTEGER            1
#define ACPI_STRING            2
#define ACPI_PACKAGE            3
#define ACPI_BUFFER            4
#define ACPI_NAME            5

// Device _STA object
#define ACPI_STA_PRESENT        0x01
#define ACPI_STA_ENABLED        0x02
#define ACPI_STA_VISIBLE        0x04
#define ACPI_STA_FUNCTION        0x08
#define ACPI_STA_BATTERY        0x10

// FADT Event/Status Fields
#define ACPI_TIMER            0x0001
#define ACPI_BUSMASTER            0x0010
#define ACPI_GLOBAL            0x0020
#define ACPI_POWER_BUTTON        0x0100
#define ACPI_SLEEP_BUTTON        0x0200
#define ACPI_RTC_ALARM            0x0400
#define ACPI_PCIE_WAKE            0x4000
#define ACPI_WAKE            0x8000

// FADT Control Block
#define ACPI_ENABLED            0x0001
#define ACPI_SLEEP            0x2000

// Parsing Resource Templates
#define ACPI_RESOURCE_MEMORY        1
#define ACPI_RESOURCE_IO        2
#define ACPI_RESOURCE_IRQ        3

// IRQ Flags
#define ACPI_IRQ_LEVEL            0x00
#define ACPI_IRQ_EDGE            0x01
#define ACPI_IRQ_ACTIVE_HIGH        0x00
#define ACPI_IRQ_ACTIVE_LOW        0x08
#define ACPI_IRQ_EXCLUSIVE        0x00
#define ACPI_IRQ_SHARED            0x10
#define ACPI_IRQ_NO_WAKE        0x00
#define ACPI_IRQ_WAKE            0x20

typedef struct acpi_rsdp_t
{
    char signature[8];
    uint8_t checksum;
    char oem[6];
    uint8_t revision;
    uint32_t rsdt;
    uint32_t length;
    uint64_t xsdt;
    uint8_t extended_checksum;
}__attribute__((packed)) acpi_rsdp_t;

typedef struct acpi_header_t
{
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem[6];
    char oem_table[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
}__attribute__((packed)) acpi_header_t;

typedef struct acpi_rsdt_t
{
    acpi_header_t header;
    uint32_t tables[];
}__attribute__((packed)) acpi_rsdt_t;

typedef struct acpi_gas_t
{
    uint8_t address_space;
    uint8_t bit_width;
    uint8_t bit_offset;
    uint8_t access_size;
    uint64_t base;
}__attribute__((packed)) acpi_gas_t;

typedef struct acpi_fadt_t
{
    acpi_header_t header;
    uint32_t firmware_control;
    uint32_t dsdt;        // pointer to dsdt

    uint8_t reserved;

    uint8_t profile;
    uint16_t sci_irq;
    uint32_t smi_command_port;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_control;
    uint32_t pm1a_event_block;
    uint32_t pm1b_event_block;
    uint32_t pm1a_control_block;
    uint32_t pm1b_control_block;
    uint32_t pm2_control_block;
    uint32_t pm_timer_block;
    uint32_t gpe0_block;
    uint32_t gpe1_block;
    uint8_t pm1_event_length;
    uint8_t pm1_control_length;
    uint8_t pm2_control_length;
    uint8_t pm_timer_length;
    uint8_t gpe0_length;
    uint8_t gpe1_length;
    uint8_t gpe1_base;
    uint8_t cstate_control;
    uint16_t worst_c2_latency;
    uint16_t worst_c3_latency;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;

    // cmos registers
    uint8_t day_alarm;
    uint8_t month_alarm;
    uint8_t century;

    // ACPI 2.0 fields
    uint16_t boot_flags;
    uint8_t reserved2;
    uint32_t flags;

    acpi_gas_t reset_register;
    uint8_t reset_command;
    uint8_t reserved3[3];

    uint64_t x_firmware_control;
    uint64_t x_dsdt;

    acpi_gas_t x_pm1a_event_block;
    acpi_gas_t x_pm1b_event_block;
    acpi_gas_t x_pm1a_control_block;
    acpi_gas_t x_pm1b_control_block;
    acpi_gas_t x_pm2_control_block;
    acpi_gas_t x_pm_timer_block;
    acpi_gas_t x_gpe0_block;
    acpi_gas_t x_gpe1_block;
}__attribute__((packed)) acpi_fadt_t;

typedef struct acpi_aml_t        // AML tables, DSDT and SSDT
{
    acpi_header_t header;
    uint8_t data[];
}__attribute__((packed)) acpi_aml_t;

typedef struct acpi_object_t
{
    int type;
    uint64_t integer;        // for Name()
    char *string;            // for Name()

    int package_size;        // for Package(), size in entries
    struct acpi_object_t *package;    // for Package(), actual entries

    size_t buffer_size;        // for Buffer(), size in bytes
    void *buffer;            // for Buffer(), actual bytes

    char name[ACPI_MAX_NAME];    // for Name References
} acpi_object_t;

typedef struct acpi_nsnode_t
{
    char path[ACPI_MAX_NAME];    // full path of object
    int type;
    void *pointer;            // valid for scopes, methods, etc.
    size_t size;            // valid for scopes, methods, etc.

    char alias[ACPI_MAX_NAME];    // for Alias() only
    acpi_object_t object;        // for Name()

    uint8_t op_address_space;    // for OpRegions only
    uint64_t op_base;        // for OpRegions only
    uint64_t op_length;        // for OpRegions only

    uint64_t field_offset;        // for Fields only, in bits
    uint8_t field_size;        // for Fields only, in bits
    uint8_t field_flags;        // for Fields only
    char field_opregion[ACPI_MAX_NAME];    // for Fields only

    uint8_t method_flags;        // for Methods only, includes ARG_COUNT in lowest three bits
    // Allows the OS to override methods. Mainly useful for _OSI, _OS and _REV.
    int (*method_override)(acpi_object_t *args, acpi_object_t *result);

    uint64_t indexfield_offset;    // for IndexFields, in bits
    char indexfield_index[ACPI_MAX_NAME];    // for IndexFields
    char indexfield_data[ACPI_MAX_NAME];    // for IndexFields
    uint8_t indexfield_flags;    // for IndexFields
    uint8_t indexfield_size;    // for IndexFields

    acpi_lock_t mutex;        // for Mutex

    uint8_t cpu_id;            // for Processor

    char buffer[ACPI_MAX_NAME];        // for Buffer field
    uint64_t buffer_offset;        // for Buffer field, in bits
    uint64_t buffer_size;        // for Buffer field, in bits
} acpi_nsnode_t;

typedef struct acpi_condition_t
{
    acpi_object_t predicate;
    size_t predicate_size;
    size_t pkgsize;
    size_t end;
} acpi_condition_t;

#define LAI_POPULATE_CONTEXT_STACKITEM 1
#define LAI_METHOD_CONTEXT_STACKITEM 2
#define LAI_LOOP_STACKITEM 3
#define LAI_COND_STACKITEM 4
#define LAI_OP_STACKITEM 5
#define LAI_NOWRITE_OP_STACKITEM 6
// This implements acpi_eval_operand(). // TODO: Eventually remove
// acpi_eval_operand() by moving all parsing functionality into acpi_exec_run().
#define LAI_EVALOPERAND_STACKITEM 10

typedef struct acpi_stackitem_ {
    int kind;
    int opstack_frame;
    union {
        struct {
            acpi_nsnode_t *ctx_handle;
        };
        struct {
            int loop_pred; // Loop predicate PC.
            int loop_end; // End of loop PC.
        };
        struct {
            int cond_taken; // Whether the conditional was true or not.
            int cond_end; // End of conditional PC.
        };
        struct {
            int op_opcode;
            int op_num_operands;
            int op_want_result;
        };
    };
} acpi_stackitem_t;

typedef struct acpi_state_t
{
    int pc;
    int limit;
    acpi_object_t retvalue;
    acpi_object_t arg[7];
    acpi_object_t local[8];

    // Stack to track the current execution state.
    int stack_ptr;
    int opstack_ptr;
    acpi_stackitem_t stack[16];
    acpi_object_t opstack[16];
    int context_ptr; // Index of the last CONTEXT_STACKITEM.
} acpi_state_t;

void acpi_init_state(acpi_state_t *);
void acpi_finalize_state(acpi_state_t *);

__attribute__((always_inline))
inline acpi_object_t *acpi_retvalue(acpi_state_t *state) {
    return &state->retvalue;
}

__attribute__((always_inline))
inline acpi_object_t *acpi_arg(acpi_state_t *state, int n) {
    return &state->arg[n];
}

typedef struct acpi_resource_t
{
    uint8_t type;

    uint64_t base;            // valid for everything

    uint64_t length;        // valid for I/O and MMIO

    uint8_t address_space;        // these are valid --
    uint8_t bit_width;        // -- only for --
    uint8_t bit_offset;        // -- generic registers

    uint8_t irq_flags;        // valid for IRQs
} acpi_resource_t;

typedef struct acpi_small_irq_t
{
    uint8_t id;
    uint16_t irq_mask;
    uint8_t config;
}__attribute__((packed)) acpi_small_irq_t;

typedef struct acpi_large_irq_t
{
    uint8_t id;
    uint16_t size;
    uint8_t config;
    uint8_t length;
    uint32_t irq;
}__attribute__((packed)) acpi_large_irq_t;

acpi_fadt_t *acpi_fadt;
acpi_aml_t *acpi_dsdt;
size_t acpi_ns_size;
volatile uint16_t acpi_last_event;

// OS-specific functions
void *acpi_scan(char *, size_t);
void *acpi_memcpy(void *, const void *, size_t);
void *acpi_memmove(void *, const void *, size_t);
void *acpi_malloc(size_t);
void *acpi_calloc(size_t, size_t);
void *acpi_realloc(void *, size_t);
void acpi_free(void *);
void *acpi_map(size_t, size_t);
char *acpi_strcpy(char *, const char *);
size_t acpi_strlen(const char *);
void *acpi_memset(void *, int, size_t);
int acpi_strcmp(const char *, const char *);
int acpi_memcmp(const char *, const char *, size_t);
void acpi_outb(uint16_t, uint8_t);
void acpi_outw(uint16_t, uint16_t);
void acpi_outd(uint16_t, uint32_t);
void acpi_pci_write(uint8_t, uint8_t, uint8_t, uint16_t, uint32_t);
uint32_t acpi_pci_read(uint8_t, uint8_t, uint8_t, uint16_t);
uint8_t acpi_inb(uint16_t);
uint16_t acpi_inw(uint16_t);
uint32_t acpi_ind(uint16_t);
void acpi_sleep(uint64_t);

// The remaining of these functions are OS independent!
// ACPI namespace functions
size_t acpins_resolve_path(acpi_nsnode_t *, char *, uint8_t *);
void acpi_create_namespace(void *);
int acpi_is_name(char);
size_t acpi_eval_integer(uint8_t *, uint64_t *);
size_t acpi_parse_pkgsize(uint8_t *, size_t *);
int acpi_eval_package(acpi_object_t *, size_t, acpi_object_t *);
acpi_nsnode_t *acpins_resolve(char *);
acpi_nsnode_t *acpins_get_device(size_t);
acpi_nsnode_t *acpins_get_deviceid(size_t, acpi_object_t *);
acpi_nsnode_t *acpins_enum(char *, size_t);
void acpi_eisaid(acpi_object_t *, char *);
size_t acpi_read_resource(acpi_nsnode_t *, acpi_resource_t *);

// ACPI Control Methods
void acpi_eval_operand(acpi_object_t *, acpi_state_t *, uint8_t *);
int acpi_eval(acpi_object_t *, char *);
void acpi_free_object(acpi_object_t *);
void acpi_move_object(acpi_object_t *, acpi_object_t *);
void acpi_copy_object(acpi_object_t *, acpi_object_t *);
void acpi_write_object(void *, acpi_object_t *, acpi_state_t *);
acpi_nsnode_t *acpi_exec_resolve(char *);
int acpi_populate(acpi_nsnode_t *, void *, size_t, acpi_state_t *);
int acpi_exec_method(acpi_nsnode_t *, acpi_state_t *);
void acpi_read_opregion(acpi_object_t *, acpi_nsnode_t *);
void acpi_write_opregion(acpi_nsnode_t *, acpi_object_t *);
void acpi_exec_name(void *, acpi_nsnode_t *, acpi_state_t *);
void acpi_exec_increment(void *, acpi_state_t *);
void acpi_exec_decrement(void *, acpi_state_t *);
void acpi_exec_sleep(void *, acpi_state_t *);
uint16_t acpi_bswap16(uint16_t);
uint32_t acpi_bswap32(uint32_t);
uint8_t acpi_char_to_hex(char);
size_t acpi_exec_multiply(void *, acpi_state_t *);
void acpi_exec_divide(void *, acpi_state_t *);
void acpi_write_buffer(acpi_nsnode_t *, acpi_object_t *);
void acpi_exec_bytefield(void *, acpi_nsnode_t *, acpi_state_t *);
void acpi_exec_wordfield(void *, acpi_nsnode_t *, acpi_state_t *);
void acpi_exec_dwordfield(void *, acpi_nsnode_t *, acpi_state_t *);

// Generic Functions
int acpi_enable(uint32_t);
int acpi_disable();
uint16_t acpi_read_event();
void acpi_set_event(uint16_t);
int acpi_enter_sleep(uint8_t);
int acpi_pci_route(acpi_resource_t *, uint8_t, uint8_t, uint8_t);


