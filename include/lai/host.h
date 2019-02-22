
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 by Omar Muhamed
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define LAI_DEBUG_LOG 1
#define LAI_WARN_LOG 2

// OS-specific functions.
void lai_log(int, const char *, ...);
__attribute__((noreturn)) void lai_panic(const char *, ...);
void *lai_scan(char *, size_t);
void *lai_malloc(size_t);
void *lai_calloc(size_t, size_t);
void *lai_realloc(void *, size_t);
void lai_free(void *);
void *lai_map(size_t, size_t);
void lai_outb(uint16_t, uint8_t);
void lai_outw(uint16_t, uint16_t);
void lai_outd(uint16_t, uint32_t);
void lai_pci_write(uint8_t, uint8_t, uint8_t, uint16_t, uint32_t);
uint32_t lai_pci_read(uint8_t, uint8_t, uint8_t, uint16_t);
uint8_t lai_inb(uint16_t);
uint16_t lai_inw(uint16_t);
uint32_t lai_ind(uint16_t);
void lai_sleep(uint64_t);

