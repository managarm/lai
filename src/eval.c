
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018 by Omar Mohammad
 */

#include "lai.h"

// acpi_is_name(): Evaluates a name character
// Param:	char character - character from name
// Return:	int - 1 if it's a name, 0 if it's not

int acpi_is_name(char character)
{
	if((character >= '0' && character <= 'Z') || character == '_' || character == ROOT_CHAR || character == PARENT_CHAR || character == MULTI_PREFIX || character == DUAL_PREFIX)
		return 1;

	else
		return 0;
}

// acpi_eval_integer(): Evaluates an integer object
// Param:	uint8_t *object - pointer to object
// Param:	uint64_t *integer - destination
// Return:	size_t - size of object in bytes, 0 if it's not an integer

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
// Param:	uint8_t *data - pointer to package size data
// Param:	size_t *destination - destination to store package size
// Return:	size_t - size of package size encoding

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
// Param:	acpi_object_t *package - pointer to package object
// Param:	size_t index - index to evaluate
// Param:	acpi_object_t *destination - where to store value
// Return:	int - 0 on success

int acpi_eval_package(acpi_object_t *package, size_t index, acpi_object_t *destination)
{
	if(index >= package->package_size)
		return 1;

	acpi_memcpy(destination, &package->package[index], sizeof(acpi_object_t));
	return 0;
}

// acpi_eval_object(): Evaluates an object
// Param:	acpi_object_t *destination - pointer to where to store object
// Param:	acpi_state_t *state - AML VM state
// Param:	void *data - data of object
// Return:	size_t - size in bytes for skipping

