/*
 * Lightweight ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
 */

#include <lai/core.h>
#include "libc.h"
#include "exec_impl.h"

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
        case 0:
            return LAI_TYPE_NONE;
        default:
            lai_panic("unexpected object type %d for lai_obj_get_type()", object->type);
    }
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

// lai_clone_buffer(): Clones a buffer object
static void lai_clone_buffer(lai_variable_t *dest, lai_variable_t *source) {
    size_t size = lai_exec_buffer_size(source);
    if (lai_create_buffer(dest, size))
        lai_panic("unable to allocate memory for buffer object.");
    memcpy(lai_exec_buffer_access(dest), lai_exec_buffer_access(source), size);
}

// lai_clone_string(): Clones a string object
static void lai_clone_string(lai_variable_t *dest, lai_variable_t *source) {
    size_t n = lai_exec_string_length(source);
    if (lai_create_string(dest, n))
        lai_panic("unable to allocate memory for string object.");
    memcpy(lai_exec_string_access(dest), lai_exec_string_access(source), n);
}

// lai_clone_package(): Clones a package object
static void lai_clone_package(lai_variable_t *dest, lai_variable_t *src) {
    size_t n = src->pkg_ptr->size;
    if (lai_create_pkg(dest, n))
        lai_panic("unable to allocate memory for package object.");
    for (int i = 0; i < n; i++)
        lai_obj_clone(&dest->pkg_ptr->elems[i], &src->pkg_ptr->elems[i]);
}

// lai_obj_clone(): Copies an object
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
