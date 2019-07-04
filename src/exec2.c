
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
 */

#include <lai/core.h>
#include "libc.h"
#include "opregion.h"
#include "exec_impl.h"
#include "ns_impl.h"

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

int lai_create_string(lai_object_t *object, size_t length) {
    object->type = LAI_STRING;
    object->string_ptr = laihost_malloc(sizeof(struct lai_string_head) + length + 1);
    if (!object->string_ptr)
        return 1;
    object->string_ptr->rc = 1;
    memset(object->string_ptr->content, 0, length + 1);
    return 0;
}

int lai_create_c_string(lai_object_t *object, const char *s) {
    size_t n = lai_strlen(s);
    int e;
    e = lai_create_string(object, n);
    if(e)
        return e;
    memcpy(lai_exec_string_access(object), s, n);
    return 0;
}

int lai_create_buffer(lai_object_t *object, size_t size) {
    object->type = LAI_BUFFER;
    object->buffer_ptr = laihost_malloc(sizeof(struct lai_buffer_head) + size);
    if (!object->buffer_ptr)
        return 1;
    object->buffer_ptr->rc = 1;
    object->buffer_ptr->size = size;
    memset(object->buffer_ptr->content, 0, size);
    return 0;
}

int lai_create_pkg(lai_object_t *object, size_t n) {
    object->type = LAI_PACKAGE;
    object->pkg_ptr = laihost_malloc(sizeof(struct lai_pkg_head)
            + n * sizeof(lai_object_t));
    if (!object->pkg_ptr)
        return 1;
    object->pkg_ptr->rc = 1;
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
    switch (object->type) {
        case LAI_STRING:
        case LAI_STRING_INDEX:
            if (lai_rc_unref(&object->string_ptr->rc))
                laihost_free(object->string_ptr);
            break;
        case LAI_BUFFER:
        case LAI_BUFFER_INDEX:
            if (lai_rc_unref(&object->buffer_ptr->rc))
                laihost_free(object->buffer_ptr);
            break;
        case LAI_PACKAGE:
        case LAI_PACKAGE_INDEX:
            if (lai_rc_unref(&object->pkg_ptr->rc))
                laihost_free_package(object);
            break;
    }

    memset(object, 0, sizeof(lai_object_t));
}

// Helper function for lai_move_object() and lai_clone_object().
void lai_swap_object(lai_object_t *first, lai_object_t *second) {
    lai_object_t temp = *first;
    *first = *second;
    *second = temp;
}

// lai_move_object(): Moves an object: instead of making a deep copy,
//                     the pointers are exchanged and the source object is reset to zero.
// Param & Return: See lai_clone_object().

void lai_move_object(lai_object_t *destination, lai_object_t *source) {
    // Move-by-swap idiom. This handles move-to-self operations correctly.
    lai_object_t temp = {0};
    lai_swap_object(&temp, source);
    lai_swap_object(&temp, destination);
    lai_free_object(&temp);
}

static void lai_assign_object(lai_object_t *dest, lai_object_t *src) {
    // Make a local shallow copy of the AML object.
    lai_object_t temp = *src;
    switch (src->type) {
        case LAI_STRING:
        case LAI_STRING_INDEX:
            lai_rc_ref(&src->string_ptr->rc);
            break;
        case LAI_BUFFER:
        case LAI_BUFFER_INDEX:
            lai_rc_ref(&src->buffer_ptr->rc);
            break;
        case LAI_PACKAGE:
        case LAI_PACKAGE_INDEX:
            lai_rc_ref(&src->pkg_ptr->rc);
            break;
    }

    lai_move_object(dest, &temp);
}

size_t lai_exec_string_length(lai_object_t *str) {
    LAI_ENSURE(str->type == LAI_STRING);
    return lai_strlen(str->string_ptr->content);
}

// Note: This function exists to enable better GC and proper locking in the future.
void lai_exec_pkg_var_load(lai_object_t *out, struct lai_pkg_head *head, size_t i) {
    lai_assign_object(out, &head->elems[i]);
}

// Note: This function exists to enable better GC and proper locking in the future.
void lai_exec_pkg_var_store(lai_object_t *in, struct lai_pkg_head *head, size_t i) {
    lai_assign_object(&head->elems[i], in);
}

