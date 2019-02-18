
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 by Omar Muhamed
 */

#include <lai/core.h>

/* ACPI Control Method Execution */
/* Type2Opcode := DefAcquire | DefAdd | DefAnd | DefBuffer | DefConcat |
   DefConcatRes | DefCondRefOf | DefCopyObject | DefDecrement |
   DefDerefOf | DefDivide | DefFindSetLeftBit | DefFindSetRightBit |
   DefFromBCD | DefIncrement | DefIndex | DefLAnd | DefLEqual |
   DefLGreater | DefLGreaterEqual | DefLLess | DefLLessEqual | DefMid |
   DefLNot | DefLNotEqual | DefLoadTable | DefLOr | DefMatch | DefMod |
   DefMultiply | DefNAnd | DefNOr | DefNot | DefObjectType | DefOr |
   DefPackage | DefVarPackage | DefRefOf | DefShiftLeft | DefShiftRight |
   DefSizeOf | DefStore | DefSubtract | DefTimer | DefToBCD | DefToBuffer |
   DefToDecimalString | DefToHexString | DefToInteger | DefToString |
   DefWait | DefXOr | UserTermObj */

// acpi_exec_resolve(): Resolves a name during control method execution
// Param:    char *path - 4-char object name or full path
// Return:    acpi_nsnode_t * - pointer to namespace object, NULL on error

acpi_nsnode_t *acpi_exec_resolve(char *path)
{
    acpi_nsnode_t *object;
    object = acpins_resolve(path);

    if(acpi_strlen(path) == 4)
        return acpins_resolve(path);

    while(!object && acpi_strlen(path) > 6)
    {
        acpi_memmove(path + acpi_strlen(path) - 9, path + acpi_strlen(path) - 4, 5);
        object = acpins_resolve(path);
        if(object != NULL)
            goto resolve_alias;
    }

    if(object == NULL)
        return NULL;

resolve_alias:
    // resolve Aliases too
    while(object->type == ACPI_NAMESPACE_ALIAS)
    {
        object = acpins_resolve(object->alias);
        if(!object)
            return NULL;
    }

    return object;
}

// acpi_free_package(): Frees a package object and all its children
// Param:   acpi_object_t *object
// Return:  Nothing

static void acpi_free_package(acpi_object_t *object)
{
    for(int i = 0; i < object->package_size; i++)
        acpi_free_object(&object->package[i]);
    acpi_free(object->package);
}

void acpi_free_object(acpi_object_t *object)
{
    if(object->type == ACPI_BUFFER)
        acpi_free(object->buffer);
    else if(object->type == ACPI_PACKAGE)
        acpi_free_package(object);

    acpi_memset(object, 0, sizeof(acpi_object_t));
}

// Helper function for acpi_move_object() and acpi_copy_object().
void acpi_swap_object(acpi_object_t *first, acpi_object_t *second) {
    acpi_object_t temp = *first;
    *first = *second;
    *second = temp;
}

// acpi_move_object(): Moves an object: instead of making a deep copy,
//                     the pointers are exchanged and the source object is reset to zero.
// Param & Return: See acpi_copy_object().

void acpi_move_object(acpi_object_t *destination, acpi_object_t *source)
{
    // Move-by-swap idiom. This handles move-to-self operations correctly.
    acpi_object_t temp = {0};
    acpi_swap_object(&temp, source);
    acpi_swap_object(&temp, destination);
    acpi_free_object(&temp);
}

// acpi_clone_buffer(): Clones a buffer object
// Param:    acpi_object_t *destination - destination
// Param:    acpi_object_t *source - source
// Return:   Nothing

static void acpi_clone_buffer(acpi_object_t *destination, acpi_object_t *source)
{
    destination->type = ACPI_BUFFER;
    destination->buffer_size = source->buffer_size;
    destination->buffer = acpi_malloc(source->buffer_size);
    if(!destination->buffer)
        acpi_panic("unable to allocate memory for buffer object.\n");

    acpi_memcpy(destination->buffer, source->buffer, source->buffer_size);
}

// acpi_clone_string(): Clones a string object
// Param:    acpi_object_t *destination - destination
// Param:    acpi_object_t *source - source
// Return:   Nothing

static void acpi_clone_string(acpi_object_t *destination, acpi_object_t *source)
{
    destination->type = ACPI_STRING;
    destination->string = acpi_malloc(acpi_strlen(source->string) + 1);
    if(!destination->string)
        acpi_panic("unable to allocate memory for string object.\n");

    acpi_strcpy(destination->string, source->string);
}

