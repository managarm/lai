
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 by Omar Muhamed
 */

/* Generic ACPI Namespace Management */

#include <lai/core.h>
#include "aml_opcodes.h"
#include "ns_impl.h"

#define CODE_WINDOW            131072
#define NAMESPACE_WINDOW       2048

int lai_do_osi_method(lai_object_t *args, lai_object_t *result);
int lai_do_os_method(lai_object_t *args, lai_object_t *result);
int lai_do_rev_method(lai_object_t *args, lai_object_t *result);

uint8_t *lai_acpins_code;
size_t lai_acpins_allocation = 0;
size_t lai_acpins_size = 0;
size_t lai_acpins_count = 0;
extern char aml_test[];

lai_nsnode_t **lai_namespace;
size_t lai_ns_size = 0;
size_t lai_ns_capacity = NAMESPACE_WINDOW;

void acpins_load_table(void *);

// Helper function to allocate a lai_nsnode_t.
lai_nsnode_t *acpins_create_nsnode()
{
    lai_nsnode_t *node = lai_malloc(sizeof(lai_nsnode_t));
    if(!node)
        return NULL;
    lai_memset(node, 0, sizeof(lai_nsnode_t));
    return node;
}

// Helper function to allocate a lai_nsnode_t.
lai_nsnode_t *acpins_create_nsnode_or_die()
{
    lai_nsnode_t *node = acpins_create_nsnode();
    if(!node)
        lai_panic("could not allocate new namespace node\n");
    return node;
}

// Installs the nsnode to the namespace.
void acpins_install_nsnode(lai_nsnode_t *node)
{
    // Classical doubling strategy to grow the namespace table.
    if(lai_ns_size == lai_ns_capacity)
    {
        size_t new_capacity = lai_ns_capacity * 2;
        if(!new_capacity)
            new_capacity = NAMESPACE_WINDOW;
        lai_nsnode_t **new_array;
        new_array = lai_realloc(lai_namespace, sizeof(lai_nsnode_t *) * new_capacity);
        if(!new_array)
            lai_panic("could not reallocate namespace table\n");
        lai_namespace = new_array;
        lai_ns_capacity = new_capacity;
    }

    /*lai_debug("created %s\n", node->path);*/
    lai_namespace[lai_ns_size++] = node;
}

// acpins_resolve_path(): Resolves a path
// Param:    char *fullpath - destination
// Param:    uint8_t *path - path to resolve
// Return:    size_t - size of path data parsed in AML

size_t acpins_resolve_path(lai_nsnode_t *context, char *fullpath, uint8_t *path)
{
    size_t name_size = 0;
    size_t multi_count = 0;
    size_t current_count = 0;

    lai_memset(fullpath, 0, ACPI_MAX_NAME);

    if(path[0] == ROOT_CHAR)
    {
        name_size = 1;
        fullpath[0] = ROOT_CHAR;
        fullpath[1] = 0;
        path++;
        if(lai_is_name(path[0]) || path[0] == DUAL_PREFIX || path[0] == MULTI_PREFIX)
        {
            fullpath[1] = '.';
            fullpath[2] = 0;
            goto start;
        } else
            return name_size;
    }

    if(context)
        lai_strcpy(fullpath, context->path);
    else
        lai_strcpy(fullpath, "\\");
    fullpath[lai_strlen(fullpath)] = '.';

start:
    while(path[0] == PARENT_CHAR)
    {
        path++;
        if(lai_strlen(fullpath) <= 2)
            break;

        name_size++;
        fullpath[lai_strlen(fullpath) - 5] = 0;
        lai_memset(fullpath + lai_strlen(fullpath), 0, 32);
    }

    if(path[0] == DUAL_PREFIX)
    {
        name_size += 9;
        path++;
        lai_memcpy(fullpath + lai_strlen(fullpath), path, 4);
        fullpath[lai_strlen(fullpath)] = '.';
        lai_memcpy(fullpath + lai_strlen(fullpath), path + 4, 4);
    } else if(path[0] == MULTI_PREFIX)
    {
        // skip MULTI_PREFIX and name count
        name_size += 2;
        path++;

        // get name count here
        multi_count = (size_t)path[0];
        path++;

        current_count = 0;
        while(current_count < multi_count)
        {
            name_size += 4;
            lai_memcpy(fullpath + lai_strlen(fullpath), path, 4);
            path += 4;
            current_count++;
            if(current_count >= multi_count)
                break;

            fullpath[lai_strlen(fullpath)] = '.';
        }
    } else
    {
        name_size += 4;
        lai_memcpy(fullpath + lai_strlen(fullpath), path, 4);
    }

    return name_size;
}

