
/*
 * Lightweight ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
 */

#include <lai/core.h>
#include "libc.h"
#include "opregion.h"
#include "exec_impl.h"
#include "ns_impl.h"

void lai_write_buffer(lai_nsnode_t *, lai_variable_t *);

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

int lai_create_string(lai_variable_t *object, size_t length) {
    object->type = LAI_STRING;
    object->string_ptr = laihost_malloc(sizeof(struct lai_string_head) + length + 1);
    if (!object->string_ptr)
        return 1;
    object->string_ptr->rc = 1;
    memset(object->string_ptr->content, 0, length + 1);
    return 0;
}

int lai_create_c_string(lai_variable_t *object, const char *s) {
    size_t n = lai_strlen(s);
    int e;
    e = lai_create_string(object, n);
    if(e)
        return e;
    memcpy(lai_exec_string_access(object), s, n);
    return 0;
}

int lai_create_buffer(lai_variable_t *object, size_t size) {
    object->type = LAI_BUFFER;
    object->buffer_ptr = laihost_malloc(sizeof(struct lai_buffer_head) + size);
    if (!object->buffer_ptr)
        return 1;
    object->buffer_ptr->rc = 1;
    object->buffer_ptr->size = size;
    memset(object->buffer_ptr->content, 0, size);
    return 0;
}

int lai_create_pkg(lai_variable_t *object, size_t n) {
    object->type = LAI_PACKAGE;
    object->pkg_ptr = laihost_malloc(sizeof(struct lai_pkg_head)
            + n * sizeof(lai_variable_t));
    if (!object->pkg_ptr)
        return 1;
    object->pkg_ptr->rc = 1;
    object->pkg_ptr->size = n;
    memset(object->pkg_ptr->elems, 0, n * sizeof(lai_variable_t));
    return 0;
}

// laihost_free_package(): Frees a package object and all its children
// Param:   lai_variable_t *object
// Return:  Nothing

static void laihost_free_package(lai_variable_t *object) {
    for(int i = 0; i < object->pkg_ptr->size; i++)
        lai_var_finalize(&object->pkg_ptr->elems[i]);
    laihost_free(object->pkg_ptr);
}

void lai_var_finalize(lai_variable_t *object) {
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

    memset(object, 0, sizeof(lai_variable_t));
}

// Helper function for lai_var_move() and lai_obj_clone().
void lai_swap_object(lai_variable_t *first, lai_variable_t *second) {
    lai_variable_t temp = *first;
    *first = *second;
    *second = temp;
}

// lai_var_move(): Moves an object: instead of making a deep copy,
//                     the pointers are exchanged and the source object is reset to zero.
// Param & Return: See lai_obj_clone().

void lai_var_move(lai_variable_t *destination, lai_variable_t *source) {
    // Move-by-swap idiom. This handles move-to-self operations correctly.
    lai_variable_t temp = {0};
    lai_swap_object(&temp, source);
    lai_swap_object(&temp, destination);
    lai_var_finalize(&temp);
}

void lai_var_assign(lai_variable_t *dest, lai_variable_t *src) {
    // Make a local shallow copy of the AML object.
    lai_variable_t temp = *src;
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

    lai_var_move(dest, &temp);
}

size_t lai_exec_string_length(lai_variable_t *str) {
    LAI_ENSURE(str->type == LAI_STRING);
    return lai_strlen(str->string_ptr->content);
}

// Note: This function exists to enable better GC and proper locking in the future.
void lai_exec_pkg_var_load(lai_variable_t *out, struct lai_pkg_head *head, size_t i) {
    lai_var_assign(out, &head->elems[i]);
}

// Note: This function exists to enable better GC and proper locking in the future.
void lai_exec_pkg_var_store(lai_variable_t *in, struct lai_pkg_head *head, size_t i) {
    lai_var_assign(&head->elems[i], in);
}

// lai_clone_buffer(): Clones a buffer object
// Param:    lai_variable_t *dest - destination
// Param:    lai_variable_t *source - source
// Return:   Nothing

static void lai_clone_buffer(lai_variable_t *dest, lai_variable_t *source) {
    size_t size = lai_exec_buffer_size(source);
    if (lai_create_buffer(dest, size))
        lai_panic("unable to allocate memory for buffer object.");
    memcpy(lai_exec_buffer_access(dest), lai_exec_buffer_access(source), size);
}

// lai_clone_string(): Clones a string object
// Param:    lai_variable_t *dest - destination
// Param:    lai_variable_t *source - source
// Return:   Nothing

