
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 by Omar Muhamed
 */

/* Windozz-specific OS functions */

#include <stddef.h>
#include <string.h>
#include <mm.h>
#include <io.h>
#include <pci.h>
#include <acpi.h>
#include <timer.h>

// Any OS using lai must provide implementations of the following functions

void *lai_scan(char *name, size_t index)
{
    void *ptr;
    acpi_status_t status = acpi_find_table(&ptr, name, index);
    if(status == ACPI_SUCCESS)
        return ptr;
    else
        return NULL;
}

void *lai_memcpy(void *dest, const void *src, size_t count)
{
    return memcpy(dest, src, count);
}

void *lai_memmove(void *dest, const void *src, size_t count)
{
    return memmove(dest, src, count);
}

char *lai_strcpy(char *dest, const char *src)
{
    return strcpy(dest, src);
}

void *lai_malloc(size_t count)
{
    return kmalloc(count);
}

void *lai_realloc(void *ptr, size_t count)
{
    return krealloc(ptr, count);
}

void *lai_calloc(size_t n, size_t size)
{
    return kcalloc(n, size);
}

void lai_free(void *ptr)
{
    kfree(ptr);
}

void *lai_map(size_t physical, size_t count)
{
    count += PAGE_SIZE - 1;
    count >>= PAGE_SIZE_SHIFT;
    return (void*)vmm_create_mmio(physical, count, "acpi");
}

size_t lai_strlen(const char *string)
{
    return strlen(string);
}

void *lai_memset(void *dest, int val, size_t count)
{
    return memset(dest, val, count);
}

int lai_memcmp(const void *m1, const void *m2, size_t count)
{    
    return memcmp(m1, m2, count);
}

int lai_strcmp(const char *s1, const char *s2)
{
    return strcmp(s1, s2);
}

void lai_outb(uint16_t port, uint8_t data)
{
    outb(port, data);
}

void lai_outw(uint16_t port, uint16_t data)
{
    outw(port, data);
}

void lai_outd(uint16_t port, uint32_t data)
{
    outd(port, data);
}

uint8_t lai_inb(uint16_t port)
{
    return inb(port);
}

uint16_t lai_inw(uint16_t port)
{
    return inw(port);
}

uint32_t lai_ind(uint16_t port)
{
    return ind(port);
}

void lai_pci_write(uint8_t bus, uint8_t slot, uint8_t function, uint16_t offset, uint32_t data)
{
    pci_dev_t dev;
    dev.bus = bus;
    dev.slot = slot;
    dev.function = function;

    pci_write(&dev, offset, data);
}

uint32_t lai_pci_read(uint8_t bus, uint8_t slot, uint8_t function, uint16_t offset)
{
    pci_dev_t dev;
    dev.bus = bus;
    dev.slot = slot;
    dev.function = function;

    return pci_read(&dev, offset);
}

void lai_sleep(uint64_t time)
{
    timer_sleep(time);
}





