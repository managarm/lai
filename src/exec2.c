
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 by Omar Muhamed
 */

#include "lai.h"

/* ACPI Control Method Execution */
/* Type2Opcode := DefAcquire | DefAdd | DefAnd | DefBuffer | DefConcat |
   DefConcatRes | DefCondRefOf | DefCopyObject | DefDecrement |
   DefDerefOf | DefDivide | DefFindSetLeftBit | DefFindSetRightBit |
   DefFromBCD | DefIncrement | DefIndex | DefLAnd | DefLEqual |
   DefLGreater | DefLGreaterEqual | DefLLess | DefLLessEqual | DefMid |
   DefLNot | DefLNotEqual | DefLoadTable | DefLOr | DefMatch | DefMod |
   DefMultiply | DefNAnd | DefNOr | DefNot | DefObjectType | DefOr |
   DefPackage | DefVarPackage | DefRefOf | DefShiftLeft | DefShiftRight |
   DefSizeOf | DefStore | DefSubtract | DefTimer | DefToBCD | DefToBuffer |
   DefToDecimalString | DefToHexString | DefToInteger | DefToString |
   DefWait | DefXOr | UserTermObj */

// acpi_exec_resolve(): Resolves a name during control method execution
// Param:    char *path - 4-char object name or full path
// Return:    acpi_nsnode_t * - pointer to namespace object, NULL on error

acpi_nsnode_t *acpi_exec_resolve(char *path)
{
    acpi_nsnode_t *object;
    object = acpins_resolve(path);

    if(acpi_strlen(path) == 4)
        return acpins_resolve(path);

    while(!object && acpi_strlen(path) > 6)
    {
        acpi_memmove(path + acpi_strlen(path) - 9, path + acpi_strlen(path) - 4, 5);
        object = acpins_resolve(path);
        if(object != NULL)
            goto resolve_alias;
    }

    if(object == NULL)
        return NULL;

resolve_alias:
    // resolve Aliases too
    while(object->type == ACPI_NAMESPACE_ALIAS)
    {
        object = acpins_resolve(object->alias);
        if(!object)
            return NULL;
    }

    return object;
}

// acpi_free_package(): Frees a package object and all its children
// Param:   acpi_object_t *object
// Return:  Nothing

static void acpi_free_package(acpi_object_t *object)
{
    for(int i = 0; i < object->package_size; i++)
        acpi_free_object(&object->package[i]);
    acpi_free(object->package);
}

void acpi_free_object(acpi_object_t *object)
{
    if(object->type == ACPI_BUFFER)
        acpi_free(object->buffer);
    else if(object->type == ACPI_PACKAGE)
        acpi_free_package(object);

    acpi_memset(object, 0, sizeof(acpi_object_t));
}

// acpi_move_object(): Moves an object: instead of making a deep copy,
//                     the pointers are exchanged and the source object is reset to zero.
// Param & Return: See acpi_copy_object().

void acpi_move_object(acpi_object_t *destination, acpi_object_t *source)
{
    acpi_free_object(destination);
    acpi_memcpy(destination, source, sizeof(acpi_object_t));
    acpi_memset(source, 0, sizeof(acpi_object_t));
}

// acpi_copy_buffer(): Copies a buffer object
// Param:    acpi_object_t *destination - destination
// Param:    acpi_object_t *source - source
// Return:   Nothing

static void acpi_copy_buffer(acpi_object_t *destination, acpi_object_t *source)
{
    destination->type = ACPI_BUFFER;
    destination->buffer_size = source->buffer_size;
    destination->buffer = acpi_malloc(source->buffer_size);
    if(!destination->buffer)
        acpi_panic("unable to allocate memory for buffer object.\n");

    acpi_memcpy(destination->buffer, source->buffer, source->buffer_size);
}

// acpi_copy_string(): Copies a string object
// Param:    acpi_object_t *destination - destination
// Param:    acpi_object_t *source - source
// Return:   Nothing

static void acpi_copy_string(acpi_object_t *destination, acpi_object_t *source)
{
    destination->type = ACPI_STRING;
    destination->string = acpi_malloc(acpi_strlen(source->string) + 1);
    if(!destination->string)
        acpi_panic("unable to allocate memory for string object.\n");

    acpi_strcpy(destination->string, source->string);
}

// acpi_copy_package(): Copies a package object
// Param:    acpi_object_t *destination - destination
// Param:    acpi_object_t *source - source
// Return:   Nothing

