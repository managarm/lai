
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 by Omar Muhamed
 */

#include "lai.h"

/* ACPI Control Method Execution */
/* Type1Opcode := DefBreak | DefBreakPoint | DefContinue | DefFatal | DefIfElse |
   DefLoad | DefNoop | DefNotify | DefRelease | DefReset | DefReturn |
   DefSignal | DefSleep | DefStall | DefUnload | DefWhile */

int acpi_exec(uint8_t *, size_t, acpi_state_t *, acpi_object_t *);

const char *acpi_emulated_os = "Microsoft Windows NT";        // OS family
uint64_t acpi_implemented_version = 2;                // ACPI 2.0

const char *supported_osi_strings[] = 
{
    "Windows 2000",        /* Windows 2000 */
    "Windows 2001",        /* Windows XP */
    "Windows 2001 SP1",    /* Windows XP SP1 */
    "Windows 2001.1",    /* Windows Server 2003 */
    "Windows 2006",        /* Windows Vista */
    "Windows 2006.1",    /* Windows Server 2008 */
    "Windows 2006 SP1",    /* Windows Vista SP1 */
    "Windows 2006 SP2",    /* Windows Vista SP2 */
    "Windows 2009",        /* Windows 7 */
    "Windows 2012",        /* Windows 8 */
    "Windows 2013",        /* Windows 8.1 */
    "Windows 2015",        /* Windows 10 */
};

// Prepare the interpreter state for a control method call.
// Param: acpi_state_t *state - will store method name and arguments
// Param: acpi_nsnode_t *method - identifies the control method

void acpi_init_call_state(acpi_state_t *state, acpi_nsnode_t *method) {
    acpi_memset(state, 0, sizeof(acpi_state_t));
    state->handle = method;
}

// Finalize the interpreter state. Frees all memory owned by the state.

void acpi_finalize_state(acpi_state_t *state) {
    acpi_free_object(&state->retvalue);
    for(int i = 0; i < 7; i++)
    {
        acpi_free_object(&state->arg[i]);
    }

    for(int i = 0; i < 8; i++)
    {
        acpi_free_object(&state->local[i]);
    }
}

// acpi_exec_method(): Finds and executes a control method
// Param:    acpi_state_t *state - method name and arguments
// Param:    acpi_object_t *method_return - return value of method
// Return:    int - 0 on success

int acpi_exec_method(acpi_state_t *state)
{
    acpi_memset(state->local, 0, sizeof(acpi_object_t) * 8);

    // When executing the _OSI() method, we'll have one parameter which contains
    // the name of an OS. We have to pretend to be a modern version of Windows,
    // for AML to let us use its features.
    if(!acpi_strcmp(state->handle->path, "\\._OSI"))
    {
        uint32_t osi_return = 0;
        for(int i = 0; i < (sizeof(supported_osi_strings) / sizeof(uintptr_t)); i++)
        {
            if(!acpi_strcmp(state->arg[0].string, supported_osi_strings[i]))
            {
                osi_return = 0xFFFFFFFF;
                break;
            }
        }

        if(!osi_return && !acpi_strcmp(state->arg[0].string, "Linux"))
            acpi_warn("buggy BIOS requested _OSI('Linux'), ignoring...\n");

        state->retvalue.type = ACPI_INTEGER;
        state->retvalue.integer = osi_return;

        acpi_debug("_OSI('%s') returned 0x%08X\n", state->arg[0].string, osi_return);
        return 0;
    }

    // OS family -- pretend to be Windows
    if(!acpi_strcmp(state->handle->path, "\\._OS_"))
    {
        state->retvalue.type = ACPI_STRING;
        state->retvalue.string = acpi_malloc(acpi_strlen(acpi_emulated_os));
        acpi_strcpy(state->retvalue.string, acpi_emulated_os);

        acpi_debug("_OS_ returned '%s'\n", state->retvalue.string);
        return 0;
    }

    // All versions of Windows starting from Windows Vista claim to implement
    // at least ACPI 2.0. Therefore we also need to do the same.
    if(!acpi_strcmp(state->handle->path, "\\._REV"))
    {
        state->retvalue.type = ACPI_INTEGER;
        state->retvalue.integer = acpi_implemented_version;

        acpi_debug("_REV returned %d\n", state->retvalue.integer);
        return 0;
    }

    // Okay, by here it's a real method
    //acpi_debug("execute control method %s\n", state->handle->path);
    int status = acpi_exec(state->handle->pointer, state->handle->size, state, &state->retvalue);

    /*acpi_debug("%s finished, ", state->handle->path);

    if(state->retvalue.type == ACPI_INTEGER)
        acpi_debug("return value is integer: %d\n", state->retvalue.integer);
    else if(state->retvalue.type == ACPI_STRING)
        acpi_debug("return value is string: '%s'\n", state->retvalue.string);
    else if(state->retvalue.type == ACPI_PACKAGE)
        acpi_debug("return value is package\n");
    else if(state->retvalue.type == ACPI_BUFFER)
        acpi_debug("return value is buffer\n");*/

    return status;
}