// lai_create_namespace(): Initializes the AML interpreter and creates the ACPI namespace
// Param:    void *dsdt - pointer to the DSDT
// Return:    Nothing

void lai_create_namespace(void *dsdt)
{
    lai_namespace = lai_calloc(sizeof(lai_nsnode_t *), lai_ns_capacity);
    if(!lai_namespace)
        lai_panic("unable to allocate memory.\n");

    lai_acpins_code = lai_malloc(CODE_WINDOW);
    lai_acpins_allocation = CODE_WINDOW;

    //acpins_load_table(aml_test);    // custom AML table just for testing

    // we need the FADT
    lai_fadt = lai_scan("FACP", 0);
    if(!lai_fadt)
    {
        lai_panic("unable to find ACPI FADT.\n");
    }

    // load the DSDT
    acpins_load_table(dsdt);

    // load all SSDTs
    size_t index = 0;
    acpi_aml_t *ssdt = lai_scan("SSDT", index);
    while(ssdt != NULL)
    {
        acpins_load_table(ssdt);
        index++;
        ssdt = lai_scan("SSDT", index);
    }

    // the PSDT is treated the same way as the SSDT
    // scan for PSDTs too for compatibility with some ACPI 1.0 PCs
    index = 0;
    acpi_aml_t *psdt = lai_scan("PSDT", index);
    while(psdt != NULL)
    {
        acpins_load_table(psdt);
        index++;
        psdt = lai_scan("PSDT", index);
    }

    // create the OS-defined objects first
    lai_nsnode_t *osi_node = acpins_create_nsnode_or_die();
    osi_node->type = LAI_NAMESPACE_METHOD;
    lai_strcpy(osi_node->path, "\\._OSI");
    osi_node->method_flags = 0x01;
    osi_node->method_override = &lai_do_osi_method;
    acpins_install_nsnode(osi_node);

    lai_nsnode_t *os_node = acpins_create_nsnode_or_die();
    os_node->type = LAI_NAMESPACE_METHOD;
    lai_strcpy(os_node->path, "\\._OS_");
    os_node->method_flags = 0x00;
    os_node->method_override = &lai_do_os_method;
    acpins_install_nsnode(os_node);

    lai_nsnode_t *rev_node = acpins_create_nsnode_or_die();
    rev_node->type = LAI_NAMESPACE_METHOD;
    lai_strcpy(rev_node->path, "\\._REV");
    rev_node->method_flags = 0x00;
    rev_node->method_override = &lai_do_rev_method;
    acpins_install_nsnode(rev_node);

    // Create the namespace with all the objects.
    lai_state_t state;
    lai_init_state(&state);
    lai_populate(NULL, lai_acpins_code, lai_acpins_size, &state);
    lai_finalize_state(&state);

    lai_debug("ACPI namespace created, total of %d predefined objects.\n", lai_ns_size);
}

// acpins_load_table(): Loads an AML table
// Param:    void *ptr - pointer to table
// Return:    Nothing

