
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

// lai_exec_resolve(): Resolves a name during control method execution
// Param:    char *path - 4-char object name or full path
// Return:    lai_nsnode_t * - pointer to namespace object, NULL on error

lai_nsnode_t *lai_exec_resolve(char *path)
{
    lai_nsnode_t *object;
    object = acpins_resolve(path);

    if(lai_strlen(path) == 4)
        return acpins_resolve(path);

    while(!object && lai_strlen(path) > 6)
    {
        lai_memmove(path + lai_strlen(path) - 9, path + lai_strlen(path) - 4, 5);
        object = acpins_resolve(path);
        if(object != NULL)
            goto resolve_alias;
    }

    if(object == NULL)
        return NULL;

resolve_alias:
    // resolve Aliases too
    while(object->type == LAI_NAMESPACE_ALIAS)
    {
        object = acpins_resolve(object->alias);
        if(!object)
            return NULL;
    }

    return object;
}

// lai_free_package(): Frees a package object and all its children
// Param:   lai_object_t *object
// Return:  Nothing

static void lai_free_package(lai_object_t *object)
{
    for(int i = 0; i < object->package_size; i++)
        lai_free_object(&object->package[i]);
    lai_free(object->package);
}

void lai_free_object(lai_object_t *object)
{
    if(object->type == LAI_BUFFER)
        lai_free(object->buffer);
    else if(object->type == LAI_PACKAGE)
        lai_free_package(object);

    lai_memset(object, 0, sizeof(lai_object_t));
}

// Helper function for lai_move_object() and lai_copy_object().
void lai_swap_object(lai_object_t *first, lai_object_t *second) {
    lai_object_t temp = *first;
    *first = *second;
    *second = temp;
}

// lai_move_object(): Moves an object: instead of making a deep copy,
//                     the pointers are exchanged and the source object is reset to zero.
// Param & Return: See lai_copy_object().

void lai_move_object(lai_object_t *destination, lai_object_t *source)
{
    // Move-by-swap idiom. This handles move-to-self operations correctly.
    lai_object_t temp = {0};
    lai_swap_object(&temp, source);
    lai_swap_object(&temp, destination);
    lai_free_object(&temp);
}

// lai_clone_buffer(): Clones a buffer object
// Param:    lai_object_t *destination - destination
// Param:    lai_object_t *source - source
// Return:   Nothing

static void lai_clone_buffer(lai_object_t *destination, lai_object_t *source)
{
    destination->type = LAI_BUFFER;
    destination->buffer_size = source->buffer_size;
    destination->buffer = lai_malloc(source->buffer_size);
    if(!destination->buffer)
        lai_panic("unable to allocate memory for buffer object.\n");

    lai_memcpy(destination->buffer, source->buffer, source->buffer_size);
}

// lai_clone_string(): Clones a string object
// Param:    lai_object_t *destination - destination
// Param:    lai_object_t *source - source
// Return:   Nothing

static void lai_clone_string(lai_object_t *destination, lai_object_t *source)
{
    destination->type = LAI_STRING;
    destination->string = lai_malloc(lai_strlen(source->string) + 1);
    if(!destination->string)
        lai_panic("unable to allocate memory for string object.\n");

    lai_strcpy(destination->string, source->string);
}

// lai_clone_package(): Clones a package object
// Param:    lai_object_t *destination - destination
// Param:    lai_object_t *source - source
// Return:   Nothing

static void lai_clone_package(lai_object_t *destination, lai_object_t *source)
{
    destination->type = LAI_PACKAGE;
    destination->package_size = source->package_size;
    destination->package = lai_calloc(source->package_size, sizeof(lai_object_t));
    if(!destination->package)
        lai_panic("unable to allocate memory for package object.\n");

    for(int i = 0; i < source->package_size; i++)
        lai_copy_object(&destination->package[i], &source->package[i]);
}

// lai_copy_object(): Copies an object
// Param:    lai_object_t *destination - destination
// Param:    lai_object_t *source - source
// Return:   Nothing

void lai_copy_object(lai_object_t *destination, lai_object_t *source)
{
    // First, clone into a temporary object.
    lai_object_t temp;
    if(source->type == LAI_STRING)
        lai_clone_string(&temp, source);
    else if(source->type == LAI_BUFFER)
        lai_clone_buffer(&temp, source);
    else if(source->type == LAI_PACKAGE)
        lai_clone_package(&temp, source);
    else
        temp = *source;

    // Afterwards, swap to the destination. This handles copy-to-self correctly.
    lai_swap_object(destination, &temp);
    lai_free_object(&temp);
}

// lai_alias_copy(): Creates a reference to the storage of an object.

void lai_alias_object(lai_object_t *alias, lai_object_t *object)
{
    if(object->type == LAI_STRING)
    {
        alias->type = LAI_STRING_REFERENCE;
        alias->string = object->string;
    }else if(object->type == LAI_BUFFER)
    {
        alias->type = LAI_BUFFER_REFERENCE;
        alias->buffer_size = object->buffer_size;
        alias->buffer = object->buffer;
    }else if(object->type == LAI_PACKAGE)
    {
        alias->type = LAI_PACKAGE_REFERENCE;
        alias->package_size = object->package_size;
        alias->package = object->package;
    }else
        lai_panic("object type %d is not valid for lai_alias_object()\n", object->type);
}

