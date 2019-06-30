
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

static int debug_resolution = 0;

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

size_t lai_amlname_parse(struct lai_amlname *amln, const void *data) {
    amln->is_absolute = 0;
    amln->height = 0;

    const uint8_t *begin = data;
    const uint8_t *it = begin;
    if (*it == '\\') {
        // First character is \ for absolute paths.
        amln->is_absolute = 1;
        it++;
    } else {
        // Non-absolute paths can be prefixed by a number of ^.
        while (*it == '^') {
            amln->height++;
            it++;
        }
    }

    // Finally, we parse the name's prefix (which determines the number of segments).
    int num_segs;
    if (*it == '\0') {
        it++;
        num_segs = 0;
    } else if (*it == DUAL_PREFIX) {
        it++;
        num_segs = 2;
    } else if (*it == MULTI_PREFIX) {
        it++;
        num_segs = *it;
        LAI_ENSURE(num_segs > 2);
        it++;
    } else {
        LAI_ENSURE(lai_is_name(*it));
        num_segs = 1;
    }

    amln->search_scopes = !amln->is_absolute && !amln->height && num_segs == 1;
    amln->it = it;
    amln->end = it + 4 * num_segs;
    return amln->end - begin;
}

int lai_amlname_done(struct lai_amlname *amln) {
    return amln->it == amln->end;
}

void lai_amlname_iterate(struct lai_amlname *amln, char *out) {
    LAI_ENSURE(amln->it < amln->end);
    for (int i = 0; i < 4; i++)
        out[i] = amln->it[i];
    amln->it += 4;
}

char *lai_stringify_amlname(const struct lai_amlname *in_amln) {
    // Make a copy to avoid rendering the original object unusable.
    struct lai_amlname amln = *in_amln;

    size_t num_segs = (amln.end - amln.it) / 4;
    size_t max_length = 1              // Leading \ for absolute paths.
                        + amln.height // Leading ^ characters.
                        + num_segs * 5 // Segments, seperated by dots.
                        + 1;          // Null-terminator.

    char *str = laihost_malloc(max_length);
    if (!str)
        lai_panic("could not allocate in lai_stringify_amlname()");

    int n = 0;
    if (amln.is_absolute)
        str[n++] = '\\';
    for (int i = 0; i < amln.height; i++)
        str[n++] = '^';

    if(!lai_amlname_done(&amln)) {
        for (;;) {
            lai_amlname_iterate(&amln, &str[n]);
            n += 4;
            if (lai_amlname_done(&amln))
                break;
            str[n++] = '.';
        }
    }
    str[n++] = '\0';
    LAI_ENSURE(n <= max_length);
    return str;
}

lai_nsnode_t *lai_do_resolve(lai_nsnode_t *ctx_handle, const struct lai_amlname *in_amln) {
    // Make a copy to avoid rendering the original object unusable.
    struct lai_amlname amln = *in_amln;

    lai_nsnode_t *current = ctx_handle;
    LAI_ENSURE(current);
    LAI_ENSURE(current->type != LAI_NAMESPACE_ALIAS); // ctx_handle needs to be resolved.

    if (amln.search_scopes) {
        char segment[5];
        lai_amlname_iterate(&amln, segment);
        LAI_ENSURE(lai_amlname_done(&amln));
        segment[4] = '\0';

        if(debug_resolution)
            lai_debug("resolving %s by searching through scopes", segment);

        while (current) {
            char path[ACPI_MAX_NAME];
            size_t n = lai_strlen(current->path);
            lai_strcpy(path, current->path);
            path[n] = '.';
            lai_strcpy(path + 1 + n, segment);

            lai_nsnode_t *node = lai_resolve(path);
            if (!node) {
                current = current->parent;
                continue;
            }

            if (node->type == LAI_NAMESPACE_ALIAS) {
                node = node->al_target;
                LAI_ENSURE(node->type != LAI_NAMESPACE_ALIAS);
            }
            return node;
        }

        return NULL;
    } else {
        if (amln.is_absolute) {
            while (current->parent)
                current = current->parent;
            LAI_ENSURE(current->type == LAI_NAMESPACE_ROOT);
        }

        for (int i = 0; i < amln.height; i++) {
            if (!current->parent) {
                LAI_ENSURE(current->type == LAI_NAMESPACE_ROOT);
                break;
            }
            current = current->parent;
        }

        if (lai_amlname_done(&amln))
            return current;

        char path[ACPI_MAX_NAME];
        size_t n = lai_strlen(current->path);
        lai_strcpy(path, current->path);

        while (!lai_amlname_done(&amln)) {
            path[n] = '.';
            lai_amlname_iterate(&amln, path + 1 + n);
            n += 5;
        }
        path[n] = '\0';

        lai_nsnode_t *node = lai_resolve(path);
        if (node->type == LAI_NAMESPACE_ALIAS) {
            node = node->al_target;
            LAI_ENSURE(node->type != LAI_NAMESPACE_ALIAS);
        }
        return node;
    }
}

