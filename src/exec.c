
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018 by Omar Mohammad
 */

#include "lai.h"

/* ACPI Control Method Execution */
/* Type1Opcode := DefBreak | DefBreakPoint | DefContinue | DefFatal | DefIfElse |
   DefLoad | DefNoop | DefNotify | DefRelease | DefReset | DefReturn |
   DefSignal | DefSleep | DefStall | DefUnload | DefWhile */

int acpi_exec(uint8_t *, size_t, acpi_state_t *, acpi_object_t *);

char acpi_emulated_os[] = "Windows 2015";		// Windows 10
uint64_t acpi_implemented_version = 2;			// ACPI 2.0

// acpi_exec_method(): Finds and executes a control method
// Param:	acpi_state_t *state - method name and arguments
// Param:	acpi_object_t *method_return - return value of method
// Return:	int - 0 on success

int acpi_exec_method(acpi_state_t *state, acpi_object_t *method_return)
{
	acpi_handle_t *method;

	uint32_t osi_return = 0;

	// When executing the _OSI() method, we'll have one parameter which contains
	// the name of an OS. We have to pretend to be a modern version of Windows,
	// for AML to let us use its features.
	if(acpi_strcmp(state->name, "\\._OSI") == 0)
	{
		if(acpi_strcmp(state->arg[0].string, "Windows 2006") == 0)		// Windows Vista
			osi_return = 0xFFFFFFFF;
		else if(acpi_strcmp(state->arg[0].string, "Windows 2009") == 0)		// Windows 7
			osi_return = 0xFFFFFFFF;
		else if(acpi_strcmp(state->arg[0].string, "Windows 2012") == 0)		// Windows 8
			osi_return = 0xFFFFFFFF;
		else if(acpi_strcmp(state->arg[0].string, "Windows 2013") == 0)		// Windows 8.1
			osi_return = 0xFFFFFFFF;
		else if(acpi_strcmp(state->arg[0].string, "Windows 2015") == 0)		// Windows 10
			osi_return = 0xFFFFFFFF;

		else
			osi_return = 0x00000000;	// unsupported OS

		method_return->type = ACPI_INTEGER;
		method_return->integer = osi_return;

		acpi_printf("acpi: _OSI('%s') returned 0x%xd\n", state->arg[0].string, osi_return);
		return 0;
	}

	// We'll tell the AML code we are Windows 10
	if(acpi_strcmp(state->name, "\\._OS_") == 0)
	{
		method_return->type = ACPI_STRING;
		method_return->string = acpi_malloc(acpi_strlen(acpi_emulated_os));
		acpi_strcpy(method_return->string, acpi_emulated_os);

		acpi_printf("acpi: _OS_ returned '%s'\n", method_return->string);
		return 0;
	}

	// All versions of Windows starting from Windows Vista claim to implement
	// at least ACPI 2.0. Therefore we also need to do the same.
	if(acpi_strcmp(state->name, "\\._REV") == 0)
	{
		method_return->type = ACPI_INTEGER;
		method_return->integer = acpi_implemented_version;

		acpi_printf("acpi: _REV returned %d\n", method_return->integer);
		return 0;
	}

	// Okay, by here it's a real method
	method = acpins_resolve(state->name);
	if(!method)
		return -1;

	//acpi_printf("acpi: execute control method %s\n", state->name);

	int status = acpi_exec(method->pointer, method->size, state, method_return);

	/*acpi_printf("acpi: %s finished, ", state->name);

	if(method_return->type == ACPI_INTEGER)
		acpi_printf("return value is integer: %d\n", method_return->integer);
	else if(method_return->type == ACPI_STRING)
		acpi_printf("return value is string: '%s'\n", method_return->string);
	else if(method_return->type == ACPI_PACKAGE)
		acpi_printf("return value is package\n");
	else if(method_return->type == ACPI_BUFFER)
		acpi_printf("return value is buffer\n");*/

	return status;
}

// acpi_exec(): Internal function, executes actual AML opcodes
// Param:	uint8_t *method - pointer to method opcodes
// Param:	size_t size - size of method of bytes
// Param:	acpi_state_t *state - machine state
// Param:	acpi_object_t *method_return - return value of method
// Return:	int - 0 on success

