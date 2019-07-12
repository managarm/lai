
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
void lai_uninstall_nsnode(lai_nsnode_t *node);

// Sets the name and parent of a namespace node.
size_t lai_resolve_new_node(lai_nsnode_t *node, lai_nsnode_t *ctx_handle, void *data);

// Namespace parsing function.
size_t lai_create_method(lai_nsnode_t *, struct lai_aml_segment *, void *);
size_t lai_create_alias(lai_nsnode_t *, void *);
