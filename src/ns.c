
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
 */

/* Generic ACPI Namespace Management */

#include <lai/core.h>
#include "aml_opcodes.h"
#include "ns_impl.h"
#include "exec_impl.h"
#include "libc.h"
#include "eval.h"

#define CODE_WINDOW            131072
#define NAMESPACE_WINDOW       8192

int lai_do_osi_method(lai_object_t *args, lai_object_t *result);
int lai_do_os_method(lai_object_t *args, lai_object_t *result);
int lai_do_rev_method(lai_object_t *args, lai_object_t *result);

lai_nsnode_t **lai_namespace;
size_t lai_ns_size = 0;
size_t lai_ns_capacity = 0;

acpi_fadt_t *lai_fadt;

static struct lai_aml_segment *lai_load_table(void *ptr, int index);

lai_nsnode_t *lai_create_nsnode(void) {
    lai_nsnode_t *node = laihost_malloc(sizeof(lai_nsnode_t));
    if (!node)
        return NULL;
    // here we assume that the host does not return zeroed memory,
    // so lai must zero the returned memory itself.
    memset(node, 0, sizeof(lai_nsnode_t));
    return node;
}

lai_nsnode_t *lai_create_nsnode_or_die(void) {
    lai_nsnode_t *node = lai_create_nsnode();
    if (!node)
        lai_panic("could not allocate new namespace node");
    return node;
}

// Installs the nsnode to the namespace.
void lai_install_nsnode(lai_nsnode_t *node) {
    if (lai_ns_size == lai_ns_capacity) {
        size_t new_capacity = lai_ns_capacity * 2;
        if (!new_capacity)
            new_capacity = NAMESPACE_WINDOW;
        lai_nsnode_t **new_array;
        new_array = laihost_realloc(lai_namespace, sizeof(lai_nsnode_t *) * new_capacity);
        if (!new_array)
            lai_panic("could not reallocate namespace table");
        lai_namespace = new_array;
        lai_ns_capacity = new_capacity;
    }

    /*lai_debug("created %s", node->path);*/
    lai_namespace[lai_ns_size++] = node;
}

size_t lai_resolve_path(lai_nsnode_t *context, char *fullpath, uint8_t *path) {
    size_t name_size = 0;
    size_t multi_count = 0;
    size_t current_count = 0;

    memset(fullpath, 0, ACPI_MAX_NAME);

    if (path[0] == ROOT_CHAR) {
        name_size = 1;
        fullpath[0] = ROOT_CHAR;
        fullpath[1] = 0;
        path++;
        if (lai_is_name(path[0]) || path[0] == DUAL_PREFIX || path[0] == MULTI_PREFIX) {
            fullpath[1] = '.';
            fullpath[2] = 0;
            goto start;
        } else
            return name_size;
    }

    if (context)
        lai_strcpy(fullpath, context->path);
    else
        lai_strcpy(fullpath, "\\");
    fullpath[lai_strlen(fullpath)] = '.';

start:
    while(path[0] == PARENT_CHAR) {
        path++;
        if (lai_strlen(fullpath) <= 2)
            break;

        name_size++;
        fullpath[lai_strlen(fullpath) - 5] = 0;
        memset(fullpath + lai_strlen(fullpath), 0, 32);
    }

    if (path[0] == DUAL_PREFIX) {
        name_size += 9;
        path++;
        memcpy(fullpath + lai_strlen(fullpath), path, 4);
        fullpath[lai_strlen(fullpath)] = '.';
        memcpy(fullpath + lai_strlen(fullpath), path + 4, 4);
    } else if (path[0] == MULTI_PREFIX) {
        // skip MULTI_PREFIX and name count
        name_size += 2;
        path++;

        // get name count here
        multi_count = (size_t)path[0];
        path++;

        current_count = 0;
        while (current_count < multi_count) {
            name_size += 4;
            memcpy(fullpath + lai_strlen(fullpath), path, 4);
            path += 4;
            current_count++;
            if (current_count >= multi_count)
                break;

            fullpath[lai_strlen(fullpath)] = '.';
        }
    } else {
        name_size += 4;
        memcpy(fullpath + lai_strlen(fullpath), path, 4);
    }

    return name_size;
}