void acpins_load_table(void *ptr)
{
    acpi_aml_t *table = (acpi_aml_t*)ptr;
    while(lai_acpins_size + table->header.length >= lai_acpins_allocation)
    {
        lai_acpins_allocation += CODE_WINDOW;
        lai_acpins_code = lai_realloc(lai_acpins_code, lai_acpins_allocation);
    }

    // copy the actual AML code
    lai_memcpy(lai_acpins_code + lai_acpins_size, table->data, table->header.length - sizeof(acpi_header_t));
    lai_acpins_size += (table->header.length - sizeof(acpi_header_t));

    lai_debug("loaded AML table '%c%c%c%c', total %d bytes of AML code.\n", table->header.signature[0], table->header.signature[1], table->header.signature[2], table->header.signature[3], lai_acpins_size);

    lai_acpins_count++;
}

// acpins_create_field(): Creates a Field object in the namespace
// Param:    void *data - pointer to field data
// Return:    size_t - total size of field in bytes

size_t acpins_create_field(lai_nsnode_t *parent, void *data)
{
    uint8_t *field = (uint8_t*)data;
    field += 2;        // skip opcode

    // package size
    size_t pkgsize, size;

    pkgsize = lai_parse_pkgsize(field, &size);
    field += pkgsize;

    // determine name of opregion
    lai_nsnode_t *opregion;
    char opregion_name[ACPI_MAX_NAME];
    size_t name_size = 0;

    name_size = acpins_resolve_path(parent, opregion_name, field);

    opregion = acpins_resolve(opregion_name);
    if(!opregion)
    {
        lai_debug("error parsing field for non-existant OpRegion %s, ignoring...\n", opregion_name);
        return size + 2;
    }

    // parse the field's entries now
    uint8_t field_flags;
    field = (uint8_t*)data + 2 + pkgsize + name_size;
    field_flags = field[0];

    /*lai_debug("field for OpRegion %s, flags 0x%X (", opregion->path, field_flags);
    switch(field_flags & 0x0F)
    {
    case FIELD_ANY_ACCESS:
        lai_debug("any ");
        break;
    case FIELD_BYTE_ACCESS:
        lai_debug("byte ");
        break;
    case FIELD_WORD_ACCESS:
        lai_debug("word ");
        break;
    case FIELD_DWORD_ACCESS:
        lai_debug("dword ");
        break;
    case FIELD_QWORD_ACCESS:
        lai_debug("qword ");
        break;
    default:
        lai_debug("undefined access size: assuming any, ");
        break;
    }

    if(field_flags & FIELD_LOCK)
        lai_debug("lock ");

    switch((field_flags >> 5) & 0x0F)
    {
    case FIELD_PRESERVE:
        lai_debug("preserve");
        break;
    case FIELD_WRITE_ONES:
        lai_debug("ones");
        break;
    case FIELD_WRITE_ZEROES:
        lai_debug("zeroes");
        break;
    default:
        lai_debug("undefined update type");
        break;
    }

    lai_debug(")\n");*/

    // FIXME: Why this increment_namespace()?
    //acpins_increment_namespace();

    field++;        // actual field objects
    size_t byte_count = (size_t)((size_t)field - (size_t)data);

    uint64_t current_offset = 0;
    size_t skip_size, skip_bits;

    while(byte_count < size)
    {
        while(field[0] == 0)        // skipping?
        {
            field++;
            byte_count++;

            skip_size = lai_parse_pkgsize(field, &skip_bits);
            current_offset += skip_bits;

            field += skip_size;
            byte_count += skip_size;
        }

        while(field[0] == 1)        // access field, unimplemented
        {
            field += 3;
            byte_count += 3;
        }

        if(!lai_is_name(field[0]))
        {
            field++;
            byte_count++;
        }

        if(byte_count >= size)
            break;

        //lai_debug("field %c%c%c%c: size %d bits, at bit offset %d\n", field[0], field[1], field[2], field[3], field[4], current_offset);
        lai_nsnode_t *node = acpins_create_nsnode_or_die();
        node->type = LAI_NAMESPACE_FIELD;

        name_size = acpins_resolve_path(parent, node->path, &field[0]);
        field += name_size;
        byte_count += name_size;

        // FIXME: This looks odd. Why do we insert a dot in the middle of the path?
        /*node->path[lai_strlen(parent->path)] = '.';*/

        lai_strcpy(node->field_opregion, opregion->path);

        node->field_flags = field_flags;
        node->field_size = field[0];
        node->field_offset = current_offset;

        current_offset += (uint64_t)(field[0]);
        acpins_install_nsnode(node);

        field++;
        byte_count++;
    }

    return size + 2;
}