void lai_do_resolve_new_node(lai_nsnode_t *node,
        lai_nsnode_t *ctx_handle, const struct lai_amlname *in_amln) {
    // Make a copy to avoid rendering the original object unusable.
    struct lai_amlname amln = *in_amln;

    lai_nsnode_t *parent = ctx_handle;
    LAI_ENSURE(parent);
    LAI_ENSURE(parent->type != LAI_NAMESPACE_ALIAS); // ctx_handle needs to be resolved.

    // Note: we do not care about amln->search_scopes here.
    //       As we are creating a new name, the code below already does the correct thing.

    if (amln.is_absolute) {
        while (parent->parent)
            parent = parent->parent;
        LAI_ENSURE(parent->type == LAI_NAMESPACE_ROOT);
    }

    for (int i = 0; i < amln.height; i++) {
        if (!parent->parent) {
            LAI_ENSURE(parent->type == LAI_NAMESPACE_ROOT);
            break;
        }
        parent = parent->parent;
    }

    // Otherwise the new object has an empty name.
    LAI_ENSURE(!lai_amlname_done(&amln));

    for (;;) {
        char segment[5];
        lai_amlname_iterate(&amln, segment);
        segment[4] = '\0';

        char path[ACPI_MAX_NAME];
        size_t n = lai_strlen(parent->path);
        lai_strcpy(path, parent->path);
        path[n] = '.';
        lai_strcpy(path + 1 + n, segment);

        if (lai_amlname_done(&amln)) {
            // The last segment is the name of the new node.
            lai_strcpy(node->path, path);
            node->parent = parent;
            break;
        } else {
            parent = lai_resolve(path);
            LAI_ENSURE(parent);
            if (parent->type == LAI_NAMESPACE_ALIAS) {
                lai_warn("resolution of new object name traverses Alias(),"
                        " this is not supported in ACPICA");
                parent = parent->al_target;
                LAI_ENSURE(parent->type != LAI_NAMESPACE_ALIAS);
            }
        }
    }
}

size_t lai_resolve_new_node(lai_nsnode_t *node, lai_nsnode_t *ctx_handle, void *data) {
    struct lai_amlname amln;
    size_t size = lai_amlname_parse(&amln, data);
    lai_do_resolve_new_node(node, ctx_handle, &amln);
    return size;
}