static void lai_clone_string(lai_variable_t *dest, lai_variable_t *source) {
    size_t n = lai_exec_string_length(source);
    if (lai_create_string(dest, n))
        lai_panic("unable to allocate memory for string object.");
    memcpy(lai_exec_string_access(dest), lai_exec_string_access(source), n);
}

// lai_clone_package(): Clones a package object
// Param:    lai_variable_t *dest - destination
// Param:    lai_variable_t *source - source
// Return:   Nothing

static void lai_clone_package(lai_variable_t *dest, lai_variable_t *src) {
    size_t n = src->pkg_ptr->size;
    if (lai_create_pkg(dest, n))
        lai_panic("unable to allocate memory for package object.");
    for (int i = 0; i < n; i++)
        lai_obj_clone(&dest->pkg_ptr->elems[i], &src->pkg_ptr->elems[i]);
}

// lai_obj_clone(): Copies an object
// Param:    lai_variable_t *dest - destination
// Param:    lai_variable_t *source - source
// Return:   Nothing

void lai_obj_clone(lai_variable_t *dest, lai_variable_t *source) {
    // Clone into a temporary object.
    lai_variable_t temp = {0};
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
        lai_var_finalize(&temp);
    }else{
        // For others objects: just do a shallow copy.
        lai_var_assign(dest, source);
    }
}