size_t acpi_eval_object(acpi_object_t *destination, acpi_state_t *state, void *data)
{
	size_t return_size = 0;
	uint8_t *object = (uint8_t*)data;

	size_t integer_size;
	uint64_t integer;
	size_t name_size;
	acpi_handle_t *handle;
	char name[ACPI_MAX_NAME];
	acpi_object_t *destination_reg;
	acpi_object_t *sizeof_object;
	acpi_object_t n1, n2;

	// try register
	if(object[0] >= LOCAL0_OP && object[0] <= LOCAL7_OP)
	{
		switch(object[0])
		{
		case LOCAL0_OP:
			destination_reg = &state->local[0];
			break;
		case LOCAL1_OP:
			destination_reg = &state->local[1];
			break;
		case LOCAL2_OP:
			destination_reg = &state->local[2];
			break;
		case LOCAL3_OP:
			destination_reg = &state->local[3];
			break;
		case LOCAL4_OP:
			destination_reg = &state->local[4];
			break;
		case LOCAL5_OP:
			destination_reg = &state->local[5];
			break;
		case LOCAL6_OP:
			destination_reg = &state->local[6];
			break;
		case LOCAL7_OP:
			destination_reg = &state->local[7];
			break;
		}

		acpi_copy_object(destination, destination_reg);
		return 1;
	} else if(object[0] >= ARG0_OP && object[0] <= ARG6_OP)
	{
		switch(object[0])
		{
		case ARG0_OP:
			destination_reg = &state->arg[0];
			break;
		case ARG1_OP:
			destination_reg = &state->arg[1];
			break;
		case ARG2_OP:
			destination_reg = &state->arg[2];
			break;
		case ARG3_OP:
			destination_reg = &state->arg[3];
			break;
		case ARG4_OP:
			destination_reg = &state->arg[4];
			break;
		case ARG5_OP:
			destination_reg = &state->arg[5];
			break;
		case ARG6_OP:
			destination_reg = &state->arg[6];
			break;
		}

		acpi_copy_object(destination, destination_reg);
		return 1;
	}

	// try integer
	integer_size = acpi_eval_integer(object, &integer);
	if(integer_size != 0)
	{
		destination->type = ACPI_INTEGER;
		destination->integer = integer;
		return_size = integer_size;
	} else if(object[0] == STRINGPREFIX)
	{
		// try string
		destination->type = ACPI_STRING;
		destination->string = (char*)(&object[1]);
		return_size = acpi_strlen(destination->string);
		return_size += 2;		// skip STRINGPREFIX and null terminator
	} else if(object[0] == PACKAGE_OP)
	{
		// package
		destination->type = ACPI_PACKAGE;
		destination->package = acpi_calloc(sizeof(acpi_object_t), ACPI_MAX_PACKAGE_ENTRIES);
		destination->package_size = acpins_create_package(destination->package, object);
		acpi_parse_pkgsize(&object[1], &return_size);
		return_size++;		// skip PACKAGE_OP
	} else if(object[0] == BUFFER_OP)
	{
		// buffer
		return_size += acpi_exec_buffer(destination, state, object);
	} else if(acpi_is_name(object[0]))
	{
		// it's a NameSpec
		// resolve the name
		name_size = acpins_resolve_path(name, &object[0]);
		handle = acpi_exec_resolve(name);
		if(!handle)
		{
			acpi_panic("acpi: undefined reference %s\n", name);
		}

		// could be a named object
		if(handle->type == ACPI_NAMESPACE_NAME)
		{
			acpi_copy_object(destination, &handle->object);
			return_size += name_size;
		} else if(handle->type == ACPI_NAMESPACE_METHOD)
			// or a MethodInvokation
			return_size += acpi_methodinvoke(&object[0], state, destination);
		else if(handle->type == ACPI_NAMESPACE_FIELD || handle->type == ACPI_NAMESPACE_INDEXFIELD)
		{
			// or an Operation Region Field
			// This is what's interesting, because it lets AML do I/O
			// accesses via I/O ports and MMIO
			acpi_read_opregion(destination, handle);
			return_size += name_size;
		} else
		{
			acpi_panic("acpi: undefined behavior when path doesn't resolve into valid data or code.\n");
		}
	} else if(object[0] == SIZEOF_OP)
	{
		// try sizeof
		return_size++;
		object++;
		if(object[0] >= LOCAL0_OP && object[0] <= LOCAL7_OP)
		{
			return_size++;

			switch(object[0])
			{
			case LOCAL0_OP:
				sizeof_object = &state->local[0];
				break;
			case LOCAL1_OP:
				sizeof_object = &state->local[1];
				break;
			case LOCAL2_OP:
				sizeof_object = &state->local[2];
				break;
			case LOCAL3_OP:
				sizeof_object = &state->local[3];
				break;
			case LOCAL4_OP:
				sizeof_object = &state->local[4];
				break;
			case LOCAL5_OP:
				sizeof_object = &state->local[5];
				break;
			case LOCAL6_OP:
				sizeof_object = &state->local[6];
				break;
			case LOCAL7_OP:
				sizeof_object = &state->local[7];
				break;
			}
		} else if(object[0] >= ARG0_OP && object[0] <= ARG6_OP)
		{
			return_size++;

			switch(object[0])
			{
			case ARG0_OP:
				sizeof_object = &state->arg[0];
				break;
			case ARG1_OP:
				sizeof_object = &state->arg[1];
				break;
			case ARG2_OP:
				sizeof_object = &state->arg[2];
				break;
			case ARG3_OP:
				sizeof_object = &state->arg[3];
				break;
			case ARG4_OP:
				sizeof_object = &state->arg[4];
				break;
			case ARG5_OP:
				sizeof_object = &state->arg[5];
				break;
			case ARG6_OP:
				sizeof_object = &state->arg[6];
				break;
			}
		} else if(acpi_is_name(object[0]))
		{
			acpi_panic("TO-DO: Implement SizeOf for namespec\n");
			/*sizeof_object = acpi_malloc(sizeof(acpi_object_t));
			return_size += acpi_eval_object(sizeof_object, state, &object[0]);*/
		} else
		{
			acpi_panic("acpi: undefined object for SizeOf\n");
		}

		// now determine the actual size
		destination->type = ACPI_INTEGER;

		if(sizeof_object->type == ACPI_INTEGER)
			destination->integer = 8;	// treat all integers like qwords
		else if(sizeof_object->type == ACPI_STRING)
			destination->integer = acpi_strlen(sizeof_object->string);
		else if(sizeof_object->type == ACPI_PACKAGE)
			destination->integer = sizeof_object->package_size;
		else if(sizeof_object->type == ACPI_BUFFER)
			destination->integer = sizeof_object->buffer_size;

		else
		{
			acpi_panic("acpi: can't perform SizeOf on object type %d\n", sizeof_object->type);
		}
	} else if(object[0] == DEREF_OP)
	{
		// what a fucking waste of space DeRef is.
		return_size = acpi_eval_object(destination, state, &object[1]) + 1;
	} else if(object[0] == INDEX_OP)
	{
		return_size = 2;
		object++;

		size_t index_size;

		acpi_object_t ref, index;
		index_size = acpi_eval_object(&ref, state, &object[0]);
		return_size += index_size;
		object += index_size;

		index_size = acpi_eval_object(&index, state, &object[0]);
		return_size += index_size;

		if(ref.type == ACPI_STRING)
		{
			destination->type = ACPI_INTEGER;
			destination->integer = (uint64_t)ref.string[index.integer];
		} else if(ref.type == ACPI_BUFFER)
		{
			destination->type = ACPI_INTEGER;
			uint8_t *byte = (uint8_t*)ref.buffer;
			destination->integer = (uint64_t)byte[0];
		} else if(ref.type == ACPI_PACKAGE)
		{
			acpi_copy_object(destination, &ref.package[index.integer]);
		} else
		{
			acpi_panic("TO-DO: More Index() objects\n");
		}
	} else if(object[0] == LNOT_OP)
	{
		return_size = 1;
		object++;

		return_size += acpi_eval_object(&n1, state, &object[0]);
		destination->type = ACPI_INTEGER;
		if(n1.integer == 0)
			destination->integer = 1;
		else
			destination->integer = 0;
	} else if(object[0] == LAND_OP)
	{
		return_size = 1;
		object++;

		integer_size = acpi_eval_object(&n1, state, &object[0]);
		return_size += integer_size;
		object += integer_size;

		integer_size = acpi_eval_object(&n2, state, &object[0]);
		return_size += integer_size;

		destination->type = ACPI_INTEGER;

		if(n1.integer != 0 && n2.integer != 0)
			destination->integer = 1;
		else
			destination->integer = 0;
	} else if(object[0] == LEQUAL_OP)
	{
		return_size = 1;
		object++;

		integer_size = acpi_eval_object(&n1, state, &object[0]);
		return_size += integer_size;
		object += integer_size;

		integer_size = acpi_eval_object(&n2, state, &object[0]);
		return_size += integer_size;

		destination->type = ACPI_INTEGER;

		if(n1.integer == n2.integer)
			destination->integer = 1;
		else
			destination->integer = 0;
	} else if(object[0] == LGREATER_OP)
	{
		return_size = 1;
		object++;

		integer_size = acpi_eval_object(&n1, state, &object[0]);
		return_size += integer_size;
		object += integer_size;

		integer_size = acpi_eval_object(&n2, state, &object[0]);
		return_size += integer_size;

		destination->type = ACPI_INTEGER;
		if(n1.integer > n2.integer)
			destination->integer = 1;
		else
			destination->integer = 0;
	} else if(object[0] == LLESS_OP)
	{
		return_size = 1;
		object++;

		integer_size = acpi_eval_object(&n1, state, &object[0]);
		return_size += integer_size;
		object += integer_size;

		integer_size = acpi_eval_object(&n2, state, &object[0]);
		return_size += integer_size;

		destination->type = ACPI_INTEGER;
		if(n1.integer < n2.integer)
			destination->integer = 1;
		else
			destination->integer = 0;
	} else if(object[0] == LOR_OP)
	{
		return_size = 1;
		object++;

		integer_size = acpi_eval_object(&n1, state, &object[0]);
		return_size += integer_size;
		object += integer_size;

		integer_size = acpi_eval_object(&n2, state, &object[0]);
		return_size += integer_size;

		destination->type = ACPI_INTEGER;
		if(n1.integer == 0 || n2.integer == 0)
			destination->integer = 1;
		else
			destination->integer = 0;
	} else if(object[0] == AND_OP)
	{
		return_size = 2;
		object++;

		integer_size = acpi_eval_object(&n1, state, &object[0]);
		return_size += integer_size;
		object += integer_size;

		integer_size = acpi_eval_object(&n2, state, &object[0]);
		return_size += integer_size;

		destination->type = ACPI_INTEGER;
		destination->integer = n1.integer & n2.integer;
	} else if(object[0] == ADD_OP)
	{
		return_size = 2;
		object++;

		integer_size = acpi_eval_object(&n1, state, &object[0]);
		return_size += integer_size;
		object += integer_size;

		integer_size = acpi_eval_object(&n2, state, &object[0]);
		return_size += integer_size;

		destination->type = ACPI_INTEGER;
		destination->integer = n1.integer + n2.integer;
	} else if(object[0] == OR_OP)
	{
		return_size = 2;
		object++;

		integer_size = acpi_eval_object(&n1, state, &object[0]);
		return_size += integer_size;
		object += integer_size;

		integer_size = acpi_eval_object(&n2, state, &object[0]);
		return_size += integer_size;

		destination->type = ACPI_INTEGER;
		destination->integer = n1.integer | n2.integer;
	} else if(object[0] == SUBTRACT_OP)
	{
		return_size = 2;
		object++;

		integer_size = acpi_eval_object(&n1, state, &object[0]);
		return_size += integer_size;
		object += integer_size;

		integer_size = acpi_eval_object(&n2, state, &object[0]);
		return_size += integer_size;

		destination->type = ACPI_INTEGER;
		destination->integer = n1.integer - n2.integer;
	} else if(object[0] == NOT_OP)
	{
		return_size = 2;
		object++;

		integer_size = acpi_eval_object(&n1, state, &object[0]);
		return_size += integer_size;

		destination->type = ACPI_INTEGER;
		destination->integer = ~n1.integer;
	} else if(object[0] == XOR_OP)
	{
		return_size = 2;
		object++;

		integer_size = acpi_eval_object(&n1, state, &object[0]);
		return_size += integer_size;
		object += integer_size;

		integer_size = acpi_eval_object(&n2, state, &object[0]);
		return_size += integer_size;

		destination->type = ACPI_INTEGER;
		destination->integer = n1.integer ^ n2.integer;
	} else if(object[0] == SHL_OP)
	{
		return_size = 2;
		object++;

		integer_size = acpi_eval_object(&n1, state, &object[0]);
		return_size += integer_size;
		object += integer_size;

		integer_size = acpi_eval_object(&n2, state, &object[0]);
		return_size += integer_size;

		destination->type = ACPI_INTEGER;
		destination->integer = n1.integer << n2.integer;
	} else if(object[0] == SHR_OP)
	{
		return_size = 2;
		object++;

		integer_size = acpi_eval_object(&n1, state, &object[0]);
		return_size += integer_size;
		object += integer_size;

		integer_size = acpi_eval_object(&n2, state, &object[0]);
		return_size += integer_size;

		destination->type = ACPI_INTEGER;
		destination->integer = n1.integer >> n2.integer;
	} else if(object[0] == EXTOP_PREFIX && object[1] == CONDREF_OP)
	{
		return_size = 3;
		object += 2;

		name_size = acpins_resolve_path(name, &object[0]);
		return_size += name_size;

		acpi_handle_t *handle = acpi_exec_resolve(name);
		destination->type = ACPI_INTEGER;

		if(!handle)
			destination->integer = 0;
		else
			destination->integer = 1;
	} else if(object[0] == MULTIPLY_OP)
	{
		return_size = 2;
		object++;

		integer_size = acpi_eval_object(&n1, state, &object[0]);
		return_size += integer_size;
		object += integer_size;

		integer_size = acpi_eval_object(&n2, state, &object[0]);
		return_size += integer_size;

		destination->type = ACPI_INTEGER;
		destination->integer = n1.integer * n2.integer;
	} else if(object[0] == DIVIDE_OP)
	{
		return_size = 3;
		object++;

		integer_size = acpi_eval_object(&n1, state, &object[0]);
		return_size += integer_size;
		object += integer_size;

		integer_size = acpi_eval_object(&n2, state, &object[0]);
		return_size += integer_size;
		object += integer_size;

		acpi_object_t mod, quo;
		mod.type = ACPI_INTEGER;
		quo.type = ACPI_INTEGER;

		mod.integer = n1.integer % n2.integer;
		quo.integer = n1.integer / n2.integer;

		integer_size = acpi_write_object(&object[0], &mod, state);
		return_size += integer_size;
		object += integer_size;

		integer_size = acpi_write_object(&object[0], &quo, state);
		return_size += integer_size;

		destination->type = ACPI_INTEGER;
		destination->integer = n1.integer / n2.integer;
	} else
	{
		acpi_panic("acpi: undefined opcode, sequence: %xb %xb %xb %xb\n", object[0], object[1], object[2], object[3]);
	}

	return return_size;
}