int acpi_exec(uint8_t *method, size_t size, acpi_state_t *state, acpi_object_t *method_return)
{
	if(!size)
	{
		method_return->type = ACPI_INTEGER;
		method_return->integer = 0;
		return 0;
	}

	acpi_strcpy(acpins_path, state->name);

	size_t i = 0;
	acpi_object_t invoke_return;
	state->status = 0;
	size_t pkgsize, condition_size;

	while(i <= size)
	{
		/* While() loops */
		if((state->status & ACPI_STATUS_WHILE) == 0 && i >= size)
			break;

		if((state->status & ACPI_STATUS_WHILE) != 0 && i >= state->loop_end)
			i = state->loop_start;

		/* If/Else Conditional */
		if(!state->condition_level)
			state->status &= ~ACPI_STATUS_CONDITIONAL;

		if((state->status & ACPI_STATUS_CONDITIONAL) != 0 && i >= state->condition[state->condition_level].end)
		{
			if(method[i] == ELSE_OP)
			{
				i++;
				pkgsize = acpi_parse_pkgsize(&method[i], &condition_size);
				i += condition_size;
			}

			state->condition_level--;
			continue;
		}

		/* Method Invokation? */
		if(acpi_is_name(method[i]))
			i += acpi_methodinvoke(&method[i], state, &invoke_return);

		if(i >= size)
			goto return_zero;

		if(acpi_is_name(method[i]))
			continue;

		/* General opcodes */
		switch(method[i])
		{
		case ZERO_OP:
		case ONE_OP:
		case ONES_OP:
		case NOP_OP:
			i++;
			break;

		case EXTOP_PREFIX:
			switch(method[i+1])
			{
			case SLEEP_OP:
				i += acpi_exec_sleep(&method[i], state);
				break;
			default:
				acpi_panic("acpi: undefined opcode in control method %s, sequence %xb %xb %xb %xb\n", state->name, method[i], method[i+1], method[i+2], method[i+3]);
			}
			break;

		/* A control method can return literally any object */
		/* So we need to take this into consideration */
		case RETURN_OP:
			i++;
			acpi_eval_object(method_return, state, &method[i]);
			return 0;

		/* While Loops */
		case WHILE_OP:
			state->loop_start = i;
			i++;
			state->loop_end = i;
			state->status |= ACPI_STATUS_WHILE;
			i += acpi_parse_pkgsize(&method[i], &state->loop_pkgsize);
			state->loop_end += state->loop_pkgsize;

			// evaluate the predicate
			state->loop_predicate_size = acpi_eval_object(&state->loop_predicate, state, &method[i]);
			if(state->loop_predicate.integer == 0)
			{
				state->status &= ~ACPI_STATUS_WHILE;
				i = state->loop_end;
			} else
			{
				i += state->loop_predicate_size;
			}

			break;

		/* Continue Looping */
		case CONTINUE_OP:
			i = state->loop_start;
			break;

		/* Break Loop */
		case BREAK_OP:
			i = state->loop_end;
			state->status &= ~ACPI_STATUS_WHILE;
			break;

		/* If/Else Conditional */
		case IF_OP:
			i++;
			state->condition_level++;
			state->condition[state->condition_level].end = i;
			i += acpi_parse_pkgsize(&method[i], &state->condition[state->condition_level].pkgsize);
			state->condition[state->condition_level].end += state->condition[state->condition_level].pkgsize;

			// evaluate the predicate
			state->condition[state->condition_level].predicate_size = acpi_eval_object(&state->condition[state->condition_level].predicate, state, &method[i]);
			if(state->condition[state->condition_level].predicate.integer == 0)
			{
				i = state->condition[state->condition_level].end;
				state->condition_level--;
			} else
			{
				state->status |= ACPI_STATUS_CONDITIONAL;
				i += state->condition[state->condition_level].predicate_size;
			}

			break;

		case ELSE_OP:
			i++;
			pkgsize = acpi_parse_pkgsize(&method[i], &condition_size);
			i += pkgsize;
			break;

		/* Most of the type 2 opcodes are implemented in exec2.c */
		case NAME_OP:
			i += acpi_exec_name(&method[i], state);
			break;
		case BYTEFIELD_OP:
			i += acpi_exec_bytefield(&method[i], state);
			break;
		case WORDFIELD_OP:
			i += acpi_exec_wordfield(&method[i], state);
			break;
		case DWORDFIELD_OP:
			i += acpi_exec_dwordfield(&method[i], state);
			break;
		case STORE_OP:
			i += acpi_exec_store(&method[i], state);
			break;
		case ADD_OP:
			i += acpi_exec_add(&method[i], state);
			break;
		case SUBTRACT_OP:
			i += acpi_exec_subtract(&method[i], state);
			break;
		case INCREMENT_OP:
			i += acpi_exec_increment(&method[i], state);
			break;
		case DECREMENT_OP:
			i += acpi_exec_decrement(&method[i], state);
			break;
		case AND_OP:
			i += acpi_exec_and(&method[i], state);
			break;
		case OR_OP:
			i += acpi_exec_or(&method[i], state);
			break;
		case NOT_OP:
			i += acpi_exec_not(&method[i], state);
			break;
		case XOR_OP:
			i += acpi_exec_xor(&method[i], state);
			break;
		case SHR_OP:
			i += acpi_exec_shr(&method[i], state);
			break;
		case SHL_OP:
			i += acpi_exec_shl(&method[i], state);
			break;
		case MULTIPLY_OP:
			i += acpi_exec_multiply(&method[i], state);
			break;
		case DIVIDE_OP:
			i += acpi_exec_divide(&method[i], state);
			break;

		default:
			acpi_panic("acpi: undefined opcode in control method %s, sequence %xb %xb %xb %xb\n", state->name, method[i], method[i+1], method[i+2], method[i+3]);
		}
	}

return_zero:
	// when it returns nothing, assume Return (0)
	method_return->type = ACPI_INTEGER;
	method_return->integer = 0;
	return 0;
}

