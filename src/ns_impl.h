/*
 * Lux ACPI Implementation
 * Copyright (C) 2019 by LAI contributors
 */

// Internal header file. Do not use outside of LAI.

#pragma once

#include <lai.h>

// Namespace management.
acpi_nsnode_t *acpins_create_nsnode();
acpi_nsnode_t *acpins_create_nsnode_or_die();
void acpins_install_nsnode(acpi_nsnode_t *node);

// Namespace parsing function.
void acpins_register_scope(acpi_nsnode_t *, uint8_t *, size_t);
size_t acpins_create_scope(acpi_nsnode_t *, void *);
size_t acpins_create_field(acpi_nsnode_t *, void *);
size_t acpins_create_method(acpi_nsnode_t *, void *);
size_t acpins_create_device(acpi_nsnode_t *, void *);
size_t acpins_create_thermalzone(acpi_nsnode_t *, void *);
size_t acpins_create_alias(acpi_nsnode_t *, void *);
size_t acpins_create_mutex(acpi_nsnode_t *, void *);
size_t acpins_create_indexfield(acpi_nsnode_t *, void *);
size_t acpins_create_processor(acpi_nsnode_t *, void *);

size_t acpins_create_bytefield(acpi_nsnode_t *, void *);
size_t acpins_create_wordfield(acpi_nsnode_t *, void *);
size_t acpins_create_dwordfield(acpi_nsnode_t *, void *);
size_t acpins_create_qwordfield(acpi_nsnode_t *, void *);