// acpins_create_method(): Registers a control method in the namespace
// Param:    void *data - pointer to AML code
// Return:    size_t - total size in bytes for skipping

size_t acpins_create_method(lai_nsnode_t *parent, void *data)
{
    uint8_t *method = (uint8_t*)data;
    method++;        // skip over METHOD_OP

    size_t size, pkgsize;
    pkgsize = lai_parse_pkgsize(method, &size);
    method += pkgsize;

    // create a namespace object for the method
    lai_nsnode_t *node = acpins_create_nsnode_or_die();
    size_t name_length = acpins_resolve_path(parent, node->path, method);

    // get the method's flags
    method = (uint8_t*)data;
    method += pkgsize + name_length + 1;

    // put the method in the namespace
    node->type = LAI_NAMESPACE_METHOD;
    node->method_flags = method[0];
    node->pointer = (void*)(method + 1);
    node->size = size - pkgsize - name_length - 1;

    /*lai_debug("control method %s, flags 0x%X (argc %d ", node->path, method[0], method[0] & METHOD_ARGC_MASK);
    if(method[0] & METHOD_SERIALIZED)
        lai_debug("serialized");
    else
        lai_debug("non-serialized");

    lai_debug(")\n");*/

    acpins_install_nsnode(node);
    return size + 1;
}

// acpins_create_alias(): Creates an alias object in the namespace
// Param:    void *data - pointer to data
// Return:    size_t - total size in bytes, for skipping

size_t acpins_create_alias(lai_nsnode_t *parent, void *data)
{
    size_t return_size = 1;
    uint8_t *alias = (uint8_t*)data;
    alias++;        // skip ALIAS_OP

    size_t name_size;

    lai_nsnode_t *node = acpins_create_nsnode_or_die();
    node->type = LAI_NAMESPACE_ALIAS;
    name_size = acpins_resolve_path(parent, node->alias, alias);

    return_size += name_size;
    alias += name_size;

    name_size = acpins_resolve_path(parent, node->path, alias);

    //lai_debug("alias %s for object %s\n", node->path, node->alias);

    acpins_install_nsnode(node);
    return_size += name_size;
    return return_size;
}

// acpins_create_mutex(): Creates a Mutex object in the namespace
// Param:    void *data - pointer to data
// Return:    size_t - total size in bytes, for skipping

size_t acpins_create_mutex(lai_nsnode_t *parent, void *data)
{
    size_t return_size = 2;
    uint8_t *mutex = (uint8_t*)data;
    mutex += 2;        // skip MUTEX_OP

    lai_nsnode_t *node = acpins_create_nsnode_or_die();
    node->type = LAI_NAMESPACE_MUTEX;
    size_t name_size = acpins_resolve_path(parent, node->path, mutex);

    return_size += name_size;
    return_size++;

    //lai_debug("mutex object %s\n", node->path);

    acpins_install_nsnode(node);
    return return_size;
}

// acpins_create_indexfield(): Creates an IndexField object in the namespace
// Param:    void *data - pointer to indexfield data
// Return:    size_t - total size of indexfield in bytes

