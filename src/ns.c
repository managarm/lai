
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 by Omar Muhamed
 */

/* Generic ACPI Namespace Management */

#include "lai.h"
#include "ns_impl.h"

#define CODE_WINDOW            131072
#define NAMESPACE_WINDOW       2048

int acpi_do_osi_method(acpi_object_t *args, acpi_object_t *result);
int acpi_do_os_method(acpi_object_t *args, acpi_object_t *result);
int acpi_do_rev_method(acpi_object_t *args, acpi_object_t *result);

uint8_t *acpi_acpins_code;
size_t acpi_acpins_allocation = 0;
size_t acpi_acpins_size = 0;
size_t acpi_acpins_count = 0;
extern char aml_test[];

acpi_nsnode_t **acpi_namespace;
size_t acpi_ns_size = 0;
size_t acpi_ns_capacity = NAMESPACE_WINDOW;

void acpins_load_table(void *);

// Helper function to allocate a acpi_nsnode_t.
acpi_nsnode_t *acpins_create_nsnode()
{
    acpi_nsnode_t *node = acpi_malloc(sizeof(acpi_nsnode_t));
    if(!node)
        return NULL;
    acpi_memset(node, 0, sizeof(acpi_nsnode_t));
    return node;
}

// Helper function to allocate a acpi_nsnode_t.
acpi_nsnode_t *acpins_create_nsnode_or_die()
{
    acpi_nsnode_t *node = acpins_create_nsnode();
    if(!node)
        acpi_panic("could not allocate new namespace node\n");
    return node;
}

// Installs the nsnode to the namespace.
void acpins_install_nsnode(acpi_nsnode_t *node)
{
    // Classical doubling strategy to grow the namespace table.
    if(acpi_ns_size == acpi_ns_capacity)
    {
        size_t new_capacity = acpi_ns_capacity * 2;
        if(!new_capacity)
            new_capacity = NAMESPACE_WINDOW;
        acpi_nsnode_t **new_array;
        new_array = acpi_realloc(acpi_namespace, sizeof(acpi_nsnode_t *) * new_capacity);
        if(!new_array)
            acpi_panic("could not reallocate namespace table\n");
        acpi_namespace = new_array;
        acpi_ns_capacity = new_capacity;
    }

    /*acpi_debug("created %s\n", node->path);*/
    acpi_namespace[acpi_ns_size++] = node;
}

// acpins_resolve_path(): Resolves a path
// Param:    char *fullpath - destination
// Param:    uint8_t *path - path to resolve
// Return:    size_t - size of path data parsed in AML

size_t acpins_resolve_path(acpi_nsnode_t *context, char *fullpath, uint8_t *path)
{
    size_t name_size = 0;
    size_t multi_count = 0;
    size_t current_count = 0;

    acpi_memset(fullpath, 0, ACPI_MAX_NAME);

    if(path[0] == ROOT_CHAR)
    {
        name_size = 1;
        fullpath[0] = ROOT_CHAR;
        fullpath[1] = 0;
        path++;
        if(acpi_is_name(path[0]) || path[0] == DUAL_PREFIX || path[0] == MULTI_PREFIX)
        {
            fullpath[1] = '.';
            fullpath[2] = 0;
            goto start;
        } else
            return name_size;
    }

    if(context)
        acpi_strcpy(fullpath, context->path);
    else
        acpi_strcpy(fullpath, "\\");
    fullpath[acpi_strlen(fullpath)] = '.';

start:
    while(path[0] == PARENT_CHAR)
    {
        path++;
        if(acpi_strlen(fullpath) <= 2)
            break;

        name_size++;
        fullpath[acpi_strlen(fullpath) - 5] = 0;
        acpi_memset(fullpath + acpi_strlen(fullpath), 0, 32);
    }

    if(path[0] == DUAL_PREFIX)
    {
        name_size += 9;
        path++;
        acpi_memcpy(fullpath + acpi_strlen(fullpath), path, 4);
        fullpath[acpi_strlen(fullpath)] = '.';
        acpi_memcpy(fullpath + acpi_strlen(fullpath), path + 4, 4);
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
            acpi_memcpy(fullpath + acpi_strlen(fullpath), path, 4);
            path += 4;
            current_count++;
            if(current_count >= multi_count)
                break;

            fullpath[acpi_strlen(fullpath)] = '.';
        }
    } else
    {
        name_size += 4;
        acpi_memcpy(fullpath + acpi_strlen(fullpath), path, 4);
    }

    return name_size;
}

// acpi_create_namespace(): Initializes the AML interpreter and creates the ACPI namespace
// Param:    void *dsdt - pointer to the DSDT
// Return:    Nothing

