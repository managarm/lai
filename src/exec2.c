
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
 */

#include <lai/core.h>
#include "libc.h"
#include "opregion.h"
#include "exec_impl.h"

void lai_write_buffer(lai_nsnode_t *, lai_object_t *);

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

lai_nsnode_t *lai_exec_resolve(char *path) {
    lai_nsnode_t *object;
    object = lai_resolve(path);

    if (lai_strlen(path) == 4)
        return lai_resolve(path);

    while (!object && lai_strlen(path) > 6) {
        memmove(path + lai_strlen(path) - 9, path + lai_strlen(path) - 4, 5);
        object = lai_resolve(path);
        if (object != NULL)
            goto resolve_alias;
    }

    if (object == NULL)
        return NULL;

resolve_alias:
    // resolve Aliases too
    while (object->type == LAI_NAMESPACE_ALIAS)
    {
        object = lai_resolve(object->alias);
        if (!object)
            return NULL;
    }

    return object;
}

int lai_create_pkg(lai_object_t *object, size_t n) {
    object->type = LAI_PACKAGE;
    object->pkg_ptr = laihost_malloc(sizeof(struct lai_pkg_head)
            + n * sizeof(lai_object_t));
    if (!object->pkg_ptr)
        return 1;
    object->pkg_ptr->size = n;
    memset(object->pkg_ptr->elems, 0, n * sizeof(lai_object_t));
    return 0;
}

// laihost_free_package(): Frees a package object and all its children
// Param:   lai_object_t *object
// Return:  Nothing

static void laihost_free_package(lai_object_t *object) {
    for(int i = 0; i < object->pkg_ptr->size; i++)
        lai_free_object(&object->pkg_ptr->elems[i]);
    laihost_free(object->pkg_ptr);
}

