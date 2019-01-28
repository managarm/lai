
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018 by Omar Mohammad
 */

/* ACPI Namespace Management During Control Method Execution */

#include "lai.h"

// acpi_exec_name(): Creates a Name() object in a Method's private namespace
// Param:	void *data - data
// Param:	acpi_state_t *state - AML VM state
// Return:	size_t - size in bytes for skipping

size_t acpi_exec_name(void *data, acpi_state_t *state)
{
	size_t return_size = 1;
	uint8_t *name = (uint8_t*)data;
	name++;			// skip over NAME_OP

	char path[ACPI_MAX_NAME];
	size_t size = acpins_resolve_path(path, name);

	acpi_handle_t *handle;
	handle = acpins_resolve(path);
	if(!handle)
	{
		// create it if it doesn't already exist
		acpi_namespace[acpi_namespace_entries].type = ACPI_NAMESPACE_NAME;
		acpi_strcpy(acpi_namespace[acpi_namespace_entries].path, path);
		handle = &acpi_namespace[acpi_namespace_entries];

		acpins_increment_namespace();
	}

	return_size += size;
	name += size;

	size = acpi_eval_object(&handle->object, state, name);
	return_size += size;

	return return_size;
}

// acpi_exec_buffer(): Creates a buffer object
// Param:	acpi_object_t *destination - destination
// Param:	acpi_state_t *state - AML VM state
// Param:	void *data - actual data
// Return:	size_t - size in bytes for skipping

size_t acpi_exec_buffer(acpi_object_t *destination, acpi_state_t *state, void *data)
{
	size_t return_size = 1;

	uint8_t *buffer = (uint8_t*)data;
	buffer++;		// skip BUFFER_OP

	size_t pkgsize, size;
	pkgsize = acpi_parse_pkgsize(buffer, &size);
	return_size += size;

	acpi_object_t buffer_size;
	buffer += pkgsize;
	buffer += acpi_eval_object(&buffer_size, state, buffer);

	destination->type = ACPI_BUFFER;
	destination->buffer_size = buffer_size.integer;
	destination->buffer = acpi_malloc(destination->buffer_size);

	size -= ((size_t)buffer - (size_t)data);

	if(size)
		acpi_memcpy(destination->buffer, buffer, destination->buffer_size);

	return return_size;
}

// acpi_exec_bytefield(): Creates a ByteField object
// Param:	void *data - data
// Param:	acpi_state_t *state - AML VM state
// Return:	size_t - size for skipping

size_t acpi_exec_bytefield(void *data, acpi_state_t *state)
{
	return acpins_create_bytefield(data);	// dirty af solution but good enough for now
}


// acpi_exec_wordfield(): Creates a WordField object
// Param:	void *data - data
// Param:	acpi_state_t *state - AML VM state
// Return:	size_t - size for skipping

size_t acpi_exec_wordfield(void *data, acpi_state_t *state)
{
	return acpins_create_wordfield(data);	// dirty af solution but good enough for now
}


// acpi_exec_dwordfield(): Creates a DwordField object
// Param:	void *data - data
// Param:	acpi_state_t *state - AML VM state
// Return:	size_t - size for skipping

size_t acpi_exec_dwordfield(void *data, acpi_state_t *state)
{
	return acpins_create_dwordfield(data);	// dirty af solution but good enough for now
}