void acpi_create_namespace(void *dsdt)
{
    acpi_namespace = acpi_calloc(sizeof(acpi_nsnode_t *), acpi_ns_capacity);
    if(!acpi_namespace)
        acpi_panic("unable to allocate memory.\n");

    acpi_acpins_code = acpi_malloc(CODE_WINDOW);
    acpi_acpins_allocation = CODE_WINDOW;

    //acpins_load_table(aml_test);    // custom AML table just for testing

    // we need the FADT
    acpi_fadt = acpi_scan("FACP", 0);
    if(!acpi_fadt)
    {
        acpi_panic("unable to find ACPI FADT.\n");
    }

    // load the DSDT
    acpins_load_table(dsdt);

    // load all SSDTs
    size_t index = 0;
    acpi_aml_t *ssdt = acpi_scan("SSDT", index);
    while(ssdt != NULL)
    {
        acpins_load_table(ssdt);
        index++;
        ssdt = acpi_scan("SSDT", index);
    }

    // the PSDT is treated the same way as the SSDT
    // scan for PSDTs too for compatibility with some ACPI 1.0 PCs
    index = 0;
    acpi_aml_t *psdt = acpi_scan("PSDT", index);
    while(psdt != NULL)
    {
        acpins_load_table(psdt);
        index++;
        psdt = acpi_scan("PSDT", index);
    }

    // create the OS-defined objects first
    acpi_nsnode_t *osi_node = acpins_create_nsnode_or_die();
    osi_node->type = ACPI_NAMESPACE_METHOD;
    acpi_strcpy(osi_node->path, "\\._OSI");
    osi_node->method_flags = 0x01;
    osi_node->method_override = &acpi_do_osi_method;
    acpins_install_nsnode(osi_node);

    acpi_nsnode_t *os_node = acpins_create_nsnode_or_die();
    os_node->type = ACPI_NAMESPACE_METHOD;
    acpi_strcpy(os_node->path, "\\._OS_");
    os_node->method_flags = 0x00;
    os_node->method_override = &acpi_do_os_method;
    acpins_install_nsnode(os_node);

    acpi_nsnode_t *rev_node = acpins_create_nsnode_or_die();
    rev_node->type = ACPI_NAMESPACE_METHOD;
    acpi_strcpy(rev_node->path, "\\._REV");
    rev_node->method_flags = 0x00;
    rev_node->method_override = &acpi_do_rev_method;
    acpins_install_nsnode(rev_node);

    // create the namespace with all the objects
    // most of the functions are recursive
    acpins_register_scope(NULL, acpi_acpins_code, acpi_acpins_size);

    acpi_debug("ACPI namespace created, total of %d predefined objects.\n", acpi_ns_size);
}

// acpins_load_table(): Loads an AML table
// Param:    void *ptr - pointer to table
// Return:    Nothing

void acpins_load_table(void *ptr)
{
    acpi_aml_t *table = (acpi_aml_t*)ptr;
    while(acpi_acpins_size + table->header.length >= acpi_acpins_allocation)
    {
        acpi_acpins_allocation += CODE_WINDOW;
        acpi_acpins_code = acpi_realloc(acpi_acpins_code, acpi_acpins_allocation);
    }

    // copy the actual AML code
    acpi_memcpy(acpi_acpins_code + acpi_acpins_size, table->data, table->header.length - sizeof(acpi_header_t));
    acpi_acpins_size += (table->header.length - sizeof(acpi_header_t));

    acpi_debug("loaded AML table '%c%c%c%c', total %d bytes of AML code.\n", table->header.signature[0], table->header.signature[1], table->header.signature[2], table->header.signature[3], acpi_acpins_size);

    acpi_acpins_count++;
}

// acpins_register_scope(): Registers a scope
// Param:    uint8_t *data - data
// Param:    size_t size - size of scope in bytes
// Return:    Nothing

void acpins_register_scope(acpi_nsnode_t *scope, uint8_t *data, size_t size)
{
    size_t count = 0;
    size_t pkgsize;
    acpi_object_t predicate = {0};
    while(count < size)
    {
        switch(data[count])
        {
        case ZERO_OP:
        case ONE_OP:
        case ONES_OP:
        case NOP_OP:
            count++;
            break;

        case BYTEPREFIX:
            count += 2;
            break;
        case WORDPREFIX:
            count += 3;
            break;
        case DWORDPREFIX:
            count += 5;
            break;
        case QWORDPREFIX:
            count += 9;
            break;
        case STRINGPREFIX:
            count += acpi_strlen((const char *)&data[count]) + 1;
            break;

        case NAME_OP:
            count += acpins_create_name(scope, &data[count]);
            break;

        case ALIAS_OP:
            count += acpins_create_alias(scope, &data[count]);
            break;

        case SCOPE_OP:
            count += acpins_create_scope(scope, &data[count]);
            break;

        case METHOD_OP:
            count += acpins_create_method(scope, &data[count]);
            break;

        case BUFFER_OP:
        case PACKAGE_OP:
        case VARPACKAGE_OP:
            count++;
            acpi_parse_pkgsize(&data[count], &pkgsize);
            count += pkgsize;
            break;

        case BYTEFIELD_OP:
            count += acpins_create_bytefield(scope, &data[count]);
            break;
        case WORDFIELD_OP:
            count += acpins_create_wordfield(scope, &data[count]);
            break;
        case DWORDFIELD_OP:
            count += acpins_create_dwordfield(scope, &data[count]);
            break;
        case QWORDFIELD_OP:
            count += acpins_create_qwordfield(scope, &data[count]);
            break;

        case EXTOP_PREFIX:
            switch(data[count+1])
            {
            case MUTEX:
                count += acpins_create_mutex(scope, &data[count]);
                break;
            case OPREGION:
                count += acpins_create_opregion(scope, &data[count]);
                break;
            case FIELD:
                count += acpins_create_field(scope, &data[count]);
                break;
            case DEVICE:
                count += acpins_create_device(scope, &data[count]);
                break;
            case THERMALZONE:
                count += acpins_create_thermalzone(scope, &data[count]);
                break;
            case INDEXFIELD:
                count += acpins_create_indexfield(scope, &data[count]);
                break;
            case PROCESSOR:
                count += acpins_create_processor(scope, &data[count]);
                break;

            default:
                acpi_panic("undefined opcode, sequence: %02X %02X %02X %02X\n", data[count], data[count+1], data[count+2], data[count+3]);
            }
            break;

        case IF_OP:
            count++;
            size_t predicate_skip = acpi_parse_pkgsize(&data[count], &pkgsize);
            size_t if_end = count + pkgsize;

            count += predicate_skip;

            count += acpi_eval_object(&predicate, scope, &data[count]);
            if(predicate.integer == 0)
                count = if_end;

            break;

        case ELSE_OP:
            count++;
            acpi_parse_pkgsize(&data[count], &pkgsize);
            count += pkgsize;
            break;

        default:
            acpi_panic("undefined opcode, sequence: %02X %02X %02X %02X\n", data[count], data[count+1], data[count+2], data[count+3]);
        }
    }
}