size_t acpins_create_indexfield(lai_nsnode_t *parent, void *data)
{
    uint8_t *indexfield = (uint8_t*)data;
    indexfield += 2;        // skip INDEXFIELD_OP

    size_t pkgsize, size;
    pkgsize = lai_parse_pkgsize(indexfield, &size);

    indexfield += pkgsize;

    // index and data
    char indexr[ACPI_MAX_NAME], datar[ACPI_MAX_NAME];
    lai_memset(indexr, 0, ACPI_MAX_NAME);
    lai_memset(datar, 0, ACPI_MAX_NAME);

    indexfield += acpins_resolve_path(parent, indexr, indexfield);
    indexfield += acpins_resolve_path(parent, datar, indexfield);

    uint8_t flags = indexfield[0];

    /*lai_debug("IndexField index %s data %s, flags 0x%X (", indexr, datar, flags);
    switch(flags & 0x0F)
    {
    case FIELD_ANY_ACCESS:
        lai_debug("any ");
        break;
    case FIELD_BYTE_ACCESS:
        lai_debug("byte ");
        break;
    case FIELD_WORD_ACCESS:
        lai_debug("word ");
        break;
    case FIELD_DWORD_ACCESS:
        lai_debug("dword ");
        break;
    case FIELD_QWORD_ACCESS:
        lai_debug("qword ");
        break;
    default:
        lai_debug("undefined access size: assuming any, ");
        break;
    }

    if(flags & FIELD_LOCK)
        lai_debug("lock ");

    switch((flags >> 5) & 0x0F)
    {
    case FIELD_PRESERVE:
        lai_debug("preserve");
        break;
    case FIELD_WRITE_ONES:
        lai_debug("ones");
        break;
    case FIELD_WRITE_ZEROES:
        lai_debug("zeroes");
        break;
    default:
        lai_debug("undefined update type");
        break;
    }

    lai_debug(")\n");*/

    indexfield++;            // actual field list
    size_t byte_count = (size_t)((size_t)indexfield - (size_t)data);

    uint64_t current_offset = 0;
    size_t skip_size, skip_bits;
    size_t name_size;

    while(byte_count < size)
    {
        while(indexfield[0] == 0)        // skipping?
        {
            indexfield++;
            byte_count++;

            skip_size = lai_parse_pkgsize(indexfield, &skip_bits);
            current_offset += skip_bits;

            indexfield += skip_size;
            byte_count += skip_size;
        }

        //lai_debug("indexfield %c%c%c%c: size %d bits, at bit offset %d\n", indexfield[0], indexfield[1], indexfield[2], indexfield[3], indexfield[4], current_offset);
        lai_nsnode_t *node = acpins_create_nsnode_or_die();
        node->type = LAI_NAMESPACE_INDEXFIELD;
        // FIXME: This looks odd. Why don't we all acpins_resolve_path()?

        /*lai_memcpy(node->path, parent->path, lai_strlen(parent->path));
        node->path[lai_strlen(parent->path)] = '.';
        lai_memcpy(node->path + lai_strlen(parent->path) + 1, indexfield, 4);*/

        name_size = acpins_resolve_path(parent, node->path, &indexfield[0]);

        indexfield += name_size;
        byte_count += name_size;

        lai_strcpy(node->indexfield_data, datar);
        lai_strcpy(node->indexfield_index, indexr);

        node->indexfield_flags = flags;
        node->indexfield_size = indexfield[0];
        node->indexfield_offset = current_offset;

        current_offset += (uint64_t)(indexfield[0]);
        acpins_install_nsnode(node);

        indexfield++;
        byte_count++;
    }

    return size + 2;
}

// acpins_create_processor(): Creates a Processor object in the namespace
// Param:    void *data - pointer to data
// Return:    size_t - total size in bytes, for skipping

size_t acpins_create_processor(lai_nsnode_t *parent, void *data)
{
    uint8_t *processor = (uint8_t*)data;
    processor += 2;            // skip over PROCESSOR_OP

    size_t pkgsize, size;
    pkgsize = lai_parse_pkgsize(processor, &size);
    processor += pkgsize;

    lai_nsnode_t *node = acpins_create_nsnode_or_die();
    node->type = LAI_NAMESPACE_PROCESSOR;
    size_t name_size = acpins_resolve_path(parent, node->path, processor);
    processor += name_size;

    node->cpu_id = processor[0];

    //lai_debug("processor %s ACPI ID %d\n", node->path, node->cpu_id);

    acpins_install_nsnode(node);

    return size + 2;
}

// acpins_create_bytefield(): Creates a ByteField object for a buffer in the namespace
// Param:    void *data - pointer to data
// Return:    size_t - total size in bytes, for skipping