lai_nsnode_t *lai_create_root(void) {
    lai_nsnode_t *root_node = lai_create_nsnode_or_die();
    root_node->type = LAI_NAMESPACE_ROOT;
    lai_strcpy(root_node->path, "\\");
    root_node->parent = NULL;

    // Create the predefined objects.
    lai_nsnode_t *sb_node = lai_create_nsnode_or_die();
    sb_node->type = LAI_NAMESPACE_DEVICE;
    lai_strcpy(sb_node->path, "\\._SB_");
    sb_node->parent = root_node;
    lai_install_nsnode(sb_node);

    lai_nsnode_t *si_node = lai_create_nsnode_or_die();
    si_node->type = LAI_NAMESPACE_DEVICE;
    lai_strcpy(si_node->path, "\\._SI_");
    si_node->parent = root_node;
    lai_install_nsnode(si_node);

    lai_nsnode_t *gpe_node = lai_create_nsnode_or_die();
    gpe_node->type = LAI_NAMESPACE_DEVICE;
    lai_strcpy(gpe_node->path, "\\._GPE");
    gpe_node->parent = root_node;
    lai_install_nsnode(gpe_node);

    // Create nodes for compatibility with ACPI 1.0.
    lai_nsnode_t *pr_node = lai_create_nsnode_or_die();
    pr_node->type = LAI_NAMESPACE_DEVICE;
    lai_strcpy(pr_node->path, "\\._PR_");
    pr_node->parent = root_node;
    lai_install_nsnode(pr_node);

    lai_nsnode_t *tz_node = lai_create_nsnode_or_die();
    tz_node->type = LAI_NAMESPACE_DEVICE;
    lai_strcpy(tz_node->path, "\\._TZ_");
    tz_node->parent = root_node;
    lai_install_nsnode(tz_node);

    // Create the OS-defined objects.
    lai_nsnode_t *osi_node = lai_create_nsnode_or_die();
    osi_node->type = LAI_NAMESPACE_METHOD;
    lai_strcpy(osi_node->path, "\\._OSI");
    osi_node->parent = root_node;
    osi_node->method_flags = 0x01;
    osi_node->method_override = &lai_do_osi_method;
    lai_install_nsnode(osi_node);

    lai_nsnode_t *os_node = lai_create_nsnode_or_die();
    os_node->type = LAI_NAMESPACE_METHOD;
    lai_strcpy(os_node->path, "\\._OS_");
    os_node->parent = root_node;
    os_node->method_flags = 0x00;
    os_node->method_override = &lai_do_os_method;
    lai_install_nsnode(os_node);

    lai_nsnode_t *rev_node = lai_create_nsnode_or_die();
    rev_node->type = LAI_NAMESPACE_METHOD;
    lai_strcpy(rev_node->path, "\\._REV");
    rev_node->parent = root_node;
    rev_node->method_flags = 0x00;
    rev_node->method_override = &lai_do_rev_method;
    lai_install_nsnode(rev_node);

    return root_node;
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

    lai_nsnode_t *root_node = lai_create_root();

    // Create the namespace with all the objects.
    lai_state_t state;

    // Load the DSDT.
    void *dsdt_table = laihost_scan("DSDT", 0);
    void *dsdt_amls = lai_load_table(dsdt_table, 0);
    lai_init_state(&state);
    lai_populate(root_node, dsdt_amls, &state);
    lai_finalize_state(&state);

    // Load all SSDTs.
    size_t index = 0;
    acpi_aml_t *ssdt_table;
    while ((ssdt_table = laihost_scan("SSDT", index))) {
        void *ssdt_amls = lai_load_table(ssdt_table, index);
        lai_init_state(&state);
        lai_populate(root_node, ssdt_amls, &state);
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
        lai_populate(root_node, psdt_amls, &state);
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
    struct lai_amlname region_amln;
    size_t name_size = 0;

    name_size = lai_amlname_parse(&region_amln, field);

    lai_nsnode_t *region_node = lai_do_resolve(parent, &region_amln);
    if (!region_node) {
        lai_warn("error parsing field for non-existant OpRegion , ignoring...");
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
            lai_warn("field for OpRegion %s: ConnectField unimplemented.", region_node->path);

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
        name_size = lai_resolve_new_node(node, parent, &field[0]);
        field += name_size;
        byte_count += name_size;

        // FIXME: This looks odd. Why do we insert a dot in the middle of the path?
        /*node->path[lai_strlen(parent->path)] = '.';*/

        node->fld_region_node = region_node;

        field_size = lai_parse_pkgsize(&field[0], &node->fld_size);

        node->fld_flags = field_flags;
        node->fld_offset = current_offset;

        current_offset += (uint64_t)node->fld_size;
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
    size_t name_length = lai_resolve_new_node(node, parent, method);

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

size_t lai_create_indexfield(lai_nsnode_t *parent, void *data) {
    uint8_t *indexfield = (uint8_t *)data;
    indexfield += 2;        // skip INDEXFIELD_OP

    size_t pkgsize, size;
    pkgsize = lai_parse_pkgsize(indexfield, &size);

    indexfield += pkgsize;

    // index and data
    struct lai_amlname index_amln;
    struct lai_amlname data_amln;
    indexfield += lai_amlname_parse(&index_amln, indexfield);
    indexfield += lai_amlname_parse(&data_amln, indexfield);

    lai_nsnode_t *index_node = lai_do_resolve(parent, &index_amln);
    if (!index_node)
        lai_panic("could not resolve index register of IndexField()");
    lai_nsnode_t *data_node = lai_do_resolve(parent, &data_amln);
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

        name_size = lai_resolve_new_node(node, parent, &indexfield[0]);

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
    size_t name_size = lai_resolve_new_node(node, parent, processor);
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
