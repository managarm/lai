
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 by Omar Muhamed
 */

#pragma once

#include <lai_system.h>

#define LAI_STRINGIFY(x) #x
#define LAI_EXPAND_STRINGIFY(x) LAI_STRINGIFY(x)

#define LAI_ENSURE(cond) \
    do { \
        if(!(cond)) \
            lai_panic("assertion failed: " #cond " at " \
                       __FILE__ ":" LAI_EXPAND_STRINGIFY(__LINE__) "\n"); \
    } while(0)

#define ACPI_MAX_NAME            64
#define ACPI_MAX_RESOURCES        512

#define ACPI_GAS_MMIO            0
#define ACPI_GAS_IO            1
#define ACPI_GAS_PCI            2

#define LAI_NAMESPACE_NAME        1
#define LAI_NAMESPACE_ALIAS        2
#define LAI_NAMESPACE_SCOPE        3
#define LAI_NAMESPACE_FIELD        4
#define LAI_NAMESPACE_METHOD        5
#define LAI_NAMESPACE_DEVICE        6
#define LAI_NAMESPACE_INDEXFIELD    7
#define LAI_NAMESPACE_MUTEX        8
#define LAI_NAMESPACE_PROCESSOR    9
#define LAI_NAMESPACE_BUFFER_FIELD    10
#define LAI_NAMESPACE_THERMALZONE    11

// ----------------------------------------------------------------------------
// Data types defined by AML.
// ----------------------------------------------------------------------------
// Value types: integer, string, buffer, package.
#define LAI_INTEGER            1
#define LAI_STRING             2
#define LAI_BUFFER             3
#define LAI_PACKAGE            4
// Handle type: this is used to represent device (and other) namespace nodes.
#define LAI_HANDLE             5
// Reference types: obtained from RefOp() or Index().
#define LAI_STRING_INDEX       6
#define LAI_BUFFER_INDEX       7
#define LAI_PACKAGE_INDEX      8
// ----------------------------------------------------------------------------
// Internal data types of the interpreter.
// ----------------------------------------------------------------------------
// Name types: unresolved names and names of certain objects.
#define LAI_NULL_NAME          9
#define LAI_UNRESOLVED_NAME   10
#define LAI_ARG_NAME          11
#define LAI_LOCAL_NAME        12
// Reference types: references to object storage.
#define LAI_STRING_REFERENCE  13
#define LAI_BUFFER_REFERENCE  14
#define LAI_PACKAGE_REFERENCE 15

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

typedef struct lai_object_t
{
    int type;
    uint64_t integer;        // for Name()
    char *string;            // for Name()

    int package_size;        // for Package(), size in entries
    struct lai_object_t *package;    // for Package(), actual entries

    size_t buffer_size;        // for Buffer(), size in bytes
    void *buffer;            // for Buffer(), actual bytes

    char name[ACPI_MAX_NAME];    // for Name References
    struct lai_nsnode_t *handle;

    int index;
} lai_object_t;

typedef struct lai_nsnode_t
{
    char path[ACPI_MAX_NAME];    // full path of object
    int type;
    void *pointer;            // valid for scopes, methods, etc.
    size_t size;            // valid for scopes, methods, etc.

    char alias[ACPI_MAX_NAME];    // for Alias() only
    lai_object_t object;        // for Name()

    uint8_t op_address_space;    // for OpRegions only
    uint64_t op_base;        // for OpRegions only
    uint64_t op_length;        // for OpRegions only

    uint64_t field_offset;        // for Fields only, in bits
    uint8_t field_size;        // for Fields only, in bits
    uint8_t field_flags;        // for Fields only
    char field_opregion[ACPI_MAX_NAME];    // for Fields only

    uint8_t method_flags;        // for Methods only, includes ARG_COUNT in lowest three bits
    // Allows the OS to override methods. Mainly useful for _OSI, _OS and _REV.
    int (*method_override)(lai_object_t *args, lai_object_t *result);

    uint64_t indexfield_offset;    // for IndexFields, in bits
    char indexfield_index[ACPI_MAX_NAME];    // for IndexFields
    char indexfield_data[ACPI_MAX_NAME];    // for IndexFields
    uint8_t indexfield_flags;    // for IndexFields
    uint8_t indexfield_size;    // for IndexFields

    lai_lock_t mutex;        // for Mutex

    uint8_t cpu_id;            // for Processor

    char buffer[ACPI_MAX_NAME];        // for Buffer field
    uint64_t buffer_offset;        // for Buffer field, in bits
    uint64_t buffer_size;        // for Buffer field, in bits
} lai_nsnode_t;

#define LAI_POPULATE_CONTEXT_STACKITEM 1
#define LAI_METHOD_CONTEXT_STACKITEM 2
#define LAI_LOOP_STACKITEM 3
#define LAI_COND_STACKITEM 4
#define LAI_PKG_INITIALIZER_STACKITEM 5
#define LAI_OP_STACKITEM 6
// This implements lai_eval_operand(). // TODO: Eventually remove
// lai_eval_operand() by moving all parsing functionality into lai_exec_run().
#define LAI_EVALOPERAND_STACKITEM 10

