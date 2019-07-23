/*
 * Lightweight ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
 */

#pragma once

#include <lai/internal-util.h>

__attribute__((always_inline))
inline void lai_namecpy(char *dest, const char *src) {
    memcpy(dest, src, 4);
}

struct lai_aml_segment {
    acpi_aml_t *table;
    // Index of the table (e.g., for SSDTs).
    size_t index;
};

#define LAI_NAMESPACE_ROOT          1
#define LAI_NAMESPACE_NAME          2
#define LAI_NAMESPACE_ALIAS         3
#define LAI_NAMESPACE_FIELD         4
#define LAI_NAMESPACE_METHOD        5
#define LAI_NAMESPACE_DEVICE        6
#define LAI_NAMESPACE_INDEXFIELD    7
#define LAI_NAMESPACE_MUTEX         8
#define LAI_NAMESPACE_PROCESSOR     9
#define LAI_NAMESPACE_BUFFER_FIELD  10
#define LAI_NAMESPACE_THERMALZONE   11
#define LAI_NAMESPACE_EVENT         12
#define LAI_NAMESPACE_POWER_RES     13
#define LAI_NAMESPACE_BANK_FIELD    14

typedef struct lai_nsnode
{
    char name[4];
    int type;
    struct lai_nsnode *parent;
    struct lai_aml_segment *amls;
    void *pointer;            // valid for scopes, methods, etc.
    size_t size;            // valid for scopes, methods, etc.

    lai_variable_t object;        // for Name()

    uint8_t op_address_space;    // for OpRegions only
    uint64_t op_base;        // for OpRegions only
    uint64_t op_length;        // for OpRegions only

    uint8_t method_flags;        // for Methods only, includes ARG_COUNT in lowest three bits
    // Allows the OS to override methods. Mainly useful for _OSI, _OS and _REV.
    int (*method_override)(lai_variable_t *args, lai_variable_t *result);

    // TODO: Find a good mechanism for locks.
    //lai_lock_t mutex;        // for Mutex

                // for Processor

    union {
        struct lai_nsnode *al_target; // LAI_NAMESPACE_ALIAS.

        struct { // LAI_NAMESPACE_FIELD.
            struct lai_nsnode *fld_region_node;
            uint64_t fld_offset; // In bits.
            size_t fld_size;     // In bits.
            uint8_t fld_flags;
        };
        struct { // LAI_NAMESPACE_INDEX_FIELD.
            uint64_t idxf_offset; // In bits.
            struct lai_nsnode *idxf_index_node;
            struct lai_nsnode *idxf_data_node;
            uint8_t idxf_flags;
            uint8_t idxf_size;
        };
        struct { // LAI_NAMESPACE_BANK_FIELD.
            uint64_t bkf_offset; // In bits.
            struct lai_nsnode *bkf_region_node;
            struct lai_nsnode *bkf_bank_node;
            uint64_t bkf_value;
            uint8_t bkf_flags;
            uint8_t bkf_size;
        };

        struct { // LAI_NAMESPACE_BUFFER_FIELD.
            struct lai_nsnode *bf_node;
            uint64_t bf_offset; // In bits.
            uint64_t bf_size;   // In bits.
        };
        struct { // LAI_NAMESPACE_PROCESSOR
            uint8_t cpu_id;
            uint32_t pblk_addr;
            uint8_t pblk_len;
        };
    };

	// Stores a list of all namespace nodes created by the same method.
    struct lai_list_item per_method_item;

    // Hash table that stores the children of each node.
    struct lai_hashtable children;
} lai_nsnode_t;