// acpi_eval(): Returns an object
// Param:	acpi_object_t *destination - where to store object
// Param:	char *path - path of object
// Return:	int - 0 on success

int acpi_eval(acpi_object_t *destination, char *path)
{
	acpi_handle_t *handle;
	handle = acpi_exec_resolve(path);
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
		acpi_memset(&state, 0, sizeof(acpi_state_t));
		acpi_strcpy(state.name, path);
		return acpi_exec_method(&state, destination);
	}

	return 1;
}

// acpi_bswap16(): Switches endianness of a WORD
// Param:	uint16_t word - WORD
// Return:	uint16_t - switched value

uint16_t acpi_bswap16(uint16_t word)
{
	return (uint16_t)((word >> 8) & 0xFF) | ((word << 8) & 0xFF00);
}

// acpi_bswap32(): Switches endianness of a DWORD
// Param:	uint32_t dword - DWORD
// Return:	uint32 - switched value

uint32_t acpi_bswap32(uint32_t dword)
{
	return (uint32_t)((dword>>24) & 0xFF) | ((dword<<8) & 0xFF0000) | ((dword>>8)&0xFF00) | ((dword<<24)&0xFF000000);
}

// acpi_char_to_hex(): Converts an ASCII hex character to a hex value
// Param:	char character - ASCII hex char
// Return:	uint8_t - hex value

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
// Param:	acpi_object_t *object - destination
// Param:	char *id - ACPI PNP ID
// Return:	Nothing

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