static void acpi_copy_package(acpi_object_t *destination, acpi_object_t *source)
{
    destination->type = ACPI_PACKAGE;
    destination->package_size = source->package_size;
    destination->package = acpi_calloc(ACPI_MAX_PACKAGE_ENTRIES, sizeof(acpi_object_t));

    if(!source->package_size)
    {
        destination->package_size = ACPI_MAX_PACKAGE_ENTRIES;
        return;
    }

    for(int i = 0; i < source->package_size; i++)
        acpi_copy_object(&destination->package[i], &source->package[i]);
}

// acpi_copy_object(): Copies an object
// Param:    acpi_object_t *destination - destination
// Param:    acpi_object_t *source - source
// Return:   Nothing

void acpi_copy_object(acpi_object_t *destination, acpi_object_t *source)
{
    if(destination == source)
        return;

    acpi_free_object(destination);

    if(source->type == ACPI_PACKAGE)
        acpi_copy_package(destination, source);
    else if(source->type == ACPI_STRING)
        acpi_copy_string(destination, source);
    else if(source->type == ACPI_BUFFER)
        acpi_copy_buffer(destination, source);
    else
        acpi_memcpy(destination, source, sizeof(acpi_object_t));
}

// acpi_write_object(): Writes to an object
// Param:    void *data - destination to be parsed
// Param:    acpi_object_t *source - source object
// Param:    acpi_state_t *state - state of the AML VM
// Return:    size_t - size in bytes for skipping

size_t acpi_write_object(void *data, acpi_object_t *source, acpi_state_t *state)
{
    uint8_t *dest = (uint8_t*)data;
    size_t return_size = 0;

    acpi_object_t *dest_reg;

    // try register
    if(dest[0] >= LOCAL0_OP && dest[0] <= LOCAL7_OP)
    {
        switch(dest[0])
        {
        case LOCAL0_OP:
            dest_reg = &state->local[0];
            break;
        case LOCAL1_OP:
            dest_reg = &state->local[1];
            break;
        case LOCAL2_OP:
            dest_reg = &state->local[2];
            break;
        case LOCAL3_OP:
            dest_reg = &state->local[3];
            break;
        case LOCAL4_OP:
            dest_reg = &state->local[4];
            break;
        case LOCAL5_OP:
            dest_reg = &state->local[5];
            break;
        case LOCAL6_OP:
            dest_reg = &state->local[6];
            break;
        case LOCAL7_OP:
            dest_reg = &state->local[7];
            break;
        }

        acpi_copy_object(dest_reg, source);
        return 1;
    } else if(dest[0] >= ARG0_OP && dest[0] <= ARG6_OP)
    {
        switch(dest[0])
        {
        case ARG0_OP:
            dest_reg = &state->arg[0];
            break;
        case ARG1_OP:
            dest_reg = &state->arg[1];
            break;
        case ARG2_OP:
            dest_reg = &state->arg[2];
            break;
        case ARG3_OP:
            dest_reg = &state->arg[3];
            break;
        case ARG4_OP:
            dest_reg = &state->arg[4];
            break;
        case ARG5_OP:
            dest_reg = &state->arg[5];
            break;
        case ARG6_OP:
            dest_reg = &state->arg[6];
            break;
        }

        acpi_copy_object(dest_reg, source);
        return 1;
    }

    // try integer
    // in which case we use va_list to store the destination in its own object
    uint64_t integer;
    size_t integer_size = acpi_eval_integer(dest, &integer);
    if(integer_size != 0)
    {
        /*va_list params;
        va_start(params, state);

        acpi_object_t *tmp = va_arg(params, acpi_object_t*);
        acpi_copy_object(tmp, source);

        va_end(params);*/
        return integer_size;
    }

    // try name spec
    // here, the name may only be an object or a field, it cannot be a MethodInvokation
    if(acpi_is_name(dest[0]))
    {
        char name[ACPI_MAX_NAME];
        size_t name_size;
        name_size = acpins_resolve_path(name, dest);
        acpi_nsnode_t *handle = acpi_exec_resolve(name);
        if(!handle)
        {
            acpi_panic("undefined reference %s\n", name);
        }

        if(handle->type == ACPI_NAMESPACE_NAME)
            acpi_copy_object(&handle->object, source);
        else if(handle->type == ACPI_NAMESPACE_FIELD || handle->type == ACPI_NAMESPACE_INDEXFIELD)
            acpi_write_opregion(handle, source);
        else if(handle->type == ACPI_NAMESPACE_BUFFER_FIELD)
            acpi_write_buffer(handle, source);
        else
        {
            acpi_panic("NameSpec destination is not a writeable object.\n");
        }

        return name_size;
    } else if(dest[0] == INDEX_OP)
    {
        return_size = 1;
        dest++;

        // the first object should be a package
        acpi_object_t object = {0};
        acpi_object_t index = {0};

        size_t object_size;
        object_size = acpi_eval_object(&object, state, &dest[0]);
        return_size += object_size;
        dest += object_size;

        object_size = acpi_eval_object(&index, state, &dest[0]);
        return_size += object_size;

        if(object.type == ACPI_PACKAGE)
        {
            if(index.integer >= object.package_size)
                acpi_panic("attempt to write to index %d of package of length %d\n", index.integer, object.package_size);

            acpi_copy_object(&object.package[index.integer], source);
            return return_size;
        } else
        {
            acpi_panic("cannot write Index() to non-package object: %d\n", object.type);
        }
    }

    acpi_panic("undefined opcode, sequence %02X %02X %02X %02X\n", dest[0], dest[1], dest[2], dest[3]);
}