// Pushes a new item to the opstack and returns it.
static acpi_object_t *acpi_exec_push_opstack_or_die(acpi_state_t *state) {
    if(state->opstack_ptr == 16)
        acpi_panic("operand stack overflow\n");
    acpi_object_t *object = &state->opstack[state->opstack_ptr];
    acpi_memset(object, 0, sizeof(acpi_object_t));
    state->opstack_ptr++;
    return object;
}

// Returns the n-th item from the opstack.
static acpi_object_t *acpi_exec_get_opstack(acpi_state_t *state, int n) {
    if(n >= state->opstack_ptr)
        acpi_panic("opstack access out of bounds"); // This is an internal execution error.
    return &state->opstack[n];
}

// Removes n items from the opstack.
static void acpi_exec_pop_opstack(acpi_state_t *state, int n) {
    for(int k = 0; k < n; k++)
        acpi_free_object(&state->opstack[state->opstack_ptr - k - 1]);
    state->opstack_ptr -= n;
}

// Pushes a new item to the execution stack and returns it.
static acpi_stackitem_t *acpi_exec_push_stack_or_die(acpi_state_t *state) {
    state->stack_ptr++;
    if(state->stack_ptr == 16)
        acpi_panic("execution engine stack overflow\n");
    return &state->stack[state->stack_ptr];
}

// Returns the n-th item from the top of the stack.
static acpi_stackitem_t *acpi_exec_peek_stack(acpi_state_t *state, int n) {
    if(state->stack_ptr - n < 0)
        return NULL;
    return &state->stack[state->stack_ptr - n];
}

// Returns the last item of the stack.
static acpi_stackitem_t *acpi_exec_peek_stack_back(acpi_state_t *state) {
    return acpi_exec_peek_stack(state, 0);
}

// Removes n items from the stack.
static void acpi_exec_pop_stack(acpi_state_t *state, int n) {
    state->stack_ptr -= n;
}

// Removes the last item from the stack.
static void acpi_exec_pop_stack_back(acpi_state_t *state) {
    acpi_exec_pop_stack(state, 1);
}