size_t acpins_create_bytefield(lai_nsnode_t *parent, void *data)
{
    uint8_t *bytefield = (uint8_t*)data;
    bytefield++;        // skip BYTEFIELD_OP
    size_t return_size = 1;

    lai_nsnode_t *node = acpins_create_nsnode_or_die();
    node->type = LAI_NAMESPACE_BUFFER_FIELD;

    // buffer name
    size_t name_size;
    name_size = acpins_resolve_path(parent, node->buffer, bytefield);

    return_size += name_size;
    bytefield += name_size;

    size_t integer_size;
    uint64_t integer;
    integer_size = lai_eval_integer(bytefield, &integer);

    node->buffer_offset = integer * 8;
    node->buffer_size = 8;

    return_size += integer_size;
    bytefield += integer_size;

    name_size = acpins_resolve_path(parent, node->path, bytefield);

    acpins_install_nsnode(node);
    return_size += name_size;
    return return_size;
}

// acpins_create_wordfield(): Creates a WordField object for a buffer in the namespace
// Param:    void *data - pointer to data
// Return:    size_t - total size in bytes, for skipping

size_t acpins_create_wordfield(lai_nsnode_t *parent, void *data)
{
    uint8_t *wordfield = (uint8_t*)data;
    wordfield++;        // skip WORDFIELD_OP
    size_t return_size = 1;

    lai_nsnode_t *node = acpins_create_nsnode_or_die();
    node->type = LAI_NAMESPACE_BUFFER_FIELD;

    // buffer name
    size_t name_size;
    name_size = acpins_resolve_path(parent, node->buffer, wordfield);

    return_size += name_size;
    wordfield += name_size;

    size_t integer_size;
    uint64_t integer;
    integer_size = lai_eval_integer(wordfield, &integer);

    node->buffer_offset = integer * 8;
    node->buffer_size = 16;    // bits

    return_size += integer_size;
    wordfield += integer_size;

    name_size = acpins_resolve_path(parent, node->path, wordfield);

    //lai_debug("field %s for buffer %s, offset %d size %d bits\n", node->path, node->buffer, node->buffer_offset, node->buffer_size);

    acpins_install_nsnode(node);
    return_size += name_size;
    return return_size;
}

// acpins_create_dwordfield(): Creates a DwordField object for a buffer in the namespace
// Param:    void *data - pointer to data
// Return:    size_t - total size in bytes, for skipping

size_t acpins_create_dwordfield(lai_nsnode_t *parent, void *data)
{
    uint8_t *dwordfield = (uint8_t*)data;
    dwordfield++;        // skip DWORDFIELD_OP
    size_t return_size = 1;

    lai_nsnode_t *node = acpins_create_nsnode_or_die();
    node->type = LAI_NAMESPACE_BUFFER_FIELD;

    // buffer name
    size_t name_size;
    name_size = acpins_resolve_path(parent, node->buffer, dwordfield);

    return_size += name_size;
    dwordfield += name_size;

    size_t integer_size;
    uint64_t integer;
    integer_size = lai_eval_integer(dwordfield, &integer);

    node->buffer_offset = integer * 8;
    node->buffer_size = 32;

    return_size += integer_size;
    dwordfield += integer_size;

    name_size = acpins_resolve_path(parent, node->path, dwordfield);

    acpins_install_nsnode(node);
    return_size += name_size;
    return return_size;
}

// acpins_create_qwordfield(): Creates a QwordField object for a buffer in the namespace
// Param:    void *data - pointer to data
// Return:    size_t - total size in bytes, for skipping