// acpi_write_buffer(): Writes to a Buffer Field
// Param:    acpi_nsnode_t *handle - handle of buffer field
// Param:    acpi_object_t *source - object to write
// Return:    Nothing

void acpi_write_buffer(acpi_nsnode_t *handle, acpi_object_t *source)
{
    acpi_nsnode_t *buffer_handle;
    buffer_handle = acpins_resolve(handle->buffer);

    if(!buffer_handle)
        acpi_debug("undefined reference %s\n", handle->buffer);

    uint64_t value = source->integer;

    uint64_t offset = handle->buffer_offset / 8;
    uint64_t bitshift = handle->buffer_offset % 8;

    value <<= bitshift;

    uint64_t mask = 1;
    mask <<= bitshift;
    mask--;
    mask <<= bitshift;

    uint8_t *byte = (uint8_t*)(buffer_handle->object.buffer + offset);
    uint16_t *word = (uint16_t*)(buffer_handle->object.buffer + offset);
    uint32_t *dword = (uint32_t*)(buffer_handle->object.buffer + offset);
    uint64_t *qword = (uint64_t*)(buffer_handle->object.buffer + offset);

    if(handle->buffer_size <= 8)
    {
        byte[0] &= (uint8_t)mask;
        byte[0] |= (uint8_t)value;
    } else if(handle->buffer_size <= 16)
    {
        word[0] &= (uint16_t)mask;
        word[0] |= (uint16_t)value;
    } else if(handle->buffer_size <= 32)
    {
        dword[0] &= (uint32_t)mask;
        dword[0] |= (uint32_t)value;
    } else if(handle->buffer_size <= 64)
    {
        qword[0] &= mask;
        qword[0] |= value;
    }
}

// acpi_exec_store(): Executes a Store() opcode
// Param:    void *data - pointer to opcode
// Param:    acpi_state_t *state - ACPI AML state
// Return:    size_t - size in bytes for skipping

size_t acpi_exec_store(void *data, acpi_state_t *state)
{
    size_t return_size = 1;
    uint8_t *store = (uint8_t*)data;
    store++;        // skip STORE_OP

    size_t dest_size, source_size;
    acpi_object_t source = {0};

    // determine the source project
    source_size = acpi_eval_object(&source, state, &store[0]);
    return_size += source_size;
    store += source_size;

    // now work on the destination
    // destination may be name or variable
    dest_size = acpi_write_object(&store[0], &source, state);

    return_size += dest_size;
    return return_size;
}

// acpi_exec_add(): Executes an Add() opcode
// Param:    void *data - pointer to opcode
// Param:    acpi_state_t *state - ACPI AML state
// Return:    size_t - size in bytes for skipping

size_t acpi_exec_add(void *data, acpi_state_t *state)
{
    size_t return_size = 1;
    uint8_t *opcode = (uint8_t*)data;
    opcode++;

    acpi_object_t n1 = {0};
    acpi_object_t n2 = {0};
    size_t size;

    size = acpi_eval_object(&n1, state, &opcode[0]);
    opcode += size;
    return_size += size;

    size = acpi_eval_object(&n2, state, &opcode[0]);
    opcode += size;
    return_size += size;

    n1.integer += n2.integer;

    size = acpi_write_object(&opcode[0], &n1, state);
    return_size += size;

    return return_size;
}

