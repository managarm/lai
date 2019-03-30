
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
 */

// Internal header file. Do not use outside of LAI.

#pragma once

#include <lai/core.h>

// Namespace management.
lai_nsnode_t *lai_create_nsnode(void);
lai_nsnode_t *lai_create_nsnode_or_die(void);
void lai_install_nsnode(lai_nsnode_t *node);

// Namespace parsing function.
size_t lai_create_field(lai_nsnode_t *, void *);
size_t lai_create_method(lai_nsnode_t *, void *);
size_t lai_create_alias(lai_nsnode_t *, void *);
size_t lai_create_mutex(lai_nsnode_t *, void *);
size_t lai_create_indexfield(lai_nsnode_t *, void *);
size_t lai_create_processor(lai_nsnode_t *, void *);


size_t lai_create_bytefield(lai_nsnode_t *, void *);
size_t lai_create_wordfield(lai_nsnode_t *, void *);
size_t lai_create_dwordfield(lai_nsnode_t *, void *);
size_t lai_create_qwordfield(lai_nsnode_t *, void *);

size_t lai_resolve_path(lai_nsnode_t *, char *, uint8_t *);