// acpins_create_scope(): Creates a scope in the namespace
// Param:    void *data - scope data
// Return:    size_t - size of scope in bytes

size_t acpins_create_scope(acpi_nsnode_t *parent, void *data)
{
    uint8_t *scope = (uint8_t*)data;
    size_t size;
    size_t pkgsize;

    pkgsize = acpi_parse_pkgsize(scope + 1, &size);

    // register the scope
    scope += pkgsize + 1;
    acpi_nsnode_t *node = acpins_create_nsnode_or_die();
    size_t name_length = acpins_resolve_path(parent, node->path, scope);

    //acpi_debug("scope %s, size %d bytes\n", node->path, size);

    // put the scope in the namespace
    node->type = ACPI_NAMESPACE_SCOPE;
    node->size = size - pkgsize - name_length;
    node->pointer = (void*)(data + 1 + pkgsize + name_length);
    acpins_install_nsnode(node);

    // register the child objects of the scope
    acpins_register_scope(node, (uint8_t*)data + 1 + pkgsize + name_length,
            size - pkgsize - name_length);

    return size + 1;
}

// acpins_create_opregion(): Creates an OpRegion
// Param:    void *data - OpRegion data
// Return:    size_t - total size of OpRegion in bytes

size_t acpins_create_opregion(acpi_nsnode_t *parent, void *data)
{
    uint8_t *opregion = (uint8_t*)data;
    opregion += 2;        // skip EXTOP_PREFIX and OPREGION opcodes

    // create a namespace object for the opregion
    acpi_nsnode_t *node = acpins_create_nsnode_or_die();
    size_t name_length = acpins_resolve_path(parent, node->path, opregion);

    opregion = (uint8_t*)data;

    size_t size = name_length + 2;
    acpi_object_t object = {0};
    uint64_t integer;
    size_t integer_size;

    node->op_address_space = opregion[size];
    size++;

	// FIXME: Do we want to resolve paths relative to the parent or the opregion?
    integer_size = acpi_eval_object(&object, parent, &opregion[size]);
    integer = object.integer;
    if(integer_size == 0)
    {
        acpi_panic("undefined opcode, sequence: %02X %02X %02X %02X\n", opregion[size], opregion[size+1], opregion[size+2], opregion[size+3]);
    }

    node->op_base = integer;
    size += integer_size;

    integer_size = acpi_eval_integer(&opregion[size], &integer);
    if(integer_size == 0)
    {
        acpi_panic("undefined opcode, sequence: %02X %02X %02X %02X\n", opregion[size], opregion[size+1], opregion[size+2], opregion[size+3]);
    }

    node->op_length = integer;
    size += integer_size;

    /*acpi_debug("OpRegion %s: ", node->path);
    switch(node->op_address_space)
    {
    case OPREGION_MEMORY:
        acpi_debug("MMIO: 0x%X-0x%X\n", node->op_base, node->op_base + node->op_length);
        break;
    case OPREGION_IO:
        acpi_debug("I/O port: 0x%X-0x%X\n", (uint16_t)(node->op_base), (uint16_t)(node->op_base + node->op_length));
        break;
    case OPREGION_PCI:
        acpi_debug("PCI config: 0x%X-0x%X\n", (uint16_t)(node->op_base), (uint16_t)(node->op_base + node->op_length));
        break;
    case OPREGION_EC:
        acpi_debug("embedded controller: 0x%X-0x%X\n", (uint8_t)(node->op_base), (uint8_t)(node->op_base + node->op_length));
        break;
    case OPREGION_CMOS:
        acpi_debug("CMOS RAM: 0x%X-0x%X\n", (uint8_t)(node->op_base), (uint8_t)(node->op_base + node->op_length));
        break;

    default:
        acpi_panic("unsupported address space ID 0x%X\n", node->op_address_space);
    }*/

    acpins_install_nsnode(node);
    return size;
}