// Creates the ACPI namespace. Requires the ability to scan for ACPI tables - ensure this is
// implemented in the host operating system.
void lai_create_namespace(void) {
    if (!laihost_scan)
        lai_panic("lai_create_namespace() needs table management functions");

    lai_namespace = lai_calloc(sizeof(lai_nsnode_t *), NAMESPACE_WINDOW);
    if (!lai_namespace)
        lai_panic("unable to allocate memory.");

    // we need the FADT
    lai_fadt = laihost_scan("FACP", 0);
    if (!lai_fadt) {
        lai_panic("unable to find ACPI FADT.");
    }

    // Create the OS-defined objects first.
    lai_nsnode_t *osi_node = lai_create_nsnode_or_die();
    osi_node->type = LAI_NAMESPACE_METHOD;
    lai_strcpy(osi_node->path, "\\._OSI");
    osi_node->method_flags = 0x01;
    osi_node->method_override = &lai_do_osi_method;
    lai_install_nsnode(osi_node);

    lai_nsnode_t *os_node = lai_create_nsnode_or_die();
    os_node->type = LAI_NAMESPACE_METHOD;
    lai_strcpy(os_node->path, "\\._OS_");
    os_node->method_flags = 0x00;
    os_node->method_override = &lai_do_os_method;
    lai_install_nsnode(os_node);

    lai_nsnode_t *rev_node = lai_create_nsnode_or_die();
    rev_node->type = LAI_NAMESPACE_METHOD;
    lai_strcpy(rev_node->path, "\\._REV");
    rev_node->method_flags = 0x00;
    rev_node->method_override = &lai_do_rev_method;
    lai_install_nsnode(rev_node);

    // Create the namespace with all the objects.
    lai_state_t state;

    // Load the DSDT.
    void *dsdt_table = laihost_scan("DSDT", 0);
    void *dsdt_amls = lai_load_table(dsdt_table, 0);
    lai_init_state(&state);
    lai_populate(NULL, dsdt_amls, &state);
    lai_finalize_state(&state);

    // Load all SSDTs.
    size_t index = 0;
    acpi_aml_t *ssdt_table;
    while ((ssdt_table = laihost_scan("SSDT", index))) {
        void *ssdt_amls = lai_load_table(ssdt_table, index);
        lai_init_state(&state);
        lai_populate(NULL, ssdt_amls, &state);
        lai_finalize_state(&state);
        index++;
    }

    // The PSDT is treated the same way as the SSDT.
    // Scan for PSDTs too for compatibility with some ACPI 1.0 PCs.
    index = 0;
    acpi_aml_t *psdt_table;
    while ((psdt_table = laihost_scan("PSDT", index))) {
        void *psdt_amls = lai_load_table(psdt_table, index);
        lai_init_state(&state);
        lai_populate(NULL, psdt_amls, &state);
        lai_finalize_state(&state);
        index++;
    }

    lai_debug("ACPI namespace created, total of %d predefined objects.", lai_ns_size);
}

static struct lai_aml_segment *lai_load_table(void *ptr, int index) {
    struct lai_aml_segment *amls = laihost_malloc(sizeof(struct lai_aml_segment));
    if(!amls)
        lai_panic("could not allocate memory for struct lai_aml_segment");
    memset(amls, 0, sizeof(struct lai_aml_segment));

    amls->table = ptr;
    amls->index = index;

    lai_debug("loaded AML table '%c%c%c%c', total %d bytes of AML code.",
            amls->table->header.signature[0],
            amls->table->header.signature[1],
            amls->table->header.signature[2],
            amls->table->header.signature[3],
            amls->table->header.length);
    return amls;
}

// TODO: This entire function could probably do with a rewrite soonish.
size_t lai_create_field(lai_nsnode_t *parent, void *data) {
    uint8_t *field = (uint8_t *)data;
    field += 2;        // skip opcode

    // package size
    size_t pkgsize, size;

    pkgsize = lai_parse_pkgsize(field, &size);
    field += pkgsize;

    // determine name of opregion
    lai_nsnode_t *opregion;
    char opregion_name[ACPI_MAX_NAME];
    size_t name_size = 0;

    name_size = lai_resolve_path(parent, opregion_name, field);

    opregion = lai_exec_resolve(opregion_name);
    if (!opregion) {
        lai_debug("error parsing field for non-existant OpRegion %s, ignoring...", opregion_name);
        return size + 2;
    }

    // parse the field's entries now
    uint8_t field_flags;
    field = (uint8_t *)data + 2 + pkgsize + name_size;
    field_flags = field[0];


    // FIXME: Why this increment_namespace()?
    //acpins_increment_namespace();

    field++;        // actual field objects
    size_t byte_count = (size_t)((size_t)field - (size_t)data);

    uint64_t current_offset = 0;
    size_t skip_size, skip_bits;
    size_t field_size;

    while (byte_count < size) {
        if (!field[0]) {
            field++;
            byte_count++;

            skip_size = lai_parse_pkgsize(field, &skip_bits);
            current_offset += skip_bits;

            field += skip_size;
            byte_count += skip_size;

            continue;
        }

        if (field[0] == 1) {
            field_flags = field[1];

            field += 3;
            byte_count += 3;

            continue;
        }

        if(field[0] == 2) {
            lai_warn("field for OpRegion %s: ConnectField unimplemented.", opregion->path);

            field++;
            byte_count++;

            while (!lai_is_name(field[0])) {
                field++;
                byte_count++;
            }
        }

        if (byte_count >= size)
            break;

        lai_nsnode_t *node = lai_create_nsnode_or_die();
        node->type = LAI_NAMESPACE_FIELD;

        name_size = lai_resolve_path(parent, node->path, &field[0]);
        field += name_size;
        byte_count += name_size;

        // FIXME: This looks odd. Why do we insert a dot in the middle of the path?
        /*node->path[lai_strlen(parent->path)] = '.';*/

        lai_strcpy(node->field_opregion, opregion->path);

        field_size = lai_parse_pkgsize(&field[0], &node->field_size);

        node->field_flags = field_flags;
        node->field_offset = current_offset;

        current_offset += (uint64_t)node->field_size;
        lai_install_nsnode(node);

        field += field_size;
        byte_count += field_size;
    }

    return size + 2;
}