// lai_clone_buffer(): Clones a buffer object
// Param:    lai_object_t *dest - destination
// Param:    lai_object_t *source - source
// Return:   Nothing

static void lai_clone_buffer(lai_object_t *dest, lai_object_t *source) {
    size_t size = lai_exec_buffer_size(source);
    if (lai_create_buffer(dest, size))
        lai_panic("unable to allocate memory for buffer object.");
    memcpy(lai_exec_buffer_access(dest), lai_exec_buffer_access(source), size);
}

// lai_clone_string(): Clones a string object
// Param:    lai_object_t *dest - destination
// Param:    lai_object_t *source - source
// Return:   Nothing

static void lai_clone_string(lai_object_t *dest, lai_object_t *source) {
    size_t n = lai_exec_string_length(source);
    if (lai_create_string(dest, n))
        lai_panic("unable to allocate memory for string object.");
    memcpy(lai_exec_string_access(dest), lai_exec_string_access(source), n);
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
        lai_clone_object(&dest->pkg_ptr->elems[i], &src->pkg_ptr->elems[i]);
}

// lai_clone_object(): Copies an object
// Param:    lai_object_t *dest - destination
// Param:    lai_object_t *source - source
// Return:   Nothing

void lai_clone_object(lai_object_t *dest, lai_object_t *source) {
    // Clone into a temporary object.
    lai_object_t temp = {0};
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
    }

    if (temp.type) {
        // Afterwards, swap to the destination. This handles copy-to-self correctly.
        lai_swap_object(dest, &temp);
        lai_free_object(&temp);
    }else{
        // For others objects: just do a shallow copy.
        lai_assign_object(dest, source);
    }
}

static void lai_load_ns(lai_nsnode_t *src, lai_object_t *object) {
    switch (src->type) {
        case LAI_NAMESPACE_NAME:
            lai_assign_object(object, &src->object);
            break;
        case LAI_NAMESPACE_FIELD:
        case LAI_NAMESPACE_INDEXFIELD:
            lai_read_opregion(object, src);
            break;
        case LAI_NAMESPACE_DEVICE:
            object->type = LAI_HANDLE;
            object->handle = src;
            break;
        default:
            lai_panic("unexpected type %d of named object in lai_load_ns()", src->type);
    }
}

static void lai_store_ns(lai_nsnode_t *target, lai_object_t *object) {
    switch (target->type) {
        case LAI_NAMESPACE_NAME:
            lai_assign_object(&target->object, object);
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

// Loads from a name.
// Returns a view of an existing object and not a clone of the object.
void lai_load(lai_state_t *state, lai_object_t *src, lai_object_t *object) {
    switch (src->type) {
        case LAI_ARG_NAME:
            lai_assign_object(object, &state->arg[src->index]);
            break;
        case LAI_LOCAL_NAME:
            lai_assign_object(object, &state->local[src->index]);
            break;
        case LAI_UNRESOLVED_NAME:
        {
            struct lai_amlname amln;
            lai_amlname_parse(&amln, src->unres_aml);

            lai_nsnode_t *node = lai_do_resolve(src->unres_ctx_handle, &amln);
            if (!node)
                lai_panic("undefined reference %s", lai_stringify_amlname(&amln));
            lai_load_ns(node, object);
            break;
        }
        case LAI_RESOLVED_NAME:
            lai_load_ns(src->handle, object);
            break;
        default:
            lai_panic("object type %d is not valid for lai_load()", src->type);
    }
}

// lai_store(): Stores a copy of the object to a reference.
void lai_store(lai_state_t *state, lai_object_t *dest, lai_object_t *object) {
    switch(dest->type) {
        case LAI_NULL_NAME:
            // Stores to the null target are ignored.
            break;
        case LAI_STRING_INDEX:
        {
            char *window = dest->string_ptr->content;
            window[dest->integer] = object->integer;
            break;
        }
        case LAI_BUFFER_INDEX:
        {
            uint8_t *window = dest->buffer_ptr->content;
            window[dest->integer] = object->integer;
            break;
        }
        case LAI_PACKAGE_INDEX:
        {
            lai_object_t copy = {0};
            lai_assign_object(&copy, object);
            lai_exec_pkg_var_store(&copy, dest->pkg_ptr, dest->integer);
            lai_free_object(&copy);
            break;
        }
        case LAI_UNRESOLVED_NAME:
        {
            struct lai_amlname amln;
            lai_amlname_parse(&amln, dest->unres_aml);

            lai_nsnode_t *handle = lai_do_resolve(dest->unres_ctx_handle, &amln);
            if(!handle)
                lai_panic("undefined reference %s", lai_stringify_amlname(&amln));
            lai_store_ns(handle, object);
            break;
        }
        case LAI_RESOLVED_NAME:
            lai_store_ns(dest->handle, object);
            break;
        case LAI_ARG_NAME:
            lai_assign_object(&state->arg[dest->index], object);
            break;
        case LAI_LOCAL_NAME:
            lai_assign_object(&state->local[dest->index], object);
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
                        lai_debug("Debug(): string(\"%s\")", lai_exec_string_access(object));
                        break;
                    case LAI_BUFFER:
                        lai_debug("Debug(): buffer(%X)", (size_t)lai_exec_buffer_access(object));
                        break;
                    default:
                        lai_debug("Debug(): type %d", object->type);
                }
            }
            break;
        default:
            lai_panic("object type %d is not valid for lai_store()", dest->type);
    }
}