// acpins_create_field(): Creates a Field object in the namespace
// Param:    void *data - pointer to field data
// Return:    size_t - total size of field in bytes

size_t acpins_create_field(acpi_nsnode_t *parent, void *data)
{
    uint8_t *field = (uint8_t*)data;
    field += 2;        // skip opcode

    // package size
    size_t pkgsize, size;

    pkgsize = acpi_parse_pkgsize(field, &size);
    field += pkgsize;

    // determine name of opregion
    acpi_nsnode_t *opregion;
    char opregion_name[ACPI_MAX_NAME];
    size_t name_size = 0;

    name_size = acpins_resolve_path(parent, opregion_name, field);

    opregion = acpins_resolve(opregion_name);
    if(!opregion)
    {
        acpi_debug("error parsing field for non-existant OpRegion %s, ignoring...\n", opregion_name);
        return size + 2;
    }

    // parse the field's entries now
    uint8_t field_flags;
    field = (uint8_t*)data + 2 + pkgsize + name_size;
    field_flags = field[0];

    /*acpi_debug("field for OpRegion %s, flags 0x%X (", opregion->path, field_flags);
    switch(field_flags & 0x0F)
    {
    case FIELD_ANY_ACCESS:
        acpi_debug("any ");
        break;
    case FIELD_BYTE_ACCESS:
        acpi_debug("byte ");
        break;
    case FIELD_WORD_ACCESS:
        acpi_debug("word ");
        break;
    case FIELD_DWORD_ACCESS:
        acpi_debug("dword ");
        break;
    case FIELD_QWORD_ACCESS:
        acpi_debug("qword ");
        break;
    default:
        acpi_debug("undefined access size: assuming any, ");
        break;
    }

    if(field_flags & FIELD_LOCK)
        acpi_debug("lock ");

    switch((field_flags >> 5) & 0x0F)
    {
    case FIELD_PRESERVE:
        acpi_debug("preserve");
        break;
    case FIELD_WRITE_ONES:
        acpi_debug("ones");
        break;
    case FIELD_WRITE_ZEROES:
        acpi_debug("zeroes");
        break;
    default:
        acpi_debug("undefined update type");
        break;
    }

    acpi_debug(")\n");*/

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

            skip_size = acpi_parse_pkgsize(field, &skip_bits);
            current_offset += skip_bits;

            field += skip_size;
            byte_count += skip_size;
        }

        while(field[0] == 1)        // access field, unimplemented
        {
            field += 3;
            byte_count += 3;
        }

        if(!acpi_is_name(field[0]))
        {
            field++;
            byte_count++;
        }

        if(byte_count >= size)
            break;

        //acpi_debug("field %c%c%c%c: size %d bits, at bit offset %d\n", field[0], field[1], field[2], field[3], field[4], current_offset);
        acpi_nsnode_t *node = acpins_create_nsnode_or_die();
        node->type = ACPI_NAMESPACE_FIELD;

        name_size = acpins_resolve_path(parent, node->path, &field[0]);
        field += name_size;
        byte_count += name_size;

        // FIXME: This looks odd. Why do we insert a dot in the middle of the path?
        node->path[acpi_strlen(parent->path)] = '.';
        acpi_strcpy(node->field_opregion, opregion->path);
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

size_t acpins_create_method(acpi_nsnode_t *parent, void *data)
{
    uint8_t *method = (uint8_t*)data;
    method++;        // skip over METHOD_OP

    size_t size, pkgsize;
    pkgsize = acpi_parse_pkgsize(method, &size);
    method += pkgsize;

    // create a namespace object for the method
    acpi_nsnode_t *node = acpins_create_nsnode_or_die();
    size_t name_length = acpins_resolve_path(parent, node->path, method);

    // get the method's flags
    method = (uint8_t*)data;
    method += pkgsize + name_length + 1;

    // put the method in the namespace
    node->type = ACPI_NAMESPACE_METHOD;
    node->method_flags = method[0];
    node->pointer = (void*)(method + 1);
    node->size = size - pkgsize - name_length - 1;

    /*acpi_debug("control method %s, flags 0x%X (argc %d ", node->path, method[0], method[0] & METHOD_ARGC_MASK);
    if(method[0] & METHOD_SERIALIZED)
        acpi_debug("serialized");
    else
        acpi_debug("non-serialized");

    acpi_debug(")\n");*/

    acpins_install_nsnode(node);
    return size + 1;
}

// acpins_create_device(): Creates a device scope in the namespace
// Param:    void *data - device scope data
// Return:    size_t - size of device scope in bytes