// Create a control method in the namespace.
size_t lai_create_method(lai_nsnode_t *parent, struct lai_aml_segment *amls, void *data) {
    uint8_t *method = (uint8_t *)data;
    method++;        // skip over METHOD_OP

    size_t size, pkgsize;
    pkgsize = lai_parse_pkgsize(method, &size);
    method += pkgsize;

    // create a namespace object for the method
    lai_nsnode_t *node = lai_create_nsnode_or_die();
    size_t name_length = lai_resolve_path(parent, node->path, method);

    // get the method's flags
    method = (uint8_t *)data;
    method += pkgsize + name_length + 1;

    // create a node corresponding to this method,
    // and add it to the namespace.
    node->type = LAI_NAMESPACE_METHOD;
    node->method_flags = method[0];
    node->amls = amls;
    node->pointer = (void *)(method + 1);
    node->size = size - pkgsize - name_length - 1;

    lai_install_nsnode(node);
    return size + 1;
}

// Create an alias in the namespace
size_t lai_create_alias(lai_nsnode_t *parent, void *data) {
    size_t return_size = 1;
    uint8_t *alias = (uint8_t *)data;
    alias++;        // skip ALIAS_OP

    size_t name_size;

    lai_nsnode_t *node = lai_create_nsnode_or_die();
    node->type = LAI_NAMESPACE_ALIAS;
    name_size = lai_resolve_path(parent, node->alias, alias);

    return_size += name_size;
    alias += name_size;

    name_size = lai_resolve_path(parent, node->path, alias);

    //lai_debug("alias %s for object %s", node->path, node->alias);

    lai_install_nsnode(node);
    return_size += name_size;
    return return_size;
}

size_t lai_create_mutex(lai_nsnode_t *parent, void *data) {
    size_t return_size = 2;
    uint8_t *mutex = (uint8_t *)data;
    mutex += 2;        // skip MUTEX_OP

    lai_nsnode_t *node = lai_create_nsnode_or_die();
    node->type = LAI_NAMESPACE_MUTEX;
    size_t name_size = lai_resolve_path(parent, node->path, mutex);

    return_size += name_size;
    return_size++;

    lai_install_nsnode(node);
    return return_size;
}

size_t lai_create_indexfield(lai_nsnode_t *parent, void *data) {
    uint8_t *indexfield = (uint8_t *)data;
    indexfield += 2;        // skip INDEXFIELD_OP

    size_t pkgsize, size;
    pkgsize = lai_parse_pkgsize(indexfield, &size);

    indexfield += pkgsize;

    // index and data
    char indexr[ACPI_MAX_NAME], datar[ACPI_MAX_NAME];
    memset(indexr, 0, ACPI_MAX_NAME);
    memset(datar, 0, ACPI_MAX_NAME);

    indexfield += lai_resolve_path(parent, indexr, indexfield);
    indexfield += lai_resolve_path(parent, datar, indexfield);

    lai_nsnode_t *index_node = lai_resolve(indexr);
    if (!index_node)
        lai_panic("could not resolve index register of IndexField()");
    lai_nsnode_t *data_node = lai_resolve(datar);
    if (!data_node)
        lai_panic("could not resolve index register of IndexField()");

    uint8_t flags = indexfield[0];

    indexfield++;            // actual field list
    size_t byte_count = (size_t)((size_t)indexfield - (size_t)data);

    uint64_t current_offset = 0;
    size_t skip_size, skip_bits;
    size_t name_size;

    while (byte_count < size) {
        while (!indexfield[0]) {
            indexfield++;
            byte_count++;

            skip_size = lai_parse_pkgsize(indexfield, &skip_bits);
            current_offset += skip_bits;

            indexfield += skip_size;
            byte_count += skip_size;
        }

        //lai_debug("indexfield %c%c%c%c: size %d bits, at bit offset %d", indexfield[0], indexfield[1], indexfield[2], indexfield[3], indexfield[4], current_offset);
        lai_nsnode_t *node = lai_create_nsnode_or_die();
        node->type = LAI_NAMESPACE_INDEXFIELD;
        // FIXME: This looks odd. Why don't we all acpins_resolve_path()?

        /*memcpy(node->path, parent->path, lai_strlen(parent->path));
        node->path[lai_strlen(parent->path)] = '.';
        memcpy(node->path + lai_strlen(parent->path) + 1, indexfield, 4);*/

        name_size = lai_resolve_path(parent, node->path, &indexfield[0]);

        indexfield += name_size;
        byte_count += name_size;

        node->idxf_index_node = index_node;
        node->idxf_data_node = data_node;
        node->idxf_flags = flags;
        node->idxf_size = indexfield[0];
        node->idxf_offset = current_offset;

        current_offset += (uint64_t)(indexfield[0]);
        lai_install_nsnode(node);

        indexfield++;
        byte_count++;
    }

    return size + 2;
}