// acpi_clone_package(): Clones a package object
// Param:    acpi_object_t *destination - destination
// Param:    acpi_object_t *source - source
// Return:   Nothing

static void acpi_clone_package(acpi_object_t *destination, acpi_object_t *source)
{
    destination->type = ACPI_PACKAGE;
    destination->package_size = source->package_size;
    destination->package = acpi_calloc(source->package_size, sizeof(acpi_object_t));
    if(!destination->package)
        acpi_panic("unable to allocate memory for package object.\n");

    for(int i = 0; i < source->package_size; i++)
        acpi_copy_object(&destination->package[i], &source->package[i]);
}

// acpi_copy_object(): Copies an object
// Param:    acpi_object_t *destination - destination
// Param:    acpi_object_t *source - source
// Return:   Nothing

void acpi_copy_object(acpi_object_t *destination, acpi_object_t *source)
{
    // First, clone into a temporary object.
    acpi_object_t temp;
    if(source->type == ACPI_STRING)
        acpi_clone_string(&temp, source);
    else if(source->type == ACPI_BUFFER)
        acpi_clone_buffer(&temp, source);
    else if(source->type == ACPI_PACKAGE)
        acpi_clone_package(&temp, source);
    else
        temp = *source;

    // Afterwards, swap to the destination. This handles copy-to-self correctly.
    acpi_swap_object(destination, &temp);
    acpi_free_object(&temp);
}

// acpi_alias_copy(): Creates a reference to the storage of an object.

void acpi_alias_object(acpi_object_t *alias, acpi_object_t *object)
{
    if(object->type == ACPI_STRING)
    {
        alias->type = ACPI_STRING_REFERENCE;
        alias->string = object->string;
    }else if(object->type == ACPI_BUFFER)
    {
        alias->type = ACPI_BUFFER_REFERENCE;
        alias->buffer_size = object->buffer_size;
        alias->buffer = object->buffer;
    }else if(object->type == ACPI_PACKAGE)
    {
        alias->type = ACPI_PACKAGE_REFERENCE;
        alias->package_size = object->package_size;
        alias->package = object->package;
    }else
        acpi_panic("object type %d is not valid for acpi_alias_object()\n", object->type);
}

void acpi_load_ns(acpi_nsnode_t *source, acpi_object_t *object)
{
    if(source->type == ACPI_NAMESPACE_NAME)
        acpi_copy_object(object, &source->object);
    else if(source->type == ACPI_NAMESPACE_FIELD || source->type == ACPI_NAMESPACE_INDEXFIELD)
        // It's an Operation Region field; perform IO in that region.
        acpi_read_opregion(object, source);
    else if(source->type == ACPI_NAMESPACE_DEVICE)
    {
        object->type = ACPI_HANDLE;
        object->handle = source;
    }else
        acpi_panic("unexpected type %d of named object in acpi_load_ns()\n", source->type);
}

void acpi_store_ns(acpi_nsnode_t *target, acpi_object_t *object)
{
    if(target->type == ACPI_NAMESPACE_NAME)
        acpi_copy_object(&target->object, object);
    else if(target->type == ACPI_NAMESPACE_FIELD || target->type == ACPI_NAMESPACE_INDEXFIELD)
    {
        acpi_write_opregion(target, object);
    }else if(target->type == ACPI_NAMESPACE_BUFFER_FIELD)
    {
        acpi_write_buffer(target, object);
    }else
        acpi_panic("unexpected type %d of named object in acpi_store_ns()\n", target->type);
}

void acpi_alias_operand(acpi_state_t *state, acpi_object_t *object, acpi_object_t *ref) {
    acpi_nsnode_t *node;
    switch(object->type)
    {
    case ACPI_ARG_NAME:
        acpi_alias_object(ref, &state->arg[object->index]);
        break;
    case ACPI_LOCAL_NAME:
        acpi_alias_object(ref, &state->local[object->index]);
        break;
    case ACPI_UNRESOLVED_NAME:
        node = acpi_exec_resolve(object->name);
        if(!node)
            acpi_panic("node %s not found.\n", object->name);

        if(node->type == ACPI_NAMESPACE_NAME)
            acpi_alias_object(ref, &node->object);
        else
            acpi_panic("node %s type %d is not valid for acpi_alias_operand()\n", node->path, node->type);

        break;
    default:
        acpi_panic("object type %d is not valid for acpi_alias_operand()\n", object->type);
    }
}

// acpi_load_operand(): Load an object from a reference.

