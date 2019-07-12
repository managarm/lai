/*
 * Lightweight ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
 */

#pragma once

#include <lai/core.h>

__attribute__((deprecated("use lai_pci_route_pin instead")))
int lai_pci_route(acpi_resource_t *, uint16_t, uint8_t, uint8_t, uint8_t);
lai_api_error_t lai_pci_route_pin(acpi_resource_t *, uint16_t, uint8_t, uint8_t, uint8_t, uint8_t);