size_t acpins_create_device(acpi_nsnode_t *parent, void *data)
{
    uint8_t *device = (uint8_t*)data;
    size_t size;
    size_t pkgsize;

    pkgsize = acpi_parse_pkgsize(device + 2, &size);

    // register the device
    device += pkgsize + 2;

    acpi_nsnode_t *node = acpins_create_nsnode_or_die();
    size_t name_length = acpins_resolve_path(parent, node->path, device);

    //acpi_debug("device scope %s, size %d bytes\n", node->path, size);

    // put the device scope in the namespace
    node->type = ACPI_NAMESPACE_DEVICE;
    node->size = size - pkgsize - name_length;
    node->pointer = (void*)(data + 2 + pkgsize + name_length);
    acpins_install_nsnode(node);

    // register the child objects of the device scope
    acpins_register_scope(node, (uint8_t*)data + 2 + pkgsize + name_length,
            size - pkgsize - name_length);

    return size + 2;
}

// acpins_create_thermalzone(): Creates a thermal zone scope in the namespace
// Param:    void *data - thermal zone scope data
// Return:    size_t - size of thermal zone scope in bytes

size_t acpins_create_thermalzone(acpi_nsnode_t *parent, void *data)
{
    uint8_t *thermalzone = (uint8_t*)data;
    size_t size;
    size_t pkgsize;

    pkgsize = acpi_parse_pkgsize(thermalzone + 2, &size);

    // register the thermalzone
    thermalzone += pkgsize + 2;

    acpi_nsnode_t *node = acpins_create_nsnode_or_die();
    size_t name_length = acpins_resolve_path(parent, node->path, thermalzone);

    // put the device scope in the namespace
    node->type = ACPI_NAMESPACE_THERMALZONE;
    node->size = size - pkgsize - name_length;
    node->pointer = (void*)(data + 2 + pkgsize + name_length);

    acpins_install_nsnode(node);

    // register the child objects of the thermal zone scope
    acpins_register_scope(node, (uint8_t*)data + 2 + pkgsize + name_length,
            size - pkgsize - name_length);

    return size + 2;
}


// acpins_create_name(): Creates a name in the namespace
// Param:    void *data - pointer to data
// Return:    size_t - total size in bytes, for skipping

size_t acpins_create_name(acpi_nsnode_t *parent, void *data)
{
    uint8_t *name = (uint8_t*)data;
    name++;            // skip NAME_OP

    // create a namespace object for the name object
    acpi_nsnode_t *node = acpins_create_nsnode_or_die();
    size_t name_length = acpins_resolve_path(parent, node->path, name);

    name += name_length;
    node->type = ACPI_NAMESPACE_NAME;

    size_t return_size = name_length + 1;

    if(name[0] == PACKAGE_OP)
    {
        node->object.type = ACPI_PACKAGE;
        node->object.package = acpi_calloc(sizeof(acpi_object_t), ACPI_MAX_PACKAGE_ENTRIES);
        node->object.package_size = acpins_create_package(parent,
                node->object.package, &name[0]);

        //acpi_debug("package object %s, entry count %d\n", node->path, node->object.package_size);
        acpins_install_nsnode(node);
        return return_size;
    }

    uint64_t integer;
    size_t integer_size = acpi_eval_integer(name, &integer);
    size_t pkgsize;
    acpi_object_t object = {0};
    size_t object_size;

    if(integer_size != 0)
    {
        node->object.type = ACPI_INTEGER;
        node->object.integer = integer;
    } else if(name[0] == BUFFER_OP)
    {
        node->object.type = ACPI_BUFFER;
        pkgsize = acpi_parse_pkgsize(&name[1], &node->object.buffer_size);
        node->object.buffer = &name[0] + pkgsize + 1;

		// FIXME: Do we want to resolve paths relative to the parent or the name?
        object_size = acpi_eval_object(&object, parent, node->object.buffer);
        node->object.buffer += object_size;
        node->object.buffer_size = object.integer;
    } else if(name[0] == STRINGPREFIX)
    {
        node->object.type = ACPI_STRING;
        node->object.string = (char*)&name[1];
    } else
    {
        acpi_panic("undefined opcode in Name(), sequence: %02X %02X %02X %02X\n", name[0], name[1], name[2], name[3]);
    }

    /*if(node->object.type == ACPI_INTEGER)
        acpi_debug("integer object %s, value 0x%X\n", node->path, node->object.integer);
    else if(node->object.type == ACPI_BUFFER)
        acpi_debug("buffer object %s\n", node->path);
    else if(node->object.type == ACPI_STRING)
        acpi_debug("string object %s: '%s'\n", node->path, node->object.string);*/

    acpins_install_nsnode(node);
    return return_size;
}

// acpins_create_alias(): Creates an alias object in the namespace
// Param:    void *data - pointer to data
// Return:    size_t - total size in bytes, for skipping

size_t acpins_create_alias(acpi_nsnode_t *parent, void *data)
{
    size_t return_size = 1;
    uint8_t *alias = (uint8_t*)data;
    alias++;        // skip ALIAS_OP

    size_t name_size;

    acpi_nsnode_t *node = acpins_create_nsnode_or_die();
    node->type = ACPI_NAMESPACE_ALIAS;
    name_size = acpins_resolve_path(parent, node->alias, alias);

    return_size += name_size;
    alias += name_size;

    name_size = acpins_resolve_path(parent, node->path, alias);

    //acpi_debug("alias %s for object %s\n", node->path, node->alias);

    acpins_install_nsnode(node);
    return_size += name_size;
    return return_size;
}