void acpi_load_operand(acpi_state_t *state, acpi_object_t *source, acpi_object_t *object)
{
    switch(source->type)
    {
    case ACPI_INTEGER:
    case ACPI_BUFFER:
    case ACPI_STRING:
    case ACPI_PACKAGE:
    {
        // Anonymous objects are just returned as-is.
        acpi_copy_object(object, source);
        break;
    }
    case ACPI_STRING_INDEX:
    case ACPI_BUFFER_INDEX:
    case ACPI_PACKAGE_INDEX:
    {
        // Indices are resolved for stores, but returned as-is for loads.
        acpi_copy_object(object, source);
        break;
    }
    case ACPI_UNRESOLVED_NAME:
    {
        acpi_nsnode_t *handle = acpi_exec_resolve(source->name);
        if(!handle)
            acpi_panic("undefined reference %s\n", source->name);
        acpi_load_ns(handle, object);
        break;
    }
    case ACPI_ARG_NAME:
        acpi_copy_object(object, &state->arg[source->index]);
        break;
    case ACPI_LOCAL_NAME:
        acpi_copy_object(object, &state->local[source->index]);
        break;
    default:
        acpi_panic("object type %d is not valid for acpi_load_operand()\n", source->type);
    }
}

// acpi_store_operand(): Stores a copy of the object to a reference.

void acpi_store_operand(acpi_state_t *state, acpi_object_t *target, acpi_object_t *object)
{
    switch(target->type)
    {
    case ACPI_NULL_NAME:
        // Stores to the null target are ignored.
        break;
    case ACPI_STRING_INDEX:
        target->string[target->integer] = object->integer;
        break;
    case ACPI_BUFFER_INDEX:
    {
        uint8_t *window = target->buffer;
        window[target->integer] = object->integer;
        break;
    }
    case ACPI_PACKAGE_INDEX:
        acpi_copy_object(&target->package[target->integer], object);
        break;
    case ACPI_UNRESOLVED_NAME:
    {
        acpi_nsnode_t *handle = acpi_exec_resolve(target->name);
        if(!handle)
            acpi_panic("undefined reference %s\n", target->name);
        acpi_store_ns(handle, object);
        break;
    }
    case ACPI_ARG_NAME:
        acpi_copy_object(&state->arg[target->index], object);
        break;
    case ACPI_LOCAL_NAME:
        acpi_copy_object(&state->local[target->index], object);
        break;
/*
    // TODO: Re-implement the Debug object.
        if(source->type == ACPI_INTEGER)
            acpi_debug("Debug(): integer(%ld)\n", source->integer);
        else if(source->type == ACPI_STRING)
            acpi_debug("Debug(): string(\"%s\")\n", source->string);
        else if(source->type == ACPI_BUFFER)
            // TODO: Print in hex and respect size.
            acpi_debug("Debug(): buffer(\"%s\")\n", source->buffer);
        else
            acpi_debug("Debug(): type %d\n", source->type);
*/
    default:
        acpi_panic("object type %d is not valid for acpi_store_operand()\n", target->type);
    }
}

// acpi_write_buffer(): Writes to a Buffer Field
// Param:    acpi_nsnode_t *handle - handle of buffer field
// Param:    acpi_object_t *source - object to write
// Return:    Nothing

void acpi_write_buffer(acpi_nsnode_t *handle, acpi_object_t *source)
{
    acpi_nsnode_t *buffer_handle;
    buffer_handle = acpins_resolve(handle->buffer);

    if(!buffer_handle)
        acpi_debug("undefined reference %s\n", handle->buffer);

    uint64_t value = source->integer;

    uint64_t offset = handle->buffer_offset / 8;
    uint64_t bitshift = handle->buffer_offset % 8;

    value <<= bitshift;

    uint64_t mask = 1;
    mask <<= bitshift;
    mask--;
    mask <<= bitshift;

    uint8_t *byte = (uint8_t*)(buffer_handle->object.buffer + offset);
    uint16_t *word = (uint16_t*)(buffer_handle->object.buffer + offset);
    uint32_t *dword = (uint32_t*)(buffer_handle->object.buffer + offset);
    uint64_t *qword = (uint64_t*)(buffer_handle->object.buffer + offset);

    if(handle->buffer_size <= 8)
    {
        byte[0] &= (uint8_t)mask;
        byte[0] |= (uint8_t)value;
    } else if(handle->buffer_size <= 16)
    {
        word[0] &= (uint16_t)mask;
        word[0] |= (uint16_t)value;
    } else if(handle->buffer_size <= 32)
    {
        dword[0] &= (uint32_t)mask;
        dword[0] |= (uint32_t)value;
    } else if(handle->buffer_size <= 64)
    {
        qword[0] &= mask;
        qword[0] |= value;
    }
}

