
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 by Omar Muhamed
 */

#include "lai.h"
#include "ns_impl.h"

// acpi_is_name(): Evaluates a name character
// Param:    char character - character from name
// Return:    int - 1 if it's a name, 0 if it's not

int acpi_is_name(char character)
{
    if((character >= '0' && character <= 'Z') || character == '_' || character == ROOT_CHAR || character == PARENT_CHAR || character == MULTI_PREFIX || character == DUAL_PREFIX)
        return 1;

    else
        return 0;
}

// acpi_eval_integer(): Evaluates an integer object
// Param:    uint8_t *object - pointer to object
// Param:    uint64_t *integer - destination
// Return:    size_t - size of object in bytes, 0 if it's not an integer

size_t acpi_eval_integer(uint8_t *object, uint64_t *integer)
{
    uint8_t *byte = (uint8_t*)(object + 1);
    uint16_t *word = (uint16_t*)(object + 1);
    uint32_t *dword = (uint32_t*)(object + 1);
    uint64_t *qword = (uint64_t*)(object + 1);

    switch(object[0])
    {
    case ZERO_OP:
        integer[0] = 0;
        return 1;
    case ONE_OP:
        integer[0] = 1;
        return 1;
    case ONES_OP:
        integer[0] = 0xFFFFFFFFFFFFFFFF;
        return 1;
    case BYTEPREFIX:
        integer[0] = (uint64_t)byte[0];
        return 2;
    case WORDPREFIX:
        integer[0] = (uint64_t)word[0];
        return 3;
    case DWORDPREFIX:
        integer[0] = (uint64_t)dword[0];
        return 5;
    case QWORDPREFIX:
        integer[0] = qword[0];
        return 9;
    default:
        return 0;
    }
}

// acpi_parse_pkgsize(): Parses package size
// Param:    uint8_t *data - pointer to package size data
// Param:    size_t *destination - destination to store package size
// Return:    size_t - size of package size encoding

size_t acpi_parse_pkgsize(uint8_t *data, size_t *destination)
{
    destination[0] = 0;

    uint8_t bytecount = (data[0] >> 6) & 3;
    if(bytecount == 0)
        destination[0] = (size_t)(data[0] & 0x3F);
    else if(bytecount == 1)
    {
        destination[0] = (size_t)(data[0] & 0x0F);
        destination[0] |= (size_t)(data[1] << 4);
    } else if(bytecount == 2)
    {
        destination[0] = (size_t)(data[0] & 0x0F);
        destination[0] |= (size_t)(data[1] << 4);
        destination[0] |= (size_t)(data[2] << 12);
    } else if(bytecount == 3)
    {
        destination[0] = (size_t)(data[0] & 0x0F);
        destination[0] |= (size_t)(data[1] << 4);
        destination[0] |= (size_t)(data[2] << 12);
        destination[0] |= (size_t)(data[3] << 20);
    }

    return (size_t)(bytecount + 1);
}

// acpi_eval_package(): Evaluates a package
// Param:    acpi_object_t *package - pointer to package object
// Param:    size_t index - index to evaluate
// Param:    acpi_object_t *destination - where to store value
// Return:    int - 0 on success

int acpi_eval_package(acpi_object_t *package, size_t index, acpi_object_t *destination)
{
    if(package->type != ACPI_PACKAGE)
    {
        acpi_warn("attempt to evaluate non-package object.\n");
        return 1;
    } else if(index >= package->package_size)
    {
        acpi_warn("attempt to evaluate index %d of package of size %d\n", index, package->package_size);
        return 1;
    }

    acpi_copy_object(destination, &package->package[index]);
    return 0;
}

// acpi_eval(): Returns an object
// Param:    acpi_object_t *destination - where to store object
// Param:    char *path - path of object
// Return:    int - 0 on success

int acpi_eval(acpi_object_t *destination, char *path)
{
    acpi_nsnode_t *handle;
    char *path_copy = acpi_malloc(acpi_strlen(path) + 1);
    acpi_strcpy(path_copy, path);
    handle = acpi_exec_resolve(path_copy);
    acpi_free(path_copy);
    if(!handle)
        return 1;

    while(handle->type == ACPI_NAMESPACE_ALIAS)
    {
        handle = acpins_resolve(handle->alias);
        if(!handle)
            return 1;
    }

    if(handle->type == ACPI_NAMESPACE_NAME)
    {
        acpi_copy_object(destination, &handle->object);
        return 0;
    } else if(handle->type == ACPI_NAMESPACE_METHOD)
    {
        acpi_state_t state;
        acpi_init_call_state(&state, handle);
        int ret;
        if((ret = acpi_exec_method(&state)))
            return ret;
        acpi_move_object(destination, &state.retvalue);
        acpi_finalize_state(&state);
        return 0;
    }

    return 1;
}

// acpi_bswap16(): Switches endianness of a WORD
// Param:    uint16_t word - WORD
// Return:    uint16_t - switched value

uint16_t acpi_bswap16(uint16_t word)
{
    return (uint16_t)((word >> 8) & 0xFF) | ((word << 8) & 0xFF00);
}

// acpi_bswap32(): Switches endianness of a DWORD
// Param:    uint32_t dword - DWORD
// Return:    uint32 - switched value

uint32_t acpi_bswap32(uint32_t dword)
{
    return (uint32_t)((dword>>24) & 0xFF) | ((dword<<8) & 0xFF0000) | ((dword>>8)&0xFF00) | ((dword<<24)&0xFF000000);
}

// acpi_char_to_hex(): Converts an ASCII hex character to a hex value
// Param:    char character - ASCII hex char
// Return:    uint8_t - hex value

uint8_t acpi_char_to_hex(char character)
{
    if(character <= '9')
        return character - '0';
    else if(character >= 'A' && character <= 'F')
        return character - 'A' + 10;
    else if(character >= 'a' && character <= 'f')
        return character - 'a' + 10;

    return 0;
}

// acpi_eisaid(): Converts a PNP ID to an ACPI object
// Param:    acpi_object_t *object - destination
// Param:    char *id - ACPI PNP ID
// Return:    Nothing

void acpi_eisaid(acpi_object_t *object, char *id)
{
    if(acpi_strlen(id) != 7)
    {
        object->type = ACPI_STRING;
        object->string = id;
        return;
    }

    // convert a string in the format "UUUXXXX" to an integer
    // "U" is an ASCII character, and "X" is an ASCII hex digit
    object->type = ACPI_INTEGER;

    uint32_t out = 0;
    out |= ((id[0] - 0x40) << 26);
    out |= ((id[1] - 0x40) << 21);
    out |= ((id[2] - 0x40) << 16);
    out |= acpi_char_to_hex(id[3]) << 12;
    out |= acpi_char_to_hex(id[4]) << 8;
    out |= acpi_char_to_hex(id[5]) << 4;
    out |= acpi_char_to_hex(id[6]);

    out = acpi_bswap32(out);
    object->integer = (uint64_t)out & 0xFFFFFFFF;

}

