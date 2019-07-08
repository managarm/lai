/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
 */

#pragma once

// Even in freestanding environments, GCC requires memcpy(), memmove(), memset()
// and memcmp() to be present. Thus, we just use them directly.
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);

//---------------------------------------------------------------------------------------
// Debugging and logging functions.
//---------------------------------------------------------------------------------------

void lai_debug(const char *, ...);
void lai_warn(const char *, ...);
__attribute__((noreturn)) void lai_panic(const char *, ...);

#define LAI_STRINGIFY(x) #x
#define LAI_EXPAND_STRINGIFY(x) LAI_STRINGIFY(x)

#define LAI_ENSURE(cond) \
    do { \
        if(!(cond)) \
            lai_panic("assertion failed: " #cond " at " \
                       __FILE__ ":" LAI_EXPAND_STRINGIFY(__LINE__) "\n"); \
    } while(0)

//---------------------------------------------------------------------------------------
// Reference counting functions.
//---------------------------------------------------------------------------------------

typedef int lai_rc_t;

__attribute__((always_inline))
inline void lai_rc_ref(lai_rc_t *rc_ptr) {
    lai_rc_t nrefs = (*rc_ptr)++;
    LAI_ENSURE(nrefs > 0);
}

__attribute__((always_inline))
inline int lai_rc_unref(lai_rc_t *rc_ptr) {
    lai_rc_t nrefs = --(*rc_ptr);
    LAI_ENSURE(nrefs >= 0);
    return !nrefs;
}

//---------------------------------------------------------------------------------------
// List data structure.
//---------------------------------------------------------------------------------------

struct lai_list_item {
    struct lai_list_item *next;
    struct lai_list_item *prev;
};

struct lai_list {
    struct lai_list_item hook;
};