// acpins_create_mutex(): Creates a Mutex object in the namespace
// Param:    void *data - pointer to data
// Return:    size_t - total size in bytes, for skipping

size_t acpins_create_mutex(acpi_nsnode_t *parent, void *data)
{
    size_t return_size = 2;
    uint8_t *mutex = (uint8_t*)data;
    mutex += 2;        // skip MUTEX_OP

    acpi_nsnode_t *node = acpins_create_nsnode_or_die();
    node->type = ACPI_NAMESPACE_MUTEX;
    size_t name_size = acpins_resolve_path(parent, node->path, mutex);

    return_size += name_size;
    return_size++;

    //acpi_debug("mutex object %s\n", node->path);

    acpins_install_nsnode(node);
    return return_size;
}

// acpins_create_indexfield(): Creates an IndexField object in the namespace
// Param:    void *data - pointer to indexfield data
// Return:    size_t - total size of indexfield in bytes

size_t acpins_create_indexfield(acpi_nsnode_t *parent, void *data)
{
    uint8_t *indexfield = (uint8_t*)data;
    indexfield += 2;        // skip INDEXFIELD_OP

    size_t pkgsize, size;
    pkgsize = acpi_parse_pkgsize(indexfield, &size);

    indexfield += pkgsize;

    // index and data
    char indexr[ACPI_MAX_NAME], datar[ACPI_MAX_NAME];
    acpi_memset(indexr, 0, ACPI_MAX_NAME);
    acpi_memset(datar, 0, ACPI_MAX_NAME);

    indexfield += acpins_resolve_path(parent, indexr, indexfield);
    indexfield += acpins_resolve_path(parent, datar, indexfield);

    uint8_t flags = indexfield[0];

    /*acpi_debug("IndexField index %s data %s, flags 0x%X (", indexr, datar, flags);
    switch(flags & 0x0F)
    {
    case FIELD_ANY_ACCESS:
        acpi_debug("any ");
        break;
    case FIELD_BYTE_ACCESS:
        acpi_debug("byte ");
        break;
    case FIELD_WORD_ACCESS:
        acpi_debug("word ");
        break;
    case FIELD_DWORD_ACCESS:
        acpi_debug("dword ");
        break;
    case FIELD_QWORD_ACCESS:
        acpi_debug("qword ");
        break;
    default:
        acpi_debug("undefined access size: assuming any, ");
        break;
    }

    if(flags & FIELD_LOCK)
        acpi_debug("lock ");

    switch((flags >> 5) & 0x0F)
    {
    case FIELD_PRESERVE:
        acpi_debug("preserve");
        break;
    case FIELD_WRITE_ONES:
        acpi_debug("ones");
        break;
    case FIELD_WRITE_ZEROES:
        acpi_debug("zeroes");
        break;
    default:
        acpi_debug("undefined update type");
        break;
    }

    acpi_debug(")\n");*/

    indexfield++;            // actual field list
    size_t byte_count = (size_t)((size_t)indexfield - (size_t)data);

    uint64_t current_offset = 0;
    size_t skip_size, skip_bits;

    while(byte_count < size)
    {
        while(indexfield[0] == 0)        // skipping?
        {
            indexfield++;
            byte_count++;

            skip_size = acpi_parse_pkgsize(indexfield, &skip_bits);
            current_offset += skip_bits;

            indexfield += skip_size;
            byte_count += skip_size;
        }

        //acpi_debug("indexfield %c%c%c%c: size %d bits, at bit offset %d\n", indexfield[0], indexfield[1], indexfield[2], indexfield[3], indexfield[4], current_offset);
        acpi_nsnode_t *node = acpins_create_nsnode_or_die();
        node->type = ACPI_NAMESPACE_INDEXFIELD;
        // FIXME: This looks odd. Why don't we all acpins_resolve_path()?
        acpi_memcpy(node->path, parent->path, acpi_strlen(parent->path));
        node->path[acpi_strlen(parent->path)] = '.';
        acpi_memcpy(node->path + acpi_strlen(parent->path) + 1, indexfield, 4);

        acpi_strcpy(node->indexfield_data, datar);
        acpi_strcpy(node->indexfield_index, indexr);
        node->indexfield_flags = flags;
        node->indexfield_size = indexfield[4];
        node->indexfield_offset = current_offset;

        current_offset += (uint64_t)(indexfield[4]);
        acpins_install_nsnode(node);

        indexfield += 5;
        byte_count += 5;
    }

    return size + 2;
}

// acpins_create_package(): Creates a package object
// Param:    acpi_object_t *destination - where to create package
// Param:    void *data - package data
// Return:    size_t - size in entries

