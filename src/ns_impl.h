/*
 * Lux ACPI Implementation
 * Copyright (C) 2019 by LAI contributors
 */

// Internal header file. Do not use outside of LAI.

#pragma once

#include <lai/core.h>

// Namespace management.
lai_nsnode_t *acpins_create_nsnode();
lai_nsnode_t *acpins_create_nsnode_or_die();
void acpins_install_nsnode(lai_nsnode_t *node);

// Namespace parsing function.
size_t acpins_create_field(lai_nsnode_t *, void *);
size_t acpins_create_method(lai_nsnode_t *, void *);
size_t acpins_create_alias(lai_nsnode_t *, void *);
size_t acpins_create_mutex(lai_nsnode_t *, void *);
size_t acpins_create_indexfield(lai_nsnode_t *, void *);
size_t acpins_create_processor(lai_nsnode_t *, void *);

size_t acpins_create_bytefield(lai_nsnode_t *, void *);
size_t acpins_create_wordfield(lai_nsnode_t *, void *);
size_t acpins_create_dwordfield(lai_nsnode_t *, void *);
size_t acpins_create_qwordfield(lai_nsnode_t *, void *);