// Load an object (i.e., integer, string, buffer, package) or reference.
// This is the access method used by Store().
// Returns immediate objects and indices as-is (i.e., without load from a name).
// Returns a view of an existing object and not a clone of the object.
void lai_get_objectref(lai_state_t *state, lai_object_t *src, lai_object_t *object) {
    switch (src->type) {
        case LAI_INTEGER:
        case LAI_STRING:
        case LAI_BUFFER:
        case LAI_PACKAGE:
        case LAI_STRING_INDEX:
        case LAI_BUFFER_INDEX:
        case LAI_PACKAGE_INDEX:
            lai_assign_object(object, src);
            return;
    }

    lai_object_t temp = {0};
    lai_load(state, src, &temp);
    lai_move_object(object, &temp);
}

// Load an integer.
// Returns immediate objects as-is.
void lai_get_integer(lai_state_t *state, lai_object_t *src, lai_object_t *object) {
    switch (src->type) {
        case LAI_INTEGER:
            lai_assign_object(object, src);
            return;
    }

    lai_object_t temp = {0};
    lai_load(state, src, &temp);
    if(temp.type != LAI_INTEGER)
        lai_panic("lai_load_integer() expects an integer, not a value of type %d", temp.type);
    lai_move_object(object, &temp);
}

// lai_write_buffer(): Writes to a Buffer Field
// Param:    lai_nsnode_t *handle - handle of buffer field
// Param:    lai_object_t *source - object to write
// Return:    Nothing

void lai_write_buffer(lai_nsnode_t *handle, lai_object_t *source) {
    lai_nsnode_t *buffer_handle = handle->bf_node;

    uint64_t value = source->integer;

    uint64_t offset = handle->bf_offset / 8;
    uint64_t bitshift = handle->bf_offset % 8;

    value <<= bitshift;

    uint64_t mask = 1;
    mask <<= bitshift;
    mask--;
    mask <<= bitshift;

    // TODO: This function does unaligned access. That needs to be fixed!
    uint8_t *byte = (uint8_t*)(lai_exec_buffer_access(&buffer_handle->object) + offset);
    uint16_t *word = (uint16_t*)(lai_exec_buffer_access(&buffer_handle->object) + offset);
    uint32_t *dword = (uint32_t*)(lai_exec_buffer_access(&buffer_handle->object) + offset);
    uint64_t *qword = (uint64_t*)(lai_exec_buffer_access(&buffer_handle->object) + offset);

    if (handle->bf_size <= 8) {
        byte[0] &= (uint8_t)mask;
        byte[0] |= (uint8_t)value;
    } else if (handle->bf_size <= 16) {
        word[0] &= (uint16_t)mask;
        word[0] |= (uint16_t)value;
    } else if (handle->bf_size <= 32) {
        dword[0] &= (uint32_t)mask;
        dword[0] |= (uint32_t)value;
    } else if (handle->bf_size <= 64) {
        qword[0] &= mask;
        qword[0] |= value;
    }
}