static void acpi_exec_reduce(int opcode, acpi_object_t *operands, acpi_object_t *result) {
    //acpi_debug("acpi_exec_reduce: opcode 0x%02X\n", opcode);
    switch(opcode) {
    case STORE_OP:
        acpi_move_object(result, operands);
        break;
    case NOT_OP:
        result->type = ACPI_INTEGER;
        result->integer = ~operands[0].integer;
        break;
    case ADD_OP:
        result->type = ACPI_INTEGER;
        result->integer = operands[0].integer + operands[1].integer;
        break;
    case SUBTRACT_OP:
        result->type = ACPI_INTEGER;
        result->integer = operands[0].integer - operands[1].integer;
        break;
    case MULTIPLY_OP:
        result->type = ACPI_INTEGER;
        result->integer = operands[0].integer * operands[1].integer;
        break;
    case AND_OP:
        result->type = ACPI_INTEGER;
        result->integer = operands[0].integer & operands[1].integer;
        break;
    case OR_OP:
        result->type = ACPI_INTEGER;
        result->integer = operands[0].integer | operands[1].integer;
        break;
    case XOR_OP:
        result->type = ACPI_INTEGER;
        result->integer = operands[0].integer ^ operands[1].integer;
        break;
    case SHL_OP:
        result->type = ACPI_INTEGER;
        result->integer = operands[0].integer << operands[1].integer;
        break;
    case SHR_OP:
        result->type = ACPI_INTEGER;
        result->integer = operands[0].integer >> operands[1].integer;
        break;
    default:
        acpi_panic("undefined opcode in acpi_exec_reduce: %02X\n", opcode);
    }
}

// acpi_exec(): Internal function, executes actual AML opcodes
// Param:    uint8_t *method - pointer to method opcodes
// Param:    size_t size - size of method of bytes
// Param:    acpi_state_t *state - machine state
// Param:    acpi_object_t *method_return - return value of method
// Return:    int - 0 on success