// acpi_methodinvoke(): Executes a MethodInvokation
// Param:	void *data - pointer to MethodInvokation
// Param:	acpi_state_t *old_state - state of currently executing method
// Param:	acpi_object_t *method_return - object to store return value
// Return:	size_t - size in bytes for skipping

size_t acpi_methodinvoke(void *data, acpi_state_t *old_state, acpi_object_t *method_return)
{
	uint8_t *methodinvokation = (uint8_t*)data;

	// save the state of the currently executing method
	char path_save[ACPI_MAX_NAME];
	acpi_strcpy(path_save, acpins_path);

	size_t return_size = 0;

	// determine the name of the method
	acpi_state_t *state = acpi_malloc(sizeof(acpi_state_t));
	size_t name_size = acpins_resolve_path(state->name, methodinvokation);
	return_size += name_size;
	methodinvokation += name_size;

	acpi_handle_t *method;
	method = acpi_exec_resolve(state->name);
	if(!method)
	{
		acpi_panic("acpi: undefined MethodInvokation %s\n", state->name);
	}

	uint8_t argc = method->method_flags & METHOD_ARGC_MASK;
	uint8_t current_argc = 0;
	size_t arg_size;
	if(argc != 0)
	{
		// parse method arguments here
		while(current_argc < argc)
		{
			arg_size = acpi_eval_object(&state->arg[current_argc], old_state, methodinvokation);
			methodinvokation += arg_size;
			return_size += arg_size;

			current_argc++;
		}
	}

	// execute
	acpi_exec_method(state, method_return);

	// restore state
	acpi_strcpy(acpins_path, path_save);
	return return_size;
}

// acpi_exec_sleep(): Executes a Sleep() opcode
// Param:	void *data - opcode data
// Param:	acpi_state_t *state - AML VM state
// Return:	size_t - size in bytes for skipping

size_t acpi_exec_sleep(void *data, acpi_state_t *state)
{
	size_t return_size = 2;
	uint8_t *opcode = (uint8_t*)data;
	opcode += 2;		// skip EXTOP_PREFIX and SLEEP_OP

	acpi_object_t time;
	return_size += acpi_eval_object(&time, state, &opcode[0]);

	if(time.integer == 0)
		time.integer = 1;

	acpi_sleep(time.integer);

	return return_size;
}