size_t acpins_create_qwordfield(lai_nsnode_t *parent, void *data)
{
    uint8_t *qwordfield = (uint8_t*)data;
    qwordfield++;        // skip QWORDFIELD_OP
    size_t return_size = 1;

    lai_nsnode_t *node = acpins_create_nsnode_or_die();
    node->type = LAI_NAMESPACE_BUFFER_FIELD;

    // buffer name
    size_t name_size;
    name_size = acpins_resolve_path(parent, node->buffer, qwordfield);

    return_size += name_size;
    qwordfield += name_size;

    size_t integer_size;
    uint64_t integer;
    integer_size = lai_eval_integer(qwordfield, &integer);

    node->buffer_offset = integer * 8;
    node->buffer_size = 64;

    return_size += integer_size;
    qwordfield += integer_size;

    name_size = acpins_resolve_path(parent, node->path, qwordfield);

    acpins_install_nsnode(node);
    return_size += name_size;
    return return_size;
}

// acpins_resolve(): Returns a namespace object from its path
// Param:    char *path - 4-char object name or full path
// Return:    lai_nsnode_t * - pointer to namespace object, NULL on error

lai_nsnode_t *acpins_resolve(char *path)
{
    size_t i = 0;

    if(path[0] == ROOT_CHAR)        // full path?
    {
        // yep, search for the absolute path
        while(i < lai_ns_size)
        {
            if(lai_strcmp(lai_namespace[i]->path, path) == 0)
                return lai_namespace[i];

            else
                i++;
        }

        return NULL;
    } else            // 4-char name here
    {
        while(i < lai_ns_size)
        {
            if(lai_memcmp(lai_namespace[i]->path + lai_strlen(lai_namespace[i]->path) - 4, path, 4) == 0)
                return lai_namespace[i];

            else
                i++;
        }

        return NULL;
    }
}

// acpins_get_device(): Returns a device by its index
// Param:    size_t index - index
// Return:    lai_nsnode_t * - device handle, NULL on error

lai_nsnode_t *acpins_get_device(size_t index)
{
    size_t i = 0, j = 0;
    while(j < lai_ns_size)
    {
        if(lai_namespace[j]->type == LAI_NAMESPACE_DEVICE)
            i++;

        if(i > index)
            return lai_namespace[j];

        j++;
    }

    return NULL;
}

// acpins_get_deviceid(): Returns a device by its index and its ID
// Param:    size_t index - index
// Param:    lai_object_t *id - device ID
// Return:    lai_nsnode_t * - device handle, NULL on error

lai_nsnode_t *acpins_get_deviceid(size_t index, lai_object_t *id)
{
    size_t i = 0, j = 0;

    lai_nsnode_t *handle;
    char path[ACPI_MAX_NAME];
    lai_object_t device_id = {0};

    handle = acpins_get_device(j);
    while(handle != NULL)
    {
        // read the ID of the device
        lai_strcpy(path, handle->path);
        lai_strcpy(path + lai_strlen(path), "._HID");    // hardware ID
        lai_memset(&device_id, 0, sizeof(lai_object_t));
        if(lai_eval(&device_id, path) != 0)
        {
            lai_strcpy(path + lai_strlen(path) - 5, "._CID");    // compatible ID
            lai_memset(&device_id, 0, sizeof(lai_object_t));
            lai_eval(&device_id, path);
        }

        if(device_id.type == LAI_INTEGER && id->type == LAI_INTEGER)
        {
            if(device_id.integer == id->integer)
                i++;
        } else if(device_id.type == LAI_STRING && id->type == LAI_STRING)
        {
            if(lai_strcmp(device_id.string, id->string) == 0)
                i++;
        }

        if(i > index)
            return handle;

        j++;
        handle = acpins_get_device(j);
    }

    return NULL;
}

// acpins_enum(): Enumerates children of an ACPI namespace node
// Param:   char *parent - parent to enumerate
// Param:   size_t index - child index
// Return:  lai_nsnode_t * - child node, NULL if non-existant

lai_nsnode_t *acpins_enum(char *parent, size_t index)
{
    index++;
    size_t parent_size = lai_strlen(parent);
    for(size_t i = 0; i < lai_ns_size; i++)
    {
        if(!lai_memcmp(parent, lai_namespace[i]->path, parent_size))
        {
            if(!index)
                return lai_namespace[i];
            else
                index--;
        }
    }

    return NULL;
}