int acpi_exec(uint8_t *method, size_t size, acpi_state_t *state, acpi_object_t *method_return)
{
    if(!size)
    {
        method_return->type = ACPI_INTEGER;
        method_return->integer = 0;
        return 0;
    }

    size_t i = 0;
    acpi_object_t invoke_return = {0};
    state->stack_ptr = -1;

    while(i <= size)
    {
        acpi_stackitem_t *item = acpi_exec_peek_stack_back(state);
        if(item)
        {
            if(item->kind == LAI_OP_STACKITEM)
            {
                if(state->opstack_ptr < item->op_opstack + item->op_num_operands) {
                    acpi_object_t *operand = acpi_exec_push_opstack_or_die(state);
                    i += acpi_eval_object(operand, state, method + i);
                    continue;
                }else
                {
                    acpi_object_t result = {0};
                    acpi_object_t *operands = acpi_exec_get_opstack(state, item->op_opstack);
                    acpi_exec_reduce(item->op_opcode, operands, &result);
                    i += acpi_write_object(method + i, &result, state);
                    acpi_exec_pop_opstack(state, item->op_num_operands);
                    acpi_exec_pop_stack_back(state);
                    continue;
                }
            }else if(item->kind == LAI_LOOP_STACKITEM)
            {
                if(i == item->loop_pred)
                {
                    // We are at the beginning of a loop. We check the predicate; if it is false,
                    // we jump to the end of the loop and remove the stack item.
                    acpi_object_t predicate = {0};
                    i += acpi_eval_object(&predicate, state, method + i);
                    if(!predicate.integer)
                    {
                        i = item->loop_end;
                        acpi_exec_pop_stack_back(state);
                    }
                    continue;
                }else if(i == item->loop_end)
                {
                    // Unconditionally jump to the loop's predicate.
                    i = item->loop_pred;
                    continue;
                }

                if (i > item->loop_end) // This would be an interpreter bug.
                    acpi_panic("execution escaped out of While() body\n");
            }else if(item->kind == LAI_COND_STACKITEM)
            {
                // If the condition wasn't taken, execute the Else() block if it exists
                if(!item->cond_taken)
                {
                    if(method[i] == ELSE_OP)
                    {
                        size_t else_size;
                        i++;
                        i += acpi_parse_pkgsize(method + i, &else_size);
                    }

                    acpi_exec_pop_stack_back(state);
                    continue;
                }

                // Clean up the execution stack at the end of If().
                if(i == item->cond_end)
                {
                    // Consume a follow-up Else() opcode.
                    if(i < size && method[i] == ELSE_OP) {
                        size_t else_size;
                        i++;
                        size_t j = i;
                        i += acpi_parse_pkgsize(method + i, &else_size);

                        i = j + else_size;
                    }

                    acpi_exec_pop_stack_back(state);
                    continue;
                }
            }else
                acpi_panic("unexpected acpi_stackitem_t\n");
        }

        if(i == size)
            break;
        if(i > size) // This would be an interpreter bug.
            acpi_panic("execution escaped out of method body\n");

        /* Method Invokation? */
        if(acpi_is_name(method[i]))
            i += acpi_methodinvoke(&method[i], state, &invoke_return);

        if(i >= size)
            goto return_zero;

        if(acpi_is_name(method[i]))
            continue;

        /* General opcodes */
        int opcode;
        if(method[i] == EXTOP_PREFIX)
        {
            if(i + 1 == size)
                acpi_panic("two-byte opcode on method boundary");
            opcode = (EXTOP_PREFIX << 8) | method[i + 1];
        }else
            opcode = method[i];

        switch(opcode)
        {
        case ZERO_OP:
        case ONE_OP:
        case ONES_OP:
        case NOP_OP:
            i++;
            break;
            {
            }
        case (EXTOP_PREFIX << 8) | SLEEP_OP:
            i += acpi_exec_sleep(&method[i], state);
            break;

        /* A control method can return literally any object */
        /* So we need to take this into consideration */
        case RETURN_OP:
            i++;
            acpi_eval_object(method_return, state, &method[i]);
            return 0;

        /* While Loops */
        case WHILE_OP:
        {
            size_t loop_size;
            i++;
            size_t j = i;
            i += acpi_parse_pkgsize(&method[i], &loop_size);

            acpi_stackitem_t *loop_item = acpi_exec_push_stack_or_die(state);
            loop_item->kind = LAI_LOOP_STACKITEM;
            loop_item->loop_pred = i;
            loop_item->loop_end = j + loop_size;
            break;
        }
        /* Continue Looping */
        case CONTINUE_OP:
        {
            // Find the last LAI_LOOP_STACKITEM on the stack.
            int j = 0;
            acpi_stackitem_t *loop_item;
            while(1) {
                loop_item = acpi_exec_peek_stack(state, j);
                if(!loop_item)
                    acpi_panic("Continue() outside of While()\n");
                if(loop_item->kind == LAI_LOOP_STACKITEM)
                    break;
                // TODO: Verify that we only cross conditions/loops.
                j++;
            }

            // Keep the loop item but remove nested items from the exeuction stack.
            i = loop_item->loop_pred;
            acpi_exec_pop_stack(state, j);
            break;
        }
        /* Break Loop */
        case BREAK_OP:
        {
            // Find the last LAI_LOOP_STACKITEM on the stack.
            int j = 0;
            acpi_stackitem_t *loop_item;
            while(1) {
                loop_item = acpi_exec_peek_stack(state, j);
                if(!loop_item)
                    acpi_panic("Break() outside of While()\n");
                if(loop_item->kind == LAI_LOOP_STACKITEM)
                    break;
                // TODO: Verify that we only cross conditions/loops.
                j++;
            }

            // Remove the loop item from the execution stack.
            i = loop_item->loop_end;
            acpi_exec_pop_stack(state, j + 1);
            break;
        }
        /* If/Else Conditional */
        case IF_OP:
        {
            size_t if_size;
            i++;
            size_t j = i;
            i += acpi_parse_pkgsize(method + i, &if_size);

            // Evaluate the predicate
            acpi_object_t predicate = {0};
            i += acpi_eval_object(&predicate, state, method + i);

            acpi_stackitem_t *cond_item = acpi_exec_push_stack_or_die(state);
            cond_item->kind = LAI_COND_STACKITEM;
            cond_item->cond_taken = predicate.integer;
            cond_item->cond_end = j + if_size;

            if(!cond_item->cond_taken)
                i = cond_item->cond_end;

            break;
        }
        case ELSE_OP:
            acpi_panic("Else() outside of If()\n");
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
        case NOT_OP:
        {
            acpi_stackitem_t *op_item = acpi_exec_push_stack_or_die(state);
            op_item->kind = LAI_OP_STACKITEM;
            op_item->op_opcode = method[i];
            op_item->op_opstack = state->opstack_ptr;
            op_item->op_num_operands = 1;
            i++;
            break;
        }
        case ADD_OP:
        case SUBTRACT_OP:
        case MULTIPLY_OP:
        case AND_OP:
        case OR_OP:
        case XOR_OP:
        case SHR_OP:
        case SHL_OP:
        {
            acpi_stackitem_t *op_item = acpi_exec_push_stack_or_die(state);
            op_item->kind = LAI_OP_STACKITEM;
            op_item->op_opcode = method[i];
            op_item->op_opstack = state->opstack_ptr;
            op_item->op_num_operands = 2;
            i++;
            break;
        }
        case INCREMENT_OP:
            i += acpi_exec_increment(&method[i], state);
            break;
        case DECREMENT_OP:
            i += acpi_exec_decrement(&method[i], state);
            break;
        case DIVIDE_OP:
            i += acpi_exec_divide(&method[i], state);
            break;

        default:
            acpi_panic("undefined opcode in control method %s, sequence %02X %02X %02X %02X\n",
                    state->handle->path, method[i], method[i+1], method[i+2], method[i+3]);
        }
    }

return_zero:
    // when it returns nothing, assume Return (0)
    method_return->type = ACPI_INTEGER;
    method_return->integer = 0;
    return 0;
}