size_t acpins_create_package(acpi_nsnode_t *context, acpi_object_t *destination, void *data)
{
    uint8_t *package = (uint8_t*)data;
    package++;        // skip PACKAGE_OP

    size_t pkgsize, size;
    pkgsize = acpi_parse_pkgsize(package, &size);
    size_t size_bak = size;

    package += pkgsize;
    uint8_t count = package[0];        // entry count
    if(!count)
        return 0;

    // parse actual package contents
    package++;
    uint8_t i = 0;
    size_t j = 0;
    size_t integer_size;
    uint64_t integer;

    //acpi_debug("acpins_create_package: start:\n");

    while(i < count && j + pkgsize + 1 < size_bak)
    {
        integer_size = acpi_eval_integer(&package[j], &integer);
        if(integer_size != 0)
        {
            destination[i].type = ACPI_INTEGER;
            destination[i].integer = integer;

            //acpi_debug("  index %d: integer %d\n", i, integer);
            i++;
            j += integer_size;
        } else if(package[j] == STRINGPREFIX)
        {
            destination[i].type = ACPI_STRING;
            destination[i].string = (char*)&package[j+1];

            //acpi_debug("  index %d: string %s\n", i, destination[i].string);
            i++;

            j += acpi_strlen((char*)&package[j]) + 1;
        } else if(acpi_is_name(package[j]) || package[j] == ROOT_CHAR || package[j] == PARENT_CHAR || package[j] == MULTI_PREFIX || package[j] == DUAL_PREFIX)
        {
            destination[i].type = ACPI_NAME;
            j += acpins_resolve_path(context, destination[i].name, &package[j]);

            //acpi_debug("  index %d: name %s\n", i, destination[i].name);
            i++;
        } else if(package[j] == PACKAGE_OP)
        {
            // Package within package!
            destination[i].type = ACPI_PACKAGE;
            destination[i].package = acpi_calloc(sizeof(acpi_object_t), ACPI_MAX_PACKAGE_ENTRIES);

            //acpi_debug("  index %d: package\n", i);

            destination[i].package_size = acpins_create_package(context,
                    destination[i].package, &package[j]);

            j++;
            acpi_parse_pkgsize(&package[j], &size);
            j += size;
            i++;
        } else if(package[j] == BUFFER_OP)
        {
            // Buffer within package
            destination[i].type = ACPI_BUFFER;
            j++;

            size_t buffer_size;
            size_t pkgsize2 = acpi_parse_pkgsize(&package[j], &buffer_size);
            destination[i].buffer = (void*)&package[j + pkgsize2];
            destination[i].buffer_size = buffer_size;

            j += buffer_size;
            i++;
        } else
        {
            // Undefined here
            acpi_panic("undefined opcode in Package(), sequence: %02X %02X %02X %02X\n", package[j], package[j+1], package[j+2], package[j+3]);
        }
    }

    //acpi_debug("acpins_create_package: end.\n");
    return (size_t)count;
}

// acpins_create_processor(): Creates a Processor object in the namespace
// Param:    void *data - pointer to data
// Return:    size_t - total size in bytes, for skipping

size_t acpins_create_processor(acpi_nsnode_t *parent, void *data)
{
    uint8_t *processor = (uint8_t*)data;
    processor += 2;            // skip over PROCESSOR_OP

    size_t pkgsize, size;
    pkgsize = acpi_parse_pkgsize(processor, &size);
    processor += pkgsize;

    acpi_nsnode_t *node = acpins_create_nsnode_or_die();
    node->type = ACPI_NAMESPACE_PROCESSOR;
    size_t name_size = acpins_resolve_path(parent, node->path, processor);
    processor += name_size;

    node->cpu_id = processor[0];

    //acpi_debug("processor %s ACPI ID %d\n", node->path, node->cpu_id);

    acpins_install_nsnode(node);

    return size + 2;
}

// acpins_create_bytefield(): Creates a ByteField object for a buffer in the namespace
// Param:    void *data - pointer to data
// Return:    size_t - total size in bytes, for skipping