// acpi_exec_increment(): Executes Increment() instruction
// Param:    void *data - pointer to opcode
// Param:    acpi_state_t *state - ACPI AML state
// Return:    size_t - size in bytes for skipping

size_t acpi_exec_increment(void *data, acpi_state_t *state)
{
    size_t return_size = 1;
    uint8_t *opcode = (uint8_t*)data;
    opcode++;

    acpi_object_t n = {0};
    size_t size;

    size = acpi_eval_object(&n, state, &opcode[0]);
    n.integer++;
    acpi_write_object(&opcode[0], &n, state);

    return_size += size;
    return return_size;
}

// acpi_exec_decrement(): Executes Decrement() instruction
// Param:    void *data - pointer to opcode
// Param:    acpi_state_t *state - ACPI AML state
// Return:    size_t - size in bytes for skipping

size_t acpi_exec_decrement(void *data, acpi_state_t *state)
{
    size_t return_size = 1;
    uint8_t *opcode = (uint8_t*)data;
    opcode++;

    acpi_object_t n = {0};
    size_t size;

    size = acpi_eval_object(&n, state, &opcode[0]);
    n.integer--;
    acpi_write_object(&opcode[0], &n, state);

    return_size += size;
    return return_size;
}

// acpi_exec_and(): Executes an And() opcode
// Param:    void *data - pointer to opcode
// Param:    acpi_state_t *state - ACPI AML state
// Return:    size_t - size in bytes for skipping

size_t acpi_exec_and(void *data, acpi_state_t *state)
{
    size_t return_size = 1;
    uint8_t *opcode = (uint8_t*)data;
    opcode++;

    acpi_object_t n1 = {0};
    acpi_object_t n2 = {0};
    size_t size;

    size = acpi_eval_object(&n1, state, &opcode[0]);
    opcode += size;
    return_size += size;

    size = acpi_eval_object(&n2, state, &opcode[0]);
    opcode += size;
    return_size += size;

    n1.integer &= n2.integer;

    size = acpi_write_object(&opcode[0], &n1, state);
    return_size += size;

    return return_size;
}

// acpi_exec_or(): Executes an Or() opcode
// Param:    void *data - pointer to opcode
// Param:    acpi_state_t *state - ACPI AML state
// Return:    size_t - size in bytes for skipping

size_t acpi_exec_or(void *data, acpi_state_t *state)
{
    size_t return_size = 1;
    uint8_t *opcode = (uint8_t*)data;
    opcode++;

    acpi_object_t n1 = {0};
    acpi_object_t n2 = {0};
    size_t size;

    size = acpi_eval_object(&n1, state, &opcode[0]);
    opcode += size;
    return_size += size;

    size = acpi_eval_object(&n2, state, &opcode[0]);
    opcode += size;
    return_size += size;

    n1.integer |= n2.integer;

    size = acpi_write_object(&opcode[0], &n1, state);
    return_size += size;

    return return_size;
}

// acpi_exec_subtract(): Executes a Subtract() opcode
// Param:    void *data - pointer to opcode
// Param:    acpi_state_t *state - ACPI AML state
// Return:    size_t - size in bytes for skipping

size_t acpi_exec_subtract(void *data, acpi_state_t *state)
{
    size_t return_size = 1;
    uint8_t *opcode = (uint8_t*)data;
    opcode++;

    acpi_object_t n1 = {0};
    acpi_object_t n2 = {0};
    size_t size;

    size = acpi_eval_object(&n1, state, &opcode[0]);
    opcode += size;
    return_size += size;

    size = acpi_eval_object(&n2, state, &opcode[0]);
    opcode += size;
    return_size += size;

    n1.integer -= n2.integer;

    size = acpi_write_object(&opcode[0], &n1, state);
    return_size += size;

    return return_size;
}

// acpi_exec_not(): Executes a Not() opcode
// Param:    void *data - pointer to opcode
// Param:    acpi_state_t *state - ACPI AML state
// Return:    size_t - size in bytes for skipping

size_t acpi_exec_not(void *data, acpi_state_t *state)
{
    size_t return_size = 1;
    uint8_t *opcode = (uint8_t*)data;
    opcode++;

    acpi_object_t n1 = {0};
    size_t size;

    size = acpi_eval_object(&n1, state, &opcode[0]);
    opcode += size;
    return_size += size;

    n1.integer = ~n1.integer;
    size = acpi_write_object(&opcode[0], &n1, state);
    return_size += size;

    return return_size;
}