void lai_load_ns(lai_nsnode_t *source, lai_object_t *object)
{
    if(source->type == LAI_NAMESPACE_NAME)
        lai_copy_object(object, &source->object);
    else if(source->type == LAI_NAMESPACE_FIELD || source->type == LAI_NAMESPACE_INDEXFIELD)
        // It's an Operation Region field; perform IO in that region.
        lai_read_opregion(object, source);
    else if(source->type == LAI_NAMESPACE_DEVICE)
    {
        object->type = LAI_HANDLE;
        object->handle = source;
    }else
        lai_panic("unexpected type %d of named object in lai_load_ns()\n", source->type);
}

void lai_store_ns(lai_nsnode_t *target, lai_object_t *object)
{
    if(target->type == LAI_NAMESPACE_NAME)
        lai_copy_object(&target->object, object);
    else if(target->type == LAI_NAMESPACE_FIELD || target->type == LAI_NAMESPACE_INDEXFIELD)
    {
        lai_write_opregion(target, object);
    }else if(target->type == LAI_NAMESPACE_BUFFER_FIELD)
    {
        lai_write_buffer(target, object);
    }else
        lai_panic("unexpected type %d of named object in lai_store_ns()\n", target->type);
}

void lai_alias_operand(lai_state_t *state, lai_object_t *object, lai_object_t *ref) {
    lai_nsnode_t *node;
    switch(object->type)
    {
    case LAI_ARG_NAME:
        lai_alias_object(ref, &state->arg[object->index]);
        break;
    case LAI_LOCAL_NAME:
        lai_alias_object(ref, &state->local[object->index]);
        break;
    case LAI_UNRESOLVED_NAME:
        node = lai_exec_resolve(object->name);
        if(!node)
            lai_panic("node %s not found.\n", object->name);

        if(node->type == LAI_NAMESPACE_NAME)
            lai_alias_object(ref, &node->object);
        else
            lai_panic("node %s type %d is not valid for lai_alias_operand()\n", node->path, node->type);

        break;
    default:
        lai_panic("object type %d is not valid for lai_alias_operand()\n", object->type);
    }
}

// lai_load_operand(): Load an object from a reference.

void lai_load_operand(lai_state_t *state, lai_object_t *source, lai_object_t *object)
{
    switch(source->type)
    {
    case LAI_INTEGER:
    case LAI_BUFFER:
    case LAI_STRING:
    case LAI_PACKAGE:
    {
        // Anonymous objects are just returned as-is.
        lai_copy_object(object, source);
        break;
    }
    case LAI_STRING_INDEX:
    case LAI_BUFFER_INDEX:
    case LAI_PACKAGE_INDEX:
    {
        // Indices are resolved for stores, but returned as-is for loads.
        lai_copy_object(object, source);
        break;
    }
    case LAI_UNRESOLVED_NAME:
    {
        lai_nsnode_t *handle = lai_exec_resolve(source->name);
        if(!handle)
            lai_panic("undefined reference %s\n", source->name);
        lai_load_ns(handle, object);
        break;
    }
    case LAI_ARG_NAME:
        lai_copy_object(object, &state->arg[source->index]);
        break;
    case LAI_LOCAL_NAME:
        lai_copy_object(object, &state->local[source->index]);
        break;
    default:
        lai_panic("object type %d is not valid for lai_load_operand()\n", source->type);
    }
}

// lai_store_operand(): Stores a copy of the object to a reference.

void lai_store_operand(lai_state_t *state, lai_object_t *target, lai_object_t *object)
{
    switch(target->type)
    {
    case LAI_NULL_NAME:
        // Stores to the null target are ignored.
        break;
    case LAI_STRING_INDEX:
        target->string[target->integer] = object->integer;
        break;
    case LAI_BUFFER_INDEX:
    {
        uint8_t *window = target->buffer;
        window[target->integer] = object->integer;
        break;
    }
    case LAI_PACKAGE_INDEX:
        lai_copy_object(&target->package[target->integer], object);
        break;
    case LAI_UNRESOLVED_NAME:
    {
        lai_nsnode_t *handle = lai_exec_resolve(target->name);
        if(!handle)
            lai_panic("undefined reference %s\n", target->name);
        lai_store_ns(handle, object);
        break;
    }
    case LAI_ARG_NAME:
        lai_copy_object(&state->arg[target->index], object);
        break;
    case LAI_LOCAL_NAME:
        lai_copy_object(&state->local[target->index], object);
        break;
/*
    // TODO: Re-implement the Debug object.
        if(source->type == LAI_INTEGER)
            lai_debug("Debug(): integer(%ld)\n", source->integer);
        else if(source->type == LAI_STRING)
            lai_debug("Debug(): string(\"%s\")\n", source->string);
        else if(source->type == LAI_BUFFER)
            // TODO: Print in hex and respect size.
            lai_debug("Debug(): buffer(\"%s\")\n", source->buffer);
        else
            lai_debug("Debug(): type %d\n", source->type);
*/
    default:
        lai_panic("object type %d is not valid for lai_store_operand()\n", target->type);
    }
}

// lai_write_buffer(): Writes to a Buffer Field
// Param:    lai_nsnode_t *handle - handle of buffer field
// Param:    lai_object_t *source - object to write
// Return:    Nothing

void lai_write_buffer(lai_nsnode_t *handle, lai_object_t *source)
{
    lai_nsnode_t *buffer_handle;
    buffer_handle = acpins_resolve(handle->buffer);

    if(!buffer_handle)
        lai_debug("undefined reference %s\n", handle->buffer);

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