size_t acpins_create_bytefield(acpi_nsnode_t *parent, void *data)
{
    uint8_t *bytefield = (uint8_t*)data;
    bytefield++;        // skip BYTEFIELD_OP
    size_t return_size = 1;

    acpi_nsnode_t *node = acpins_create_nsnode_or_die();
    node->type = ACPI_NAMESPACE_BUFFER_FIELD;

    // buffer name
    size_t name_size;
    name_size = acpins_resolve_path(parent, node->buffer, bytefield);

    return_size += name_size;
    bytefield += name_size;

    size_t integer_size;
    uint64_t integer;
    integer_size = acpi_eval_integer(bytefield, &integer);

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

size_t acpins_create_wordfield(acpi_nsnode_t *parent, void *data)
{
    uint8_t *wordfield = (uint8_t*)data;
    wordfield++;        // skip WORDFIELD_OP
    size_t return_size = 1;

    acpi_nsnode_t *node = acpins_create_nsnode_or_die();
    node->type = ACPI_NAMESPACE_BUFFER_FIELD;

    // buffer name
    size_t name_size;
    name_size = acpins_resolve_path(parent, node->buffer, wordfield);

    return_size += name_size;
    wordfield += name_size;

    size_t integer_size;
    uint64_t integer;
    integer_size = acpi_eval_integer(wordfield, &integer);

    node->buffer_offset = integer * 8;
    node->buffer_size = 16;    // bits

    return_size += integer_size;
    wordfield += integer_size;

    name_size = acpins_resolve_path(parent, node->path, wordfield);

    //acpi_debug("field %s for buffer %s, offset %d size %d bits\n", node->path, node->buffer, node->buffer_offset, node->buffer_size);

    acpins_install_nsnode(node);
    return_size += name_size;
    return return_size;
}

// acpins_create_dwordfield(): Creates a DwordField object for a buffer in the namespace
// Param:    void *data - pointer to data
// Return:    size_t - total size in bytes, for skipping

size_t acpins_create_dwordfield(acpi_nsnode_t *parent, void *data)
{
    uint8_t *dwordfield = (uint8_t*)data;
    dwordfield++;        // skip DWORDFIELD_OP
    size_t return_size = 1;

    acpi_nsnode_t *node = acpins_create_nsnode_or_die();
    node->type = ACPI_NAMESPACE_BUFFER_FIELD;

    // buffer name
    size_t name_size;
    name_size = acpins_resolve_path(parent, node->buffer, dwordfield);

    return_size += name_size;
    dwordfield += name_size;

    size_t integer_size;
    uint64_t integer;
    integer_size = acpi_eval_integer(dwordfield, &integer);

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

size_t acpins_create_qwordfield(acpi_nsnode_t *parent, void *data)
{
    uint8_t *qwordfield = (uint8_t*)data;
    qwordfield++;        // skip QWORDFIELD_OP
    size_t return_size = 1;

    acpi_nsnode_t *node = acpins_create_nsnode_or_die();
    node->type = ACPI_NAMESPACE_BUFFER_FIELD;

    // buffer name
    size_t name_size;
    name_size = acpins_resolve_path(parent, node->buffer, qwordfield);

    return_size += name_size;
    qwordfield += name_size;

    size_t integer_size;
    uint64_t integer;
    integer_size = acpi_eval_integer(qwordfield, &integer);

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
// Return:    acpi_nsnode_t * - pointer to namespace object, NULL on error

acpi_nsnode_t *acpins_resolve(char *path)
{
    size_t i = 0;

    if(path[0] == ROOT_CHAR)        // full path?
    {
        // yep, search for the absolute path
        while(i < acpi_ns_size)
        {
            if(acpi_strcmp(acpi_namespace[i]->path, path) == 0)
                return acpi_namespace[i];

            else
                i++;
        }

        return NULL;
    } else            // 4-char name here
    {
        while(i < acpi_ns_size)
        {
            if(acpi_memcmp(acpi_namespace[i]->path + acpi_strlen(acpi_namespace[i]->path) - 4, path, 4) == 0)
                return acpi_namespace[i];

            else
                i++;
        }

        return NULL;
    }
}

// acpins_get_device(): Returns a device by its index
// Param:    size_t index - index
// Return:    acpi_nsnode_t * - device handle, NULL on error

acpi_nsnode_t *acpins_get_device(size_t index)
{
    size_t i = 0, j = 0;
    while(j < acpi_ns_size)
    {
        if(acpi_namespace[j]->type == ACPI_NAMESPACE_DEVICE)
            i++;

        if(i > index)
            return acpi_namespace[j];

        j++;
    }

    return NULL;
}

// acpins_get_deviceid(): Returns a device by its index and its ID
// Param:    size_t index - index
// Param:    acpi_object_t *id - device ID
// Return:    acpi_nsnode_t * - device handle, NULL on error

acpi_nsnode_t *acpins_get_deviceid(size_t index, acpi_object_t *id)
{
    size_t i = 0, j = 0;

    acpi_nsnode_t *handle;
    char path[ACPI_MAX_NAME];
    acpi_object_t device_id = {0};

    handle = acpins_get_device(j);
    while(handle != NULL)
    {
        // read the ID of the device
        acpi_strcpy(path, handle->path);
        acpi_strcpy(path + acpi_strlen(path), "._HID");    // hardware ID
        acpi_memset(&device_id, 0, sizeof(acpi_object_t));
        if(acpi_eval(&device_id, path) != 0)
        {
            acpi_strcpy(path + acpi_strlen(path) - 5, "._CID");    // compatible ID
            acpi_memset(&device_id, 0, sizeof(acpi_object_t));
            acpi_eval(&device_id, path);
        }

        if(device_id.type == ACPI_INTEGER && id->type == ACPI_INTEGER)
        {
            if(device_id.integer == id->integer)
                i++;
        } else if(device_id.type == ACPI_STRING && id->type == ACPI_STRING)
        {
            if(acpi_strcmp(device_id.string, id->string) == 0)
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
// Return:  acpi_nsnode_t * - child node, NULL if non-existant

acpi_nsnode_t *acpins_enum(char *parent, size_t index)
{
    index++;
    size_t parent_size = acpi_strlen(parent);
    for(size_t i = 0; i < acpi_ns_size; i++)
    {
        if(!acpi_memcmp(parent, acpi_namespace[i]->path, parent_size))
        {
            if(!index)
                return acpi_namespace[i];
            else
                index--;
        }
    }

    return NULL;
}