void lai_free_object(lai_object_t *object) {
    if (object->type == LAI_BUFFER)
        laihost_free(object->buffer);
    else if (object->type == LAI_PACKAGE)
        laihost_free_package(object);

    memset(object, 0, sizeof(lai_object_t));
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

void lai_move_object(lai_object_t *destination, lai_object_t *source) {
    // Move-by-swap idiom. This handles move-to-self operations correctly.
    lai_object_t temp = {0};
    lai_swap_object(&temp, source);
    lai_swap_object(&temp, destination);
    lai_free_object(&temp);
}

static void lai_assign_object(lai_object_t *dest, lai_object_t *src) {
    // TODO: This has to handle reference counting in the future.
    lai_object_t temp = *src;
    lai_move_object(dest, &temp);
}

void lai_exec_pkg_load(lai_object_t *out, lai_object_t *pkg, size_t i) {
    lai_assign_object(out, &pkg->pkg_ptr->elems[i]);
}

void lai_exec_pkg_store(lai_object_t *in, lai_object_t *pkg, size_t i) {
    lai_assign_object(&pkg->pkg_ptr->elems[i], in);
}

// lai_clone_buffer(): Clones a buffer object
// Param:    lai_object_t *destination - destination
// Param:    lai_object_t *source - source
// Return:   Nothing

static void lai_clone_buffer(lai_object_t *destination, lai_object_t *source) {
    destination->type = LAI_BUFFER;
    destination->buffer_size = source->buffer_size;
    destination->buffer = laihost_malloc(source->buffer_size);
    if (!destination->buffer)
        lai_panic("unable to allocate memory for buffer object.");

    memcpy(destination->buffer, source->buffer, source->buffer_size);
}

// lai_clone_string(): Clones a string object
// Param:    lai_object_t *destination - destination
// Param:    lai_object_t *source - source
// Return:   Nothing

static void lai_clone_string(lai_object_t *destination, lai_object_t *source) {
    destination->type = LAI_STRING;
    destination->string = laihost_malloc(lai_strlen(source->string) + 1);
    if (!destination->string)
        lai_panic("unable to allocate memory for string object.");

    lai_strcpy(destination->string, source->string);
}

// lai_clone_package(): Clones a package object
// Param:    lai_object_t *dest - destination
// Param:    lai_object_t *source - source
// Return:   Nothing

static void lai_clone_package(lai_object_t *dest, lai_object_t *src) {
    size_t n = src->pkg_ptr->size;
    if (lai_create_pkg(dest, n))
        lai_panic("unable to allocate memory for package object.");
    for (int i = 0; i < n; i++)
        lai_copy_object(&dest->pkg_ptr->elems[i], &src->pkg_ptr->elems[i]);
}

// lai_copy_object(): Copies an object
// Param:    lai_object_t *destination - destination
// Param:    lai_object_t *source - source
// Return:   Nothing

void lai_copy_object(lai_object_t *destination, lai_object_t *source) {
    // First, clone into a temporary object.
    lai_object_t temp;
    switch (source->type) {
        case LAI_STRING:
            lai_clone_string(&temp, source);
            break;
        case LAI_BUFFER:
            lai_clone_buffer(&temp, source);
            break;
        case LAI_PACKAGE:
            lai_clone_package(&temp, source);
            break;
        default:
            temp = *source;
            break;
    }

    // Afterwards, swap to the destination. This handles copy-to-self correctly.
    lai_swap_object(destination, &temp);
    lai_free_object(&temp);
}

// lai_alias_object(): Creates a reference to the storage of an object.
void lai_alias_object(lai_object_t *alias, lai_object_t *object) {
    switch (object->type) {
        case LAI_STRING:
            alias->type = LAI_STRING_REFERENCE;
            alias->string = object->string;
            break;
        case LAI_BUFFER:
            alias->type = LAI_BUFFER_REFERENCE;
            alias->buffer_size = object->buffer_size;
            alias->buffer = object->buffer;
            break;
        case LAI_PACKAGE:
            alias->type = LAI_PACKAGE_REFERENCE;
            alias->pkg_ptr = object->pkg_ptr;
            break;
        default:
            lai_panic("object type %d is not valid for lai_alias_object()", object->type);
    }
}

void lai_load_ns(lai_nsnode_t *source, lai_object_t *object) {
    switch (source->type) {
        case LAI_NAMESPACE_NAME:
            lai_copy_object(object, &source->object);
            break;
        case LAI_NAMESPACE_FIELD:
        case LAI_NAMESPACE_INDEXFIELD:
            lai_read_opregion(object, source);
            break;
        case LAI_NAMESPACE_DEVICE:
            object->type = LAI_HANDLE;
            object->handle = source;
            break;
        default:
            lai_panic("unexpected type %d of named object in lai_load_ns()");
    }
}

void lai_store_ns(lai_nsnode_t *target, lai_object_t *object) {
    switch (target->type) {
        case LAI_NAMESPACE_NAME:
            lai_copy_object(&target->object, object);
            break;
        case LAI_NAMESPACE_FIELD:
        case LAI_NAMESPACE_INDEXFIELD:
            lai_write_opregion(target, object);
            break;
        case LAI_NAMESPACE_BUFFER_FIELD:
            lai_write_buffer(target, object);
            break;
        default:
            lai_panic("unexpected type %d of named object in lai_store_ns()");
    }
}

void lai_alias_operand(lai_state_t *state, lai_object_t *object, lai_object_t *ref) {
    lai_nsnode_t *node;
    switch (object->type) {
        case LAI_STRING:
            ref->type = LAI_STRING_REFERENCE;
            ref->string = object->string;
            break;
        case LAI_BUFFER:
            ref->type = LAI_BUFFER_REFERENCE;
            ref->buffer_size = object->buffer_size;
            ref->buffer = object->buffer;
            break;
        case LAI_PACKAGE:
            ref->type = LAI_PACKAGE_REFERENCE;
            ref->pkg_ptr = object->pkg_ptr;
            break;
        case LAI_ARG_NAME:
            lai_alias_object(ref, &state->arg[object->index]);
            break;
        case LAI_LOCAL_NAME:
            lai_alias_object(ref, &state->local[object->index]);
            break;
        case LAI_UNRESOLVED_NAME:
            node = lai_exec_resolve(object->name);
            if (!node)
                lai_panic("node %s not found.", object->name);

            if (node->type == LAI_NAMESPACE_NAME)
                lai_alias_object(ref, &node->object);
            else
                lai_panic("node %s type %d is not valid for lai_alias_operand()", node->path, node->type);

            break;
        default:
            lai_panic("object type %d is not valid for lai_alias_operand()", object->type);
    }
}

// lai_load_operand(): Load an object from a reference.

void lai_load_operand(lai_state_t *state, lai_object_t *source, lai_object_t *object) {
    switch (source->type) {
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
            if (!handle)
                lai_panic("undefined reference %s", source->name);
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
            lai_panic("object type %d is not valid for lai_load_operand()", source->type);
    }
}

// lai_store_operand(): Stores a copy of the object to a reference.

void lai_store_operand(lai_state_t *state, lai_object_t *target, lai_object_t *object) {
    switch(target->type) {
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
        {
            lai_object_t copy = {0};
            lai_copy_object(&copy, object);
            lai_exec_pkg_store(&copy, target, target->integer);
            lai_free_object(&copy);
            break;
        }
        case LAI_UNRESOLVED_NAME:
        {
            lai_nsnode_t *handle = lai_exec_resolve(target->name);
            if(!handle)
                lai_panic("undefined reference %s", target->name);
            lai_store_ns(handle, object);
            break;
        }
        case LAI_ARG_NAME:
            lai_copy_object(&state->arg[target->index], object);
            break;
        case LAI_LOCAL_NAME:
            lai_copy_object(&state->local[target->index], object);
            break;
        case LAI_DEBUG_NAME:
            if(laihost_handle_amldebug)
                laihost_handle_amldebug(object);
            else {
                switch (object->type) {
                    case LAI_INTEGER:
                        lai_debug("Debug(): integer(%ld)", object->integer);
                        break;
                    case LAI_STRING:
                        lai_debug("Debug(): string(\"%s\")", object->string);
                        break;
                    case LAI_BUFFER:
                        lai_debug("Debug(): buffer(%X)", (size_t)object->buffer);
                        break;
                    default:
                        lai_debug("Debug(): type %d", object->type);
                }
            }
            break;
        default:
            lai_panic("object type %d is not valid for lai_store_operand()", target->type);
        }
}

// lai_write_buffer(): Writes to a Buffer Field
// Param:    lai_nsnode_t *handle - handle of buffer field
// Param:    lai_object_t *source - object to write
// Return:    Nothing

void lai_write_buffer(lai_nsnode_t *handle, lai_object_t *source) {
    lai_nsnode_t *buffer_handle;
    buffer_handle = lai_resolve(handle->buffer);
    if (!buffer_handle)
        lai_panic("undefined reference %s", handle->buffer);

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

    if (handle->buffer_size <= 8) {
        byte[0] &= (uint8_t)mask;
        byte[0] |= (uint8_t)value;
    } else if (handle->buffer_size <= 16) {
        word[0] &= (uint16_t)mask;
        word[0] |= (uint16_t)value;
    } else if (handle->buffer_size <= 32) {
        dword[0] &= (uint32_t)mask;
        dword[0] |= (uint32_t)value;
    } else if (handle->buffer_size <= 64) {
        qword[0] &= mask;
        qword[0] |= value;
    }
}