// acpi_exec_xor(): Executes an Xor() opcode
// Param:    void *data - pointer to opcode
// Param:    acpi_state_t *state - ACPI AML state
// Return:    size_t - size in bytes for skipping

size_t acpi_exec_xor(void *data, acpi_state_t *state)
{
    size_t return_size = 1;
    uint8_t *opcode = (uint8_t*)data;
    opcode++;

    acpi_object_t n1 = {0};
    acpi_object_t n2 = {0};
    size_t size;

    size = acpi_eval_object(&n1, state, &opcode[0]);
    opcode += size;
    return_size += size;

    size = acpi_eval_object(&n2, state, &opcode[0]);
    opcode += size;
    return_size += size;

    n1.integer ^= n2.integer;

    size = acpi_write_object(&opcode[0], &n1, state);
    return_size += size;

    return return_size;
}

// acpi_exec_shl(): Executes a Shl() opcode
// Param:    void *data - pointer to opcode
// Param:    acpi_state_t *state - ACPI AML state
// Return:    size_t - size in bytes for skipping

size_t acpi_exec_shl(void *data, acpi_state_t *state)
{
    size_t return_size = 1;
    uint8_t *opcode = (uint8_t*)data;
    opcode++;

    acpi_object_t n1 = {0};
    acpi_object_t n2 = {0};
    size_t size;

    size = acpi_eval_object(&n1, state, &opcode[0]);
    opcode += size;
    return_size += size;

    size = acpi_eval_object(&n2, state, &opcode[0]);
    opcode += size;
    return_size += size;

    n1.integer <<= n2.integer;

    size = acpi_write_object(&opcode[0], &n1, state);
    return_size += size;

    return return_size;
}

// acpi_exec_shr(): Executes a Shr() opcode
// Param:    void *data - pointer to opcode
// Param:    acpi_state_t *state - ACPI AML state
// Return:    size_t - size in bytes for skipping

size_t acpi_exec_shr(void *data, acpi_state_t *state)
{
    size_t return_size = 1;
    uint8_t *opcode = (uint8_t*)data;
    opcode++;

    acpi_object_t n1 = {0};
    acpi_object_t n2 = {0};
    size_t size;

    size = acpi_eval_object(&n1, state, &opcode[0]);
    opcode += size;
    return_size += size;

    size = acpi_eval_object(&n2, state, &opcode[0]);
    opcode += size;
    return_size += size;

    n1.integer >>= n2.integer;

    size = acpi_write_object(&opcode[0], &n1, state);
    return_size += size;

    return return_size;
}

// acpi_exec_multiply(): Executes a Multiply() opcode
// Param:    void *data - pointer to opcode
// Param:    acpi_state_t *state - ACPI AML state
// Return:    size_t - size in bytes for skipping

size_t acpi_exec_multiply(void *data, acpi_state_t *state)
{
    size_t return_size = 1;
    uint8_t *opcode = (uint8_t*)data;
    opcode++;

    acpi_object_t n1 = {0};
    acpi_object_t n2 = {0};
    size_t size;

    size = acpi_eval_object(&n1, state, &opcode[0]);
    opcode += size;
    return_size += size;

    size = acpi_eval_object(&n2, state, &opcode[0]);
    opcode += size;
    return_size += size;

    n1.integer = n1.integer * n2.integer;

    size = acpi_write_object(&opcode[0], &n1, state);
    return_size += size;

    return return_size;
}

// acpi_exec_divide(): Executes a Divide() opcode
// Param:    void *data - pointer to opcode
// Param:    acpi_state_t *state - ACPI AML state
// Return:    size_t - size in bytes for skipping

size_t acpi_exec_divide(void *data, acpi_state_t *state)
{
    size_t return_size = 1;
    uint8_t *opcode = (uint8_t*)data;
    opcode++;

    acpi_object_t n1 = {0};
    acpi_object_t n2 = {0};
    acpi_object_t mod = {0};
    acpi_object_t quo = {0};
    size_t size;

    size = acpi_eval_object(&n1, state, &opcode[0]);
    opcode += size;
    return_size += size;

    size = acpi_eval_object(&n2, state, &opcode[0]);
    opcode += size;
    return_size += size;

    mod.type = ACPI_INTEGER;
    quo.type = ACPI_INTEGER;

    mod.integer = n1.integer % n2.integer;
    quo.integer = n1.integer / n2.integer;

    size = acpi_write_object(&opcode[0], &mod, state);
    return_size += size;
    opcode += size;

    size = acpi_write_object(&opcode[0], &quo, state);
    return_size += size;

    return return_size;
}