typedef struct lai_stackitem_ {
    int kind;
    int opstack_frame;
    union {
        struct {
            lai_nsnode_t *ctx_handle;
            int ctx_limit;
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
            int pkg_index;
            int pkg_end;
            uint8_t pkg_result_mode;
        };
        struct {
            int op_opcode;
            uint8_t op_arg_modes[8];
            uint8_t op_result_mode;
        };
    };
} lai_stackitem_t;

typedef struct lai_state_t
{
    int pc;
    int limit;
    lai_object_t retvalue;
    lai_object_t arg[7];
    lai_object_t local[8];

    // Stack to track the current execution state.
    int stack_ptr;
    int opstack_ptr;
    lai_stackitem_t stack[16];
    lai_object_t opstack[16];
    int context_ptr; // Index of the last CONTEXT_STACKITEM.
} lai_state_t;

void lai_init_state(lai_state_t *);
void lai_finalize_state(lai_state_t *);

__attribute__((always_inline))
inline lai_object_t *lai_retvalue(lai_state_t *state) {
    return &state->retvalue;
}

__attribute__((always_inline))
inline lai_object_t *lai_arg(lai_state_t *state, int n) {
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

acpi_fadt_t *lai_fadt;
acpi_aml_t *lai_dsdt;
size_t lai_ns_size;
volatile uint16_t lai_last_event;

// OS-specific functions
void *lai_scan(char *, size_t);
void *lai_memcpy(void *, const void *, size_t);
void *lai_memmove(void *, const void *, size_t);
void *lai_malloc(size_t);
void *lai_calloc(size_t, size_t);
void *lai_realloc(void *, size_t);
void lai_free(void *);
void *lai_map(size_t, size_t);
char *lai_strcpy(char *, const char *);
size_t lai_strlen(const char *);
void *lai_memset(void *, int, size_t);
int lai_strcmp(const char *, const char *);
int lai_memcmp(const char *, const char *, size_t);
void lai_outb(uint16_t, uint8_t);
void lai_outw(uint16_t, uint16_t);
void lai_outd(uint16_t, uint32_t);
void lai_pci_write(uint8_t, uint8_t, uint8_t, uint16_t, uint32_t);
uint32_t lai_pci_read(uint8_t, uint8_t, uint8_t, uint16_t);
uint8_t lai_inb(uint16_t);
uint16_t lai_inw(uint16_t);
uint32_t lai_ind(uint16_t);
void lai_sleep(uint64_t);

// The remaining of these functions are OS independent!
// ACPI namespace functions
size_t acpins_resolve_path(lai_nsnode_t *, char *, uint8_t *);
void lai_create_namespace(void *);
int lai_is_name(char);
size_t lai_eval_integer(uint8_t *, uint64_t *);
size_t lai_parse_pkgsize(uint8_t *, size_t *);
int lai_eval_package(lai_object_t *, size_t, lai_object_t *);
lai_nsnode_t *acpins_resolve(char *);
lai_nsnode_t *acpins_get_device(size_t);
lai_nsnode_t *acpins_get_deviceid(size_t, lai_object_t *);
lai_nsnode_t *acpins_enum(char *, size_t);
void lai_eisaid(lai_object_t *, char *);
size_t lai_read_resource(lai_nsnode_t *, acpi_resource_t *);

// ACPI Control Methods
void lai_eval_operand(lai_object_t *, lai_state_t *, uint8_t *);
int lai_eval(lai_object_t *, char *);
void lai_free_object(lai_object_t *);
void lai_move_object(lai_object_t *, lai_object_t *);
void lai_copy_object(lai_object_t *, lai_object_t *);
lai_nsnode_t *lai_exec_resolve(char *);
int lai_populate(lai_nsnode_t *, void *, size_t, lai_state_t *);
int lai_exec_method(lai_nsnode_t *, lai_state_t *);
void lai_read_opregion(lai_object_t *, lai_nsnode_t *);
void lai_write_opregion(lai_nsnode_t *, lai_object_t *);
void lai_exec_name(void *, lai_nsnode_t *, lai_state_t *);
void lai_exec_sleep(void *, lai_state_t *);
uint16_t lai_bswap16(uint16_t);
uint32_t lai_bswap32(uint32_t);
uint8_t lai_char_to_hex(char);
void lai_write_buffer(lai_nsnode_t *, lai_object_t *);
void lai_exec_bytefield(void *, lai_nsnode_t *, lai_state_t *);
void lai_exec_wordfield(void *, lai_nsnode_t *, lai_state_t *);
void lai_exec_dwordfield(void *, lai_nsnode_t *, lai_state_t *);

// Generic Functions
int lai_enable_acpi(uint32_t);
int lai_disable_acpi();
uint16_t lai_read_event();
void lai_set_event(uint16_t);
int lai_enter_sleep(uint8_t);
int lai_pci_route(acpi_resource_t *, uint8_t, uint8_t, uint8_t);