static void lai_load_ns(lai_nsnode_t *src, lai_variable_t *object) {
    switch (src->type) {
        case LAI_NAMESPACE_NAME:
            lai_var_assign(object, &src->object);
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

static void lai_store_ns(lai_nsnode_t *target, lai_variable_t *object) {
    switch (target->type) {
        case LAI_NAMESPACE_NAME:
            lai_var_assign(&target->object, object);
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
void lai_load(lai_state_t *state, struct lai_operand *src, lai_variable_t *object) {
    switch (src->tag) {
        case LAI_ARG_NAME:
            LAI_ENSURE(state->invocation);
            lai_var_assign(object, &state->invocation->arg[src->index]);
            break;
        case LAI_LOCAL_NAME:
            LAI_ENSURE(state->invocation);
            lai_var_assign(object, &state->invocation->local[src->index]);
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
            lai_panic("tag %d is not valid for lai_load()", src->tag);
    }
}

// lai_store(): Stores a copy of the object to a reference.
void lai_store(lai_state_t *state, struct lai_operand *dest, lai_variable_t *object) {
    // First, handle stores to AML references (returned by Index() and friends).
    if (dest->tag == LAI_OPERAND_OBJECT) {
        switch (dest->object.type) {
            case LAI_STRING_INDEX: {
                char *window = dest->object.string_ptr->content;
                window[dest->object.integer] = object->integer;
                break;
            }
            case LAI_BUFFER_INDEX: {
                uint8_t *window = dest->object.buffer_ptr->content;
                window[dest->object.integer] = object->integer;
                break;
            }
            case LAI_PACKAGE_INDEX: {
                lai_variable_t copy = {0};
                lai_var_assign(&copy, object);
                lai_exec_pkg_var_store(&copy, dest->object.pkg_ptr, dest->object.integer);
                lai_var_finalize(&copy);
                break;
            }
            default:
                lai_panic("unexpected object type %d for lai_store()", dest->object.type);
        }
        return;
    }

    switch (dest->tag) {
        case LAI_NULL_NAME:
            // Stores to the null target are ignored.
            break;
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
            LAI_ENSURE(state->invocation);
            lai_var_assign(&state->invocation->arg[dest->index], object);
            break;
        case LAI_LOCAL_NAME:
            LAI_ENSURE(state->invocation);
            lai_var_assign(&state->invocation->local[dest->index], object);
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
            lai_panic("tag %d is not valid for lai_store()", dest->tag);
    }
}

static enum lai_object_type lai_object_type_of_objref(lai_variable_t *object) {
    switch (object->type) {
        case LAI_INTEGER:
            return LAI_TYPE_INTEGER;
        case LAI_STRING:
            return LAI_TYPE_STRING;
        case LAI_BUFFER:
            return LAI_TYPE_BUFFER;
        case LAI_PACKAGE:
            return LAI_TYPE_PACKAGE;

        default:
            lai_panic("unexpected object type %d in lai_object_type_of_objref()", object->type);
    }
}

static enum lai_object_type lai_object_type_of_node(lai_nsnode_t *handle) {
    switch (handle->type) {
        case LAI_NAMESPACE_DEVICE:
            return LAI_TYPE_DEVICE;

        default:
            lai_panic("unexpected node type %d in lai_object_type_of_node()", handle->type);
    }
}

enum lai_object_type lai_obj_get_type(lai_variable_t *object) {
    switch (object->type) {
        case LAI_INTEGER:
        case LAI_STRING:
        case LAI_BUFFER:
        case LAI_PACKAGE:
            return lai_object_type_of_objref(object);

        case LAI_HANDLE:
            return lai_object_type_of_node(object->handle);
        case LAI_LAZY_HANDLE: {
            struct lai_amlname amln;
            lai_amlname_parse(&amln, object->unres_aml);

            lai_nsnode_t *handle = lai_do_resolve(object->unres_ctx_handle, &amln);
            if(!handle)
                lai_panic("undefined reference %s", lai_stringify_amlname(&amln));
            return lai_object_type_of_node(handle);
        }

        default:
            lai_panic("unexpected object type %d for lai_obj_get_type()", object->type);
    }
}

// Load an object (i.e., integer, string, buffer, package) or reference.
// This is the access method used by Store().
// Returns immediate objects and indices as-is (i.e., without load from a name).
// Returns a view of an existing object and not a clone of the object.
void lai_exec_get_objectref(lai_state_t *state, struct lai_operand *src, lai_variable_t *object) {
    lai_variable_t temp = {0};
    if (src->tag == LAI_OPERAND_OBJECT) {
        lai_var_assign(&temp, &src->object);
    } else {
        lai_load(state, src, &temp);
    }
    lai_var_move(object, &temp);
}

// Load an integer.
// Returns immediate objects as-is.
void lai_exec_get_integer(lai_state_t *state, struct lai_operand *src, lai_variable_t *object) {
    lai_variable_t temp = {0};
    if (src->tag == LAI_OPERAND_OBJECT) {
        lai_var_assign(&temp, &src->object);
    } else {
        lai_load(state, src, &temp);
    }
    if(temp.type != LAI_INTEGER)
        lai_panic("lai_load_integer() expects an integer, not a value of type %d", temp.type);
    lai_var_move(object, &temp);
}

lai_api_error_t lai_obj_get_integer(lai_variable_t *object, uint64_t *out) {
    switch (object->type) {
        case LAI_INTEGER:
            *out = object->integer;
            return LAI_ERROR_NONE;

        default:
            lai_warn("lai_obj_get_integer() expects an integer, not a value of type %d",
                      object->type);
            return LAI_ERROR_TYPE_MISMATCH;
    }
}

lai_api_error_t lai_obj_get_pkg(lai_variable_t *object, size_t i, lai_variable_t *out) {
    if (object->type != LAI_PACKAGE)
        return LAI_ERROR_TYPE_MISMATCH;
    if (i >= lai_exec_pkg_size(object))
        return LAI_ERROR_OUT_OF_BOUNDS;
    lai_exec_pkg_load(out, object, i);
    return 0;
}

lai_api_error_t lai_obj_get_handle(lai_variable_t *object, lai_nsnode_t **out) {
    switch (object->type) {
        case LAI_HANDLE:
            *out = object->handle;
            return LAI_ERROR_NONE;
        case LAI_LAZY_HANDLE: {
            struct lai_amlname amln;
            lai_amlname_parse(&amln, object->unres_aml);

            lai_nsnode_t *handle = lai_do_resolve(object->unres_ctx_handle, &amln);
            if(!handle)
                lai_panic("undefined reference %s", lai_stringify_amlname(&amln));
            *out = handle;
            return LAI_ERROR_NONE;
        }

        default:
            lai_warn("lai_obj_get_handle() expects a handle type, not a value of type %d",
                      object->type);
            return LAI_ERROR_TYPE_MISMATCH;
    }
}

// lai_write_buffer(): Writes to a BufferField.
void lai_write_buffer(lai_nsnode_t *handle, lai_variable_t *source) {
    lai_nsnode_t *buffer_handle = handle->bf_node;

    uint64_t value = source->integer;

    // Offset that we are writing to, in bytes.
    size_t offset = handle->bf_offset;
    size_t size = handle->bf_size;
    uint8_t *data = lai_exec_buffer_access(&buffer_handle->object);

    int n = 0; // Number of bits that have been written.
    while (n < size) {
        // First bit (of the current byte) that will be overwritten.
        int bit = (offset + n) & 7;

        // Number of bits (of the current byte) that will be overwritten.
        int m = size - n;
        if (m > (8 - bit))
            m = 8 - bit;
        LAI_ENSURE(m); // Write at least one bit.

        uint8_t mask = (1 << m) - 1;
        data[(offset + n) >> 3] &= ~(mask << bit);
        data[(offset + n) >> 3] |= ((value >> n) & mask) << bit;

        n += m;
    }
}