// acpi_methodinvoke(): Executes a MethodInvokation
// Param:    void *data - pointer to MethodInvokation
// Param:    acpi_state_t *old_state - state of currently executing method
// Param:    acpi_object_t *method_return - object to store return value
// Return:    size_t - size in bytes for skipping

size_t acpi_methodinvoke(void *data, acpi_state_t *old_state, acpi_object_t *method_return)
{
    uint8_t *methodinvokation = (uint8_t*)data;

    size_t return_size = 0;

    // determine the name of the method
    char path[ACPI_MAX_NAME];
    size_t name_size = acpins_resolve_path(old_state->handle, path, methodinvokation);
    return_size += name_size;
    methodinvokation += name_size;

    acpi_nsnode_t *method = acpi_exec_resolve(path);
    if(!method)
        acpi_panic("undefined MethodInvokation %s\n", path);

    acpi_state_t state;
    acpi_init_call_state(&state, method);
    uint8_t argc = method->method_flags & METHOD_ARGC_MASK;
    uint8_t current_argc = 0;
    size_t arg_size;
    if(argc != 0)
    {
        // parse method arguments here
        while(current_argc < argc)
        {
            arg_size = acpi_eval_object(&state.arg[current_argc], old_state, methodinvokation);
            methodinvokation += arg_size;
            return_size += arg_size;

            current_argc++;
        }
    }

    // execute
    acpi_exec_method(&state);
    acpi_move_object(method_return, &state.retvalue);
    acpi_finalize_state(&state);

    return return_size;
}

// acpi_exec_sleep(): Executes a Sleep() opcode
// Param:    void *data - opcode data
// Param:    acpi_state_t *state - AML VM state
// Return:    size_t - size in bytes for skipping

size_t acpi_exec_sleep(void *data, acpi_state_t *state)
{
    size_t return_size = 2;
    uint8_t *opcode = (uint8_t*)data;
    opcode += 2;        // skip EXTOP_PREFIX and SLEEP_OP

    acpi_object_t time = {0};
    return_size += acpi_eval_object(&time, state, &opcode[0]);

    if(time.integer == 0)
        time.integer = 1;

    acpi_sleep(time.integer);

    return return_size;
}




