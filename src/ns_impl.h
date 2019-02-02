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