// acpins_create_processor(): Creates a Processor object in the namespace
// Param:    void *data - pointer to data
// Return:    size_t - total size in bytes, for skipping

size_t lai_create_processor(lai_nsnode_t *parent, void *data) {
    uint8_t *processor = (uint8_t *)data;
    processor += 2;            // skip over PROCESSOR_OP

    size_t pkgsize, size;
    pkgsize = lai_parse_pkgsize(processor, &size);
    processor += pkgsize;

    lai_nsnode_t *node = lai_create_nsnode_or_die();
    node->type = LAI_NAMESPACE_PROCESSOR;
    size_t name_size = lai_resolve_path(parent, node->path, processor);
    processor += name_size;

    node->cpu_id = processor[0];

    lai_install_nsnode(node);

    return size + 2;
}

// Resolve a namespace object by its path
lai_nsnode_t *lai_resolve(char *path) {
    size_t i = 0;

    if (path[0] == ROOT_CHAR) {
        while(i < lai_ns_size) {
            if(!lai_strcmp(lai_namespace[i]->path, path))
                return lai_namespace[i];
            else
                i++;
        }

        return NULL;
    } else {
        while (i < lai_ns_size) {
            if (!memcmp(lai_namespace[i]->path + lai_strlen(lai_namespace[i]->path) - 4, path, 4))
                return lai_namespace[i];
            else
                i++;
        }

        return NULL;
    }
}

// search for a device by its index
lai_nsnode_t *lai_get_device(size_t index) {
    size_t i = 0, j = 0;
    while (j < lai_ns_size) {
        if (lai_namespace[j]->type == LAI_NAMESPACE_DEVICE)
            i++;

        if (i > index)
            return lai_namespace[j];

        j++;
    }

    return NULL;
}

// search for a device by its id and index.
lai_nsnode_t *lai_get_deviceid(size_t index, lai_object_t *id) {
    size_t i = 0, j = 0;

    lai_nsnode_t *handle;
    char path[ACPI_MAX_NAME];
    lai_object_t device_id = {0};

    handle = lai_get_device(j);
    while (handle) {
        // read the ID of the device
        lai_strcpy(path, handle->path);
        // change the device ID to hardware ID
        lai_strcpy(path + lai_strlen(path), "._HID");
        memset(&device_id, 0, sizeof(lai_object_t));
        if (lai_eval(&device_id, path)) {
            // same principle here
            lai_strcpy(path + lai_strlen(path) - 5, "._CID");
            memset(&device_id, 0, sizeof(lai_object_t));
            lai_eval(&device_id, path);
        }

        if (device_id.type == LAI_INTEGER && id->type == LAI_INTEGER) {
            if (device_id.integer == id->integer)
                i++;
        } else if (device_id.type == LAI_STRING && id->type == LAI_STRING) {
            if (!lai_strcmp(lai_exec_string_access(&device_id),
                            lai_exec_string_access(id)))
                i++;
        }

        if (i > index)
            return handle;

        j++;
        handle = lai_get_device(j);
    }

    return NULL;
}

// determine the node in the ACPI namespace corresponding to a given path,
// and return this node.
lai_nsnode_t *lai_enum(char *parent, size_t index) {
    index++;
    size_t parent_size = lai_strlen(parent);
    for (size_t i = 0; i < lai_ns_size; i++) {
        if (!memcmp(parent, lai_namespace[i]->path, parent_size)) {
            if(!index)
                return lai_namespace[i];
            else
                index--;
        }
    }

    return NULL;
}
