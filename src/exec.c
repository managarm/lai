
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 by Omar Muhamed
 */

#include <lai/core.h>
#include "aml_opcodes.h"
#include "exec_impl.h"
#include "ns_impl.h"

static int debug_opcodes = 0;

/* ACPI Control Method Execution */
/* Type1Opcode := DefBreak | DefBreakPoint | DefContinue | DefFatal | DefIfElse |
   DefLoad | DefNoop | DefNotify | DefRelease | DefReset | DefReturn |
   DefSignal | DefSleep | DefStall | DefUnload | DefWhile */

void lai_eval_operand(lai_object_t *destination, lai_state_t *state, uint8_t *code);

// Prepare the interpreter state for a control method call.
// Param: lai_state_t *state - will store method name and arguments
// Param: lai_nsnode_t *method - identifies the control method

void lai_init_state(lai_state_t *state) {
    lai_memset(state, 0, sizeof(lai_state_t));
    state->stack_ptr = -1;
    state->context_ptr = -1;
}

// Finalize the interpreter state. Frees all memory owned by the state.

void lai_finalize_state(lai_state_t *state) {
    lai_free_object(&state->retvalue);
    for(int i = 0; i < 7; i++)
    {
        lai_free_object(&state->arg[i]);
    }

    for(int i = 0; i < 8; i++)
    {
        lai_free_object(&state->local[i]);
    }
}

// Pushes a new item to the opstack and returns it.
static lai_object_t *lai_exec_push_opstack_or_die(lai_state_t *state) {
    if(state->opstack_ptr == 16)
        lai_panic("operand stack overflow\n");
    lai_object_t *object = &state->opstack[state->opstack_ptr];
    lai_memset(object, 0, sizeof(lai_object_t));
    state->opstack_ptr++;
    return object;
}

// Returns the n-th item from the opstack.
static lai_object_t *lai_exec_get_opstack(lai_state_t *state, int n) {
    if(n >= state->opstack_ptr)
        lai_panic("opstack access out of bounds"); // This is an internal execution error.
    return &state->opstack[n];
}

// Removes n items from the opstack.
static void lai_exec_pop_opstack(lai_state_t *state, int n) {
    for(int k = 0; k < n; k++)
        lai_free_object(&state->opstack[state->opstack_ptr - k - 1]);
    state->opstack_ptr -= n;
}

// Pushes a new item to the execution stack and returns it.
static lai_stackitem_t *lai_exec_push_stack_or_die(lai_state_t *state) {
    state->stack_ptr++;
    if(state->stack_ptr == 16)
        lai_panic("execution engine stack overflow\n");
    return &state->stack[state->stack_ptr];
}

// Returns the n-th item from the top of the stack.
static lai_stackitem_t *lai_exec_peek_stack(lai_state_t *state, int n) {
    if(state->stack_ptr - n < 0)
        return NULL;
    return &state->stack[state->stack_ptr - n];
}

// Returns the last item of the stack.
static lai_stackitem_t *lai_exec_peek_stack_back(lai_state_t *state) {
    return lai_exec_peek_stack(state, 0);
}

// Removes n items from the stack.
static void lai_exec_pop_stack(lai_state_t *state, int n) {
    state->stack_ptr -= n;
}

// Removes the last item from the stack.
static void lai_exec_pop_stack_back(lai_state_t *state) {
    lai_exec_pop_stack(state, 1);
}

// Returns the lai_stackitem_t pointed to by the state's context_ptr.
static lai_stackitem_t *lai_exec_context(lai_state_t *state) {
    return &state->stack[state->context_ptr];
}

// Updates the state's context_ptr to point to the innermost context item.
static void lai_exec_update_context(lai_state_t *state) {
    int j = 0;
    lai_stackitem_t *ctx_item;
    while(1) {
        ctx_item = lai_exec_peek_stack(state, j);
        if(!ctx_item)
            break;
        if(ctx_item->kind == LAI_POPULATE_CONTEXT_STACKITEM
                || ctx_item->kind == LAI_METHOD_CONTEXT_STACKITEM)
            break;
        j++;
    }

    state->context_ptr = state->stack_ptr - j;
}

static int lai_compare(lai_object_t *lhs, lai_object_t *rhs) {
    // TODO: Allow comparsions of strings and buffers as in the spec.
    if(lhs->type != LAI_INTEGER || rhs->type != LAI_INTEGER)
        lai_panic("comparsion of object type %d with type %d is not implemented\n",
                lhs->type, rhs->type);
    return lhs->integer - rhs->integer;
}

static void lai_exec_reduce(int opcode, lai_state_t *state, lai_object_t *operands,
        lai_object_t *reduction_res) {
    if(debug_opcodes)
        lai_debug("lai_exec_reduce: opcode 0x%02X\n", opcode);
    lai_object_t result = {0};
    switch(opcode) {
    case STORE_OP:
        lai_load_operand(state, &operands[0], &result);
        lai_store_operand(state, &operands[1], &result);
        break;
    case NOT_OP:
    {
        lai_object_t operand = {0};
        lai_load_operand(state, operands, &operand);

        result.type = LAI_INTEGER;
        result.integer = ~operand.integer;
        lai_store_operand(state, &operands[1], &result);
        break;
    }
    case ADD_OP:
    {
        lai_object_t lhs = {0};
        lai_object_t rhs = {0};
        lai_load_operand(state, &operands[0], &lhs);
        lai_load_operand(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = lhs.integer + rhs.integer;
        lai_store_operand(state, &operands[2], &result);
        break;
    }
    case SUBTRACT_OP:
    {
        lai_object_t lhs = {0};
        lai_object_t rhs = {0};
        lai_load_operand(state, &operands[0], &lhs);
        lai_load_operand(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = lhs.integer - rhs.integer;
        lai_store_operand(state, &operands[2], &result);
        break;
    }
    case MULTIPLY_OP:
    {
        lai_object_t lhs = {0};
        lai_object_t rhs = {0};
        lai_load_operand(state, &operands[0], &lhs);
        lai_load_operand(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = lhs.integer * rhs.integer;
        lai_store_operand(state, &operands[2], &result);
        break;
    }
    case AND_OP:
    {
        lai_object_t lhs = {0};
        lai_object_t rhs = {0};
        lai_load_operand(state, &operands[0], &lhs);
        lai_load_operand(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = lhs.integer & rhs.integer;
        lai_store_operand(state, &operands[2], &result);
        break;
    }
    case OR_OP:
    {
        lai_object_t lhs = {0};
        lai_object_t rhs = {0};
        lai_load_operand(state, &operands[0], &lhs);
        lai_load_operand(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = lhs.integer | rhs.integer;
        lai_store_operand(state, &operands[2], &result);
        break;
    }
    case XOR_OP:
    {
        lai_object_t lhs = {0};
        lai_object_t rhs = {0};
        lai_load_operand(state, &operands[0], &lhs);
        lai_load_operand(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = lhs.integer ^ rhs.integer;
        lai_store_operand(state, &operands[2], &result);
        break;
    }
    case SHL_OP:
    {
        lai_object_t lhs = {0};
        lai_object_t rhs = {0};
        lai_load_operand(state, &operands[0], &lhs);
        lai_load_operand(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = lhs.integer << rhs.integer;
        lai_store_operand(state, &operands[2], &result);
        break;
    }
    case SHR_OP:
    {
        lai_object_t lhs = {0};
        lai_object_t rhs = {0};
        lai_load_operand(state, &operands[0], &lhs);
        lai_load_operand(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = lhs.integer >> rhs.integer;
        lai_store_operand(state, &operands[2], &result);
        break;
    }
    case DIVIDE_OP:
    {
        lai_object_t lhs = {0};
        lai_object_t rhs = {0};
        lai_load_operand(state, &operands[0], &lhs);
        lai_load_operand(state, &operands[1], &rhs);

        lai_object_t mod = {0};
        lai_object_t div = {0};
        mod.type = LAI_INTEGER;
        div.type = LAI_INTEGER;
        mod.integer = lhs.integer % rhs.integer;
        div.integer = lhs.integer / rhs.integer;
        lai_store_operand(state, &operands[2], &mod);
        lai_store_operand(state, &operands[3], &div);
        break;
    }
    case INCREMENT_OP:
        lai_load_operand(state, operands, &result);
        result.integer++;
        lai_store_operand(state, operands, &result);
        break;
    case DECREMENT_OP:
        lai_load_operand(state, operands, &result);
        result.integer--;
        lai_store_operand(state, operands, &result);
        break;
    case LNOT_OP:
    {
        lai_object_t operand = {0};
        lai_load_operand(state, operands, &operand);

        result.type = LAI_INTEGER;
        result.integer = !operand.integer;
        break;
    }
    case LAND_OP:
    {
        lai_object_t lhs = {0};
        lai_object_t rhs = {0};
        lai_load_operand(state, &operands[0], &lhs);
        lai_load_operand(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = lhs.integer && rhs.integer;
        break;
    }
    case LOR_OP:
    {
        lai_object_t lhs = {0};
        lai_object_t rhs = {0};
        lai_load_operand(state, &operands[0], &lhs);
        lai_load_operand(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = lhs.integer || rhs.integer;
        break;
    }
    case LEQUAL_OP:
    {
        lai_object_t lhs = {0};
        lai_object_t rhs = {0};
        lai_load_operand(state, &operands[0], &lhs);
        lai_load_operand(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = !lai_compare(&lhs, &rhs);
        break;
    }
    case LLESS_OP:
    {
        lai_object_t lhs = {0};
        lai_object_t rhs = {0};
        lai_load_operand(state, &operands[0], &lhs);
        lai_load_operand(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = lai_compare(&lhs, &rhs) < 0;
        break;
    }
    case LGREATER_OP:
    {
        lai_object_t lhs = {0};
        lai_object_t rhs = {0};
        lai_load_operand(state, &operands[0], &lhs);
        lai_load_operand(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = lai_compare(&lhs, &rhs) > 0;
        break;
    }
    case INDEX_OP:
    {
        lai_object_t storage = {0};
        lai_object_t index = {0};
        lai_alias_operand(state, &operands[0], &storage);
        lai_load_operand(state, &operands[1], &index);
        int n = index.integer;

        if(storage.type == LAI_STRING_REFERENCE)
        {
            if(n >= lai_strlen(storage.string))
                lai_panic("string Index() out of bounds");
            result.type = LAI_STRING_INDEX;
            result.string = storage.string;
            result.integer = n;
        }else if(storage.type == LAI_BUFFER_REFERENCE)
        {
            if(n >= storage.buffer_size)
                lai_panic("buffer Index() out of bounds");
            result.type = LAI_BUFFER_INDEX;
            result.buffer = storage.buffer;
            result.integer = n;
        }else if(storage.type == LAI_PACKAGE_REFERENCE)
        {
            if(n >= storage.package_size)
                lai_panic("package Index() out of bounds");
            result.type = LAI_PACKAGE_INDEX;
            result.package = storage.package;
            result.integer = n;
        }else
            lai_panic("Index() is only defined for buffers, strings and packages\n");
        lai_free_object(&storage);

        lai_store_operand(state, &operands[2], &result);
        break;
    }
    case DEREF_OP:
    {
        lai_object_t ref = {0};
        lai_load_operand(state, &operands[0], &ref);

        if(ref.type == LAI_STRING_INDEX)
        {
            result.type = LAI_INTEGER;
            result.integer = ref.string[ref.integer];
        }else if(ref.type == LAI_BUFFER)
        {
            uint8_t *window = ref.buffer;
            result.type = LAI_INTEGER;
            result.integer = window[ref.integer];
        }else if(ref.type == LAI_PACKAGE)
        {
            lai_copy_object(&result, &ref.package[ref.integer]);
        }else
            lai_panic("DeRefOf() is only defined for references\n");
        break;
    }
    case SIZEOF_OP:
    {
        lai_object_t object = {0};
        lai_load_operand(state, &operands[0], &object);

        if(object.type == LAI_STRING)
        {
            result.type = LAI_INTEGER;
            result.integer = lai_strlen(object.string);
        }else if(object.type == LAI_BUFFER)
        {
            result.type = LAI_INTEGER;
            result.integer = object.buffer_size;
        }else if(object.type == LAI_PACKAGE)
        {
            result.type = LAI_INTEGER;
            result.integer = object.package_size;
        }else
            lai_panic("SizeOf() is only defined for buffers, strings and packages\n");
        break;
    }
    case (EXTOP_PREFIX << 8) | CONDREF_OP:
    {
        lai_object_t *operand = &operands[0];
        lai_object_t *target = &operands[1];

        // TODO: The resolution code should be shared with REF_OP.
        lai_object_t ref = {0};
        switch(operand->type)
        {
        case LAI_UNRESOLVED_NAME:
        {
            lai_nsnode_t *handle = lai_exec_resolve(operand->name);
            if(handle) {
                ref.type = LAI_HANDLE;
                ref.handle = handle;
            }
            break;
        }
        default:
            lai_panic("CondRefOp() is only defined for names\n");
        }

        if(ref.type)
        {
            result.type = LAI_INTEGER;
            result.integer = 1;
            lai_store_operand(state, target, &ref);
        }else
        {
            result.type = LAI_INTEGER;
            result.integer = 0;
        }
        break;
    }
    default:
        lai_panic("undefined opcode in lai_exec_reduce: %02X\n", opcode);
    }

    lai_move_object(reduction_res, &result);
}

// lai_exec_run(): Internal function, executes actual AML opcodes
// Param:  uint8_t *method - pointer to method opcodes
// Param:  lai_state_t *state - machine state
// Return: int - 0 on success

static int lai_exec_run(uint8_t *method, lai_state_t *state)
{
    lai_stackitem_t *item;
    while((item = lai_exec_peek_stack_back(state)))
    {
        // Package-size encoding (and similar) needs to know the PC of the opcode.
        // If an opcode sequence contains a pkgsize, the sequence generally ends at:
        //     opcode_pc + pkgsize + opcode size.
        int opcode_pc = state->pc;

        // Whether we use the result of an expression or not.
        // If yes, it will be pushed onto the opstack after the expression is computed.
        int exec_result_mode = LAI_EXEC_MODE;

        if(item->kind == LAI_POPULATE_CONTEXT_STACKITEM)
        {
			if(state->pc == item->ctx_limit)
            {
				lai_exec_pop_stack_back(state);
                lai_exec_update_context(state);
				continue;
			}

            if(state->pc > item->ctx_limit) // This would be an interpreter bug.
                lai_panic("namespace population escaped out of code range\n");
        }else if(item->kind == LAI_METHOD_CONTEXT_STACKITEM)
        {
			// ACPI does an implicit Return(0) at the end of a control method.
			if(state->pc == state->limit)
			{
				if(state->opstack_ptr) // This is an internal error.
					lai_panic("opstack is not empty before return\n");
				lai_object_t *result = lai_exec_push_opstack_or_die(state);
				result->type = LAI_INTEGER;
				result->integer = 0;

				lai_exec_pop_stack_back(state);
                lai_exec_update_context(state);
				continue;
			}
        }else if(item->kind == LAI_EVALOPERAND_STACKITEM)
        {
            if(state->opstack_ptr == item->opstack_frame + 1)
            {
                lai_exec_pop_stack_back(state);
                return 0;
            }

            exec_result_mode = LAI_OBJECT_MODE;
        }else if(item->kind == LAI_PKG_INITIALIZER_STACKITEM)
        {
            lai_object_t *frame = lai_exec_get_opstack(state, item->opstack_frame);
            lai_object_t *package = &frame[0];
            lai_object_t *initializer = &frame[1];
            if(state->opstack_ptr == item->opstack_frame + 2)
            {
                if(item->pkg_index == package->package_size)
                    lai_panic("package initializer overflows its size");
                LAI_ENSURE(item->pkg_index < package->package_size);

                lai_move_object(&package->package[item->pkg_index], initializer);
                item->pkg_index++;
                lai_exec_pop_opstack(state, 1);
            }
            LAI_ENSURE(state->opstack_ptr == item->opstack_frame + 1);

            if(state->pc == item->pkg_end)
            {
                if(item->pkg_result_mode == LAI_EXEC_MODE)
                    lai_exec_pop_opstack(state, 1);
                else
                    LAI_ENSURE(item->pkg_result_mode == LAI_DATA_MODE
                               || item->pkg_result_mode == LAI_OBJECT_MODE);

                lai_exec_pop_stack_back(state);
                continue;
            }

            if(state->pc > item->pkg_end) // This would be an interpreter bug.
                lai_panic("package initializer escaped out of code range\n");

            exec_result_mode = LAI_DATA_MODE;
        }else if(item->kind == LAI_OP_STACKITEM)
        {
            int k = state->opstack_ptr - item->opstack_frame;
//            lai_debug("got %d parameters\n", k);
            if(!item->op_arg_modes[k]) {
                lai_object_t result = {0};
                lai_object_t *operands = lai_exec_get_opstack(state, item->opstack_frame);
                lai_exec_reduce(item->op_opcode, state, operands, &result);
                lai_exec_pop_opstack(state, k);

                if(item->op_result_mode == LAI_OBJECT_MODE
                        || item->op_result_mode == LAI_TARGET_MODE)
                {
                    lai_object_t *opstack_res = lai_exec_push_opstack_or_die(state);
                    lai_move_object(opstack_res, &result);
                }else
                    LAI_ENSURE(item->op_result_mode == LAI_EXEC_MODE);

                lai_exec_pop_stack_back(state);
                continue;
            }

            exec_result_mode = item->op_arg_modes[k];
        }else if(item->kind == LAI_LOOP_STACKITEM)
        {
            if(state->pc == item->loop_pred)
            {
                // We are at the beginning of a loop. We check the predicate; if it is false,
                // we jump to the end of the loop and remove the stack item.
                lai_object_t predicate = {0};
                lai_eval_operand(&predicate, state, method);
                if(!predicate.integer)
                {
                    state->pc = item->loop_end;
                    lai_exec_pop_stack_back(state);
                }
                continue;
            }else if(state->pc == item->loop_end)
            {
                // Unconditionally jump to the loop's predicate.
                state->pc = item->loop_pred;
                continue;
            }

            if (state->pc > item->loop_end) // This would be an interpreter bug.
                lai_panic("execution escaped out of While() body\n");
        }else if(item->kind == LAI_COND_STACKITEM)
        {
            // If the condition wasn't taken, execute the Else() block if it exists
            if(!item->cond_taken)
            {
                if(method[state->pc] == ELSE_OP)
                {
                    size_t else_size;
                    state->pc++;
                    state->pc += lai_parse_pkgsize(method + state->pc, &else_size);
                }

                lai_exec_pop_stack_back(state);
                continue;
            }

            // Clean up the execution stack at the end of If().
            if(state->pc == item->cond_end)
            {
                // Consume a follow-up Else() opcode.
                if(state->pc < state->limit && method[state->pc] == ELSE_OP) {
                    state->pc++;
                    size_t else_size;
                    state->pc += lai_parse_pkgsize(method + state->pc, &else_size);
                    state->pc = opcode_pc + else_size + 1;
                }

                lai_exec_pop_stack_back(state);
                continue;
            }
        }else
            lai_panic("unexpected lai_stackitem_t\n");

        if(state->pc >= state->limit) // This would be an interpreter bug.
            lai_panic("execution escaped out of code range (PC is 0x%x with limit 0x%x)\n",
                    state->pc, state->limit);

        lai_stackitem_t *ctx_item = lai_exec_context(state);
        lai_nsnode_t *ctx_handle = ctx_item->ctx_handle;

        // Process names.
        if(lai_is_name(method[state->pc])) {
            lai_object_t unresolved = {0};
            unresolved.type = LAI_UNRESOLVED_NAME;
            state->pc += acpins_resolve_path(ctx_handle, unresolved.name, method + state->pc);

            if(exec_result_mode == LAI_DATA_MODE || exec_result_mode == LAI_TARGET_MODE)
            {
                if(debug_opcodes)
                    lai_debug("parsing name %s [@ %d]\n", unresolved.name, opcode_pc);

                lai_object_t *opstack_res = lai_exec_push_opstack_or_die(state);
                lai_move_object(opstack_res, &unresolved);
            }else
            {
                LAI_ENSURE(exec_result_mode == LAI_OBJECT_MODE
                           || exec_result_mode == LAI_EXEC_MODE);
                lai_nsnode_t *handle = lai_exec_resolve(unresolved.name);
                if(!handle)
                    lai_panic("undefined reference %s in object mode\n", unresolved.name);

                lai_object_t result = {0};
                if(handle->type == LAI_NAMESPACE_METHOD)
                {
                    if(debug_opcodes)
                        lai_debug("parsing invocation %s [@ %d]\n", unresolved.name, opcode_pc);

                    lai_state_t nested_state;
                    lai_init_state(&nested_state);
                    int argc = handle->method_flags & METHOD_ARGC_MASK;
                    for(int i = 0; i < argc; i++)
                        lai_eval_operand(&nested_state.arg[i], state, method);

                    lai_exec_method(handle, &nested_state);
                    lai_move_object(&result, &nested_state.retvalue);
                    lai_finalize_state(&nested_state);
                }else
                {
                    if(debug_opcodes)
                        lai_debug("parsing name %s [@ %d]\n", unresolved.name, opcode_pc);

                    lai_load_ns(handle, &result);
                }

                if(exec_result_mode == LAI_OBJECT_MODE)
                {
                    lai_object_t *opstack_res = lai_exec_push_opstack_or_die(state);
                    lai_move_object(opstack_res, &result);
                }
            }
            lai_free_object(&unresolved);
            continue;
        }

        /* General opcodes */
        int opcode;
        if(method[state->pc] == EXTOP_PREFIX)
        {
            if(state->pc + 1 == state->limit)
                lai_panic("two-byte opcode on method boundary\n");
            opcode = (EXTOP_PREFIX << 8) | method[state->pc + 1];
        }else
            opcode = method[state->pc];
        if(debug_opcodes)
            lai_debug("parsing opcode 0x%02x [@ %d]\n", opcode, opcode_pc);

        // This switch handles the majority of all opcodes.
        switch(opcode)
        {
        case NOP_OP:
            state->pc++;
            break;

        case ZERO_OP:
            if(exec_result_mode == LAI_DATA_MODE || exec_result_mode == LAI_OBJECT_MODE)
            {
                lai_object_t *result = lai_exec_push_opstack_or_die(state);
                result->type = LAI_INTEGER;
                result->integer = 0;
            }else if(exec_result_mode == LAI_TARGET_MODE)
            {
                // In target mode, ZERO_OP generates a null target and not an integer!
                lai_object_t *result = lai_exec_push_opstack_or_die(state);
                result->type = LAI_NULL_NAME;
            }else
            {
                lai_warn("Zero() in execution mode has no effect\n");
                LAI_ENSURE(exec_result_mode == LAI_EXEC_MODE);
            }
            state->pc++;
            break;
        case ONE_OP:
            if(exec_result_mode == LAI_DATA_MODE || exec_result_mode == LAI_OBJECT_MODE)
            {
                lai_object_t *result = lai_exec_push_opstack_or_die(state);
                result->type = LAI_INTEGER;
                result->integer = 1;
            }else
                LAI_ENSURE(exec_result_mode == LAI_EXEC_MODE);
            state->pc++;
            break;
        case ONES_OP:
            if(exec_result_mode == LAI_DATA_MODE || exec_result_mode == LAI_OBJECT_MODE)
            {
                lai_object_t *result = lai_exec_push_opstack_or_die(state);
                result->type = LAI_INTEGER;
                result->integer = ~((uint64_t)0);
            }else
                LAI_ENSURE(exec_result_mode == LAI_EXEC_MODE);
            state->pc++;
            break;

        case BYTEPREFIX:
        case WORDPREFIX:
        case DWORDPREFIX:
        case QWORDPREFIX:
        {
            uint64_t integer;
            size_t integer_size = lai_eval_integer(method + state->pc, &integer);
            if(!integer_size)
                lai_panic("failed to parse integer opcode\n");
            if(exec_result_mode == LAI_DATA_MODE || exec_result_mode == LAI_OBJECT_MODE)
            {
                lai_object_t *result = lai_exec_push_opstack_or_die(state);
                result->type = LAI_INTEGER;
                result->integer = integer;
            }else
                LAI_ENSURE(exec_result_mode == LAI_EXEC_MODE);
            state->pc += integer_size;
            break;
        }
        case STRINGPREFIX:
        {
            state->pc++;

            size_t n = 0; // Determine the length of null-terminated string.
            while(state->pc + n < state->limit && method[state->pc + n])
                n++;
            if(state->pc + n == state->limit)
                lai_panic("unterminated string in AML code");

            if(exec_result_mode == LAI_DATA_MODE || exec_result_mode == LAI_OBJECT_MODE)
            {
                lai_object_t *opstack_res = lai_exec_push_opstack_or_die(state);
                opstack_res->type = LAI_STRING;
                opstack_res->string = lai_malloc(n + 1);
                lai_memcpy(opstack_res->string, method + state->pc, n);
                opstack_res->string[n] = 0;
            }else
                LAI_ENSURE(exec_result_mode == LAI_EXEC_MODE);
            state->pc += n + 1;
            break;
        }
        case BUFFER_OP:
        {
            state->pc++;        // Skip BUFFER_OP.

            // Size of the buffer initializer.
            // Note that not all elements of the buffer need to be initialized.
            size_t encoded_size;
            state->pc += lai_parse_pkgsize(method + state->pc, &encoded_size);

            // The size of the buffer in bytes.
            lai_object_t buffer_size = {0};
            lai_eval_operand(&buffer_size, state, method);

            lai_object_t result = {0};
            result.type = LAI_BUFFER;
            result.buffer_size = buffer_size.integer;
            result.buffer = lai_malloc(buffer_size.integer);
            if(!result.buffer)
                lai_panic("failed to allocate memory for AML buffer");
            lai_memset(result.buffer, 0, buffer_size.integer);

            int initial_size = (opcode_pc + encoded_size + 1) - state->pc;
            if(initial_size < 0)
                lai_panic("buffer initializer has negative size\n");
            if(initial_size > result.buffer_size)
                lai_panic("buffer initializer overflows buffer\n");
            lai_memcpy(result.buffer, method + state->pc, initial_size);
            state->pc += initial_size;

            if(exec_result_mode == LAI_DATA_MODE || exec_result_mode == LAI_OBJECT_MODE)
            {
                lai_object_t *opstack_res = lai_exec_push_opstack_or_die(state);
                lai_move_object(opstack_res, &result);
            }else
                LAI_ENSURE(exec_result_mode == LAI_EXEC_MODE);
            lai_free_object(&result);
            break;
        }
        case PACKAGE_OP:
        {
            state->pc++;

            // Size of the package initializer.
            // Note that not all elements of the package need to be initialized.
            size_t encoded_size;
            state->pc += lai_parse_pkgsize(method + state->pc, &encoded_size);

            // The number of elements of the package.
            int num_ents = method[state->pc];
            state->pc++;

            lai_stackitem_t *pkg_item = lai_exec_push_stack_or_die(state);
            pkg_item->kind = LAI_PKG_INITIALIZER_STACKITEM;
            pkg_item->opstack_frame = state->opstack_ptr;
            pkg_item->pkg_index = 0;
            pkg_item->pkg_end = opcode_pc + encoded_size + 1;
            pkg_item->pkg_result_mode = exec_result_mode;

            lai_object_t *opstack_pkg = lai_exec_push_opstack_or_die(state);
            opstack_pkg->type = LAI_PACKAGE;
            opstack_pkg->package = lai_calloc(num_ents, sizeof(lai_object_t));
            opstack_pkg->package_size = num_ents;
            break;
        }

        case (EXTOP_PREFIX << 8) | SLEEP_OP:
            lai_exec_sleep(method, state);
            break;

        /* A control method can return literally any object */
        /* So we need to take this into consideration */
        case RETURN_OP:
        {
            state->pc++;
            lai_object_t result = {0};
            lai_eval_operand(&result, state, method);

            // Find the last LAI_METHOD_CONTEXT_STACKITEM on the stack.
            int j = 0;
            lai_stackitem_t *method_item;
            while(1) {
                method_item = lai_exec_peek_stack(state, j);
                if(!method_item)
                    lai_panic("Return() outside of control method()\n");
                if(method_item->kind == LAI_METHOD_CONTEXT_STACKITEM)
                    break;
                // TODO: Verify that we only cross conditions/loops.
                j++;
            }

            // Remove the method stack item and push the return value.
            if(state->opstack_ptr) // This is an internal error.
                lai_panic("opstack is not empty before return\n");
            lai_object_t *opstack_res = lai_exec_push_opstack_or_die(state);
            lai_move_object(opstack_res, &result);

            lai_exec_pop_stack(state, j + 1);
            lai_exec_update_context(state);
            break;
        }
        /* While Loops */
        case WHILE_OP:
        {
            size_t loop_size;
            state->pc++;
            state->pc += lai_parse_pkgsize(&method[state->pc], &loop_size);

            lai_stackitem_t *loop_item = lai_exec_push_stack_or_die(state);
            loop_item->kind = LAI_LOOP_STACKITEM;
            loop_item->loop_pred = state->pc;
            loop_item->loop_end = opcode_pc + loop_size + 1;
            break;
        }
        /* Continue Looping */
        case CONTINUE_OP:
        {
            // Find the last LAI_LOOP_STACKITEM on the stack.
            int j = 0;
            lai_stackitem_t *loop_item;
            while(1) {
                loop_item = lai_exec_peek_stack(state, j);
                if(!loop_item)
                    lai_panic("Continue() outside of While()\n");
                if(loop_item->kind == LAI_LOOP_STACKITEM)
                    break;
                // TODO: Verify that we only cross conditions/loops.
                j++;
            }

            // Keep the loop item but remove nested items from the exeuction stack.
            state->pc = loop_item->loop_pred;
            lai_exec_pop_stack(state, j);
            break;
        }
        /* Break Loop */
        case BREAK_OP:
        {
            // Find the last LAI_LOOP_STACKITEM on the stack.
            int j = 0;
            lai_stackitem_t *loop_item;
            while(1) {
                loop_item = lai_exec_peek_stack(state, j);
                if(!loop_item)
                    lai_panic("Break() outside of While()\n");
                if(loop_item->kind == LAI_LOOP_STACKITEM)
                    break;
                // TODO: Verify that we only cross conditions/loops.
                j++;
            }

            // Remove the loop item from the execution stack.
            state->pc = loop_item->loop_end;
            lai_exec_pop_stack(state, j + 1);
            break;
        }
        /* If/Else Conditional */
        case IF_OP:
        {
            size_t if_size;
            state->pc++;
            state->pc += lai_parse_pkgsize(method + state->pc, &if_size);

            // Evaluate the predicate
            lai_object_t predicate = {0};
            lai_eval_operand(&predicate, state, method);

            lai_stackitem_t *cond_item = lai_exec_push_stack_or_die(state);
            cond_item->kind = LAI_COND_STACKITEM;
            cond_item->cond_taken = predicate.integer;
            cond_item->cond_end = opcode_pc + if_size + 1;

            if(!cond_item->cond_taken)
                state->pc = cond_item->cond_end;

            break;
        }
        case ELSE_OP:
            lai_panic("Else() outside of If()\n");
            break;

        // "Simple" objects in the ACPI namespace.
        case NAME_OP:
            lai_exec_name(method, ctx_handle, state);
            break;
        case BYTEFIELD_OP:
            lai_exec_bytefield(method, ctx_handle, state);
            break;
        case WORDFIELD_OP:
            lai_exec_wordfield(method, ctx_handle, state);
            break;
        case DWORDFIELD_OP:
            lai_exec_dwordfield(method, ctx_handle, state);
            break;

        // Scope-like objects in the ACPI namespace.
        case SCOPE_OP:
        {
            state->pc++;

            size_t encoded_size;
            state->pc += lai_parse_pkgsize(method + state->pc, &encoded_size);
            char name[ACPI_MAX_NAME];
            state->pc += acpins_resolve_path(ctx_handle, name, method + state->pc);

            lai_nsnode_t *node = acpins_create_nsnode_or_die();
            node->type = LAI_NAMESPACE_SCOPE;
            lai_strcpy(node->path, name);
            acpins_install_nsnode(node);

            lai_stackitem_t *item = lai_exec_push_stack_or_die(state);
            item->kind = LAI_POPULATE_CONTEXT_STACKITEM;
            item->ctx_handle = node;
            item->ctx_limit = opcode_pc + encoded_size + 1;
            lai_exec_update_context(state);
            break;
        }
        case (EXTOP_PREFIX << 8) | DEVICE:
        {
            state->pc += 2;

            size_t encoded_size;
            state->pc += lai_parse_pkgsize(method + state->pc, &encoded_size);
            char name[ACPI_MAX_NAME];
            state->pc += acpins_resolve_path(ctx_handle, name, method + state->pc);

            lai_nsnode_t *node = acpins_create_nsnode_or_die();
            node->type = LAI_NAMESPACE_DEVICE;
            lai_strcpy(node->path, name);
            acpins_install_nsnode(node);

            lai_stackitem_t *item = lai_exec_push_stack_or_die(state);
            item->kind = LAI_POPULATE_CONTEXT_STACKITEM;
            item->ctx_handle = node;
            item->ctx_limit = opcode_pc + encoded_size + 2;
            lai_exec_update_context(state);
            break;
        }
        case (EXTOP_PREFIX << 8) | PROCESSOR:
            state->pc += acpins_create_processor(ctx_handle, method + state->pc);
            break;
        case (EXTOP_PREFIX << 8) | THERMALZONE:
        {
            state->pc += 2;

            size_t encoded_size;
            state->pc += lai_parse_pkgsize(method + state->pc, &encoded_size);
            char name[ACPI_MAX_NAME];
            state->pc += acpins_resolve_path(ctx_handle, name, method + state->pc);

            lai_nsnode_t *node = acpins_create_nsnode_or_die();
            node->type = LAI_NAMESPACE_THERMALZONE;
            lai_strcpy(node->path, name);
            acpins_install_nsnode(node);

            lai_stackitem_t *item = lai_exec_push_stack_or_die(state);
            item->kind = LAI_POPULATE_CONTEXT_STACKITEM;
            item->ctx_handle = node;
            item->ctx_limit = opcode_pc + encoded_size + 2;
            lai_exec_update_context(state);
            break;
        }

        // Leafs in the ACPI namespace.
        case METHOD_OP:
            state->pc += acpins_create_method(ctx_handle, method + state->pc);
            break;
        case ALIAS_OP:
            state->pc += acpins_create_alias(ctx_handle, method + state->pc);
            break;
        case (EXTOP_PREFIX << 8) | MUTEX:
            state->pc += acpins_create_mutex(ctx_handle, method + state->pc);
            break;
        case (EXTOP_PREFIX << 8) | OPREGION:
        {
            state->pc += 2;
            char name[ACPI_MAX_NAME];
            state->pc += acpins_resolve_path(ctx_handle, name, method + state->pc);

            // First byte identifies the address space (memory, I/O ports, PCI, etc.).
            int address_space = method[state->pc];
            state->pc++;

            // Now, parse the offset and length of the opregion.
            lai_object_t disp = {0};
            lai_object_t length = {0};
            lai_eval_operand(&disp, state, method);
            lai_eval_operand(&length, state, method);

            lai_nsnode_t *node = acpins_create_nsnode_or_die();
            lai_strcpy(node->path, name);
            node->op_address_space = address_space;
            node->op_base = disp.integer;
            node->op_length = length.integer;
            acpins_install_nsnode(node);
            break;
        }
        case (EXTOP_PREFIX << 8) | FIELD:
            state->pc += acpins_create_field(ctx_handle, method + state->pc);
            break;
        case (EXTOP_PREFIX << 8) | INDEXFIELD:
            state->pc += acpins_create_indexfield(ctx_handle, method + state->pc);
            break;

        case ARG0_OP:
        case ARG1_OP:
        case ARG2_OP:
        case ARG3_OP:
        case ARG4_OP:
        case ARG5_OP:
        case ARG6_OP:
        {
            if(exec_result_mode == LAI_OBJECT_MODE
                    || exec_result_mode == LAI_TARGET_MODE)
            {
                lai_object_t *result = lai_exec_push_opstack_or_die(state);
                result->type = LAI_ARG_NAME;
                result->index = opcode - ARG0_OP;
            }
            state->pc++;
            break;
        }

        case LOCAL0_OP:
        case LOCAL1_OP:
        case LOCAL2_OP:
        case LOCAL3_OP:
        case LOCAL4_OP:
        case LOCAL5_OP:
        case LOCAL6_OP:
        case LOCAL7_OP:
        {
            if(exec_result_mode == LAI_OBJECT_MODE
                    || exec_result_mode == LAI_TARGET_MODE)
            {
                lai_object_t *result = lai_exec_push_opstack_or_die(state);
                result->type = LAI_LOCAL_NAME;
                result->index = opcode - LOCAL0_OP;
            }
            state->pc++;
            break;
        }

        case STORE_OP:
        case NOT_OP:
        {
            lai_stackitem_t *op_item = lai_exec_push_stack_or_die(state);
            op_item->kind = LAI_OP_STACKITEM;
            op_item->op_opcode = opcode;
            op_item->opstack_frame = state->opstack_ptr;
            op_item->op_arg_modes[0] = LAI_OBJECT_MODE;
            op_item->op_arg_modes[1] = LAI_TARGET_MODE;
            op_item->op_arg_modes[2] = 0;
            op_item->op_result_mode = exec_result_mode;
            state->pc++;
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
            lai_stackitem_t *op_item = lai_exec_push_stack_or_die(state);
            op_item->kind = LAI_OP_STACKITEM;
            op_item->op_opcode = opcode;
            op_item->opstack_frame = state->opstack_ptr;
            op_item->op_arg_modes[0] = LAI_OBJECT_MODE;
            op_item->op_arg_modes[1] = LAI_OBJECT_MODE;
            op_item->op_arg_modes[2] = LAI_TARGET_MODE;
            op_item->op_arg_modes[3] = 0;
            op_item->op_result_mode = exec_result_mode;
            state->pc++;
            break;
        }
        case DIVIDE_OP:
        {
            lai_stackitem_t *op_item = lai_exec_push_stack_or_die(state);
            op_item->kind = LAI_OP_STACKITEM;
            op_item->op_opcode = opcode;
            op_item->opstack_frame = state->opstack_ptr;
            op_item->op_arg_modes[0] = LAI_OBJECT_MODE;
            op_item->op_arg_modes[1] = LAI_OBJECT_MODE;
            op_item->op_arg_modes[2] = LAI_TARGET_MODE;
            op_item->op_arg_modes[3] = LAI_TARGET_MODE;
            op_item->op_arg_modes[4] = 0;
            op_item->op_result_mode = exec_result_mode;
            state->pc++;
            break;
        }

        case INCREMENT_OP:
        case DECREMENT_OP:
        {
            lai_stackitem_t *op_item = lai_exec_push_stack_or_die(state);
            op_item->kind = LAI_OP_STACKITEM;
            op_item->op_opcode = opcode;
            op_item->opstack_frame = state->opstack_ptr;
            op_item->op_arg_modes[0] = LAI_TARGET_MODE;
            op_item->op_arg_modes[1] = 0;
            op_item->op_result_mode = exec_result_mode;
            state->pc++;
            break;
        }

        case LNOT_OP:
        {
            lai_stackitem_t *op_item = lai_exec_push_stack_or_die(state);
            op_item->kind = LAI_OP_STACKITEM;
            op_item->op_opcode = opcode;
            op_item->opstack_frame = state->opstack_ptr;
            op_item->op_arg_modes[0] = LAI_OBJECT_MODE;
            op_item->op_arg_modes[1] = 0;
            op_item->op_result_mode = exec_result_mode;
            state->pc++;
            break;
        }
        case LAND_OP:
        case LOR_OP:
        case LEQUAL_OP:
        case LLESS_OP:
        case LGREATER_OP:
        {
            lai_stackitem_t *op_item = lai_exec_push_stack_or_die(state);
            op_item->kind = LAI_OP_STACKITEM;
            op_item->op_opcode = opcode;
            op_item->opstack_frame = state->opstack_ptr;
            op_item->op_arg_modes[0] = LAI_OBJECT_MODE;
            op_item->op_arg_modes[1] = LAI_OBJECT_MODE;
            op_item->op_arg_modes[2] = 0;
            op_item->op_result_mode = exec_result_mode;
            state->pc++;
            break;
        }

        case INDEX_OP:
        {
            lai_stackitem_t *op_item = lai_exec_push_stack_or_die(state);
            op_item->kind = LAI_OP_STACKITEM;
            op_item->op_opcode = opcode;
            op_item->opstack_frame = state->opstack_ptr;
            op_item->op_arg_modes[0] = LAI_TARGET_MODE;
            op_item->op_arg_modes[1] = LAI_OBJECT_MODE;
            op_item->op_arg_modes[2] = LAI_TARGET_MODE;
            op_item->op_arg_modes[3] = 0;
            op_item->op_result_mode = exec_result_mode;
            state->pc++;
            break;
        }
        case DEREF_OP:
        case SIZEOF_OP:
        {
            lai_stackitem_t *op_item = lai_exec_push_stack_or_die(state);
            op_item->kind = LAI_OP_STACKITEM;
            op_item->op_opcode = opcode;
            op_item->opstack_frame = state->opstack_ptr;
            op_item->op_arg_modes[0] = LAI_OBJECT_MODE;
            op_item->op_arg_modes[1] = 0;
            op_item->op_result_mode = exec_result_mode;
            state->pc++;
            break;
        }
        case (EXTOP_PREFIX << 8) | CONDREF_OP:
        {
            lai_stackitem_t *op_item = lai_exec_push_stack_or_die(state);
            op_item->kind = LAI_OP_STACKITEM;
            op_item->op_opcode = opcode;
            op_item->opstack_frame = state->opstack_ptr;
            // TODO: There should be a NAME_MODE that allows only names to be parsed.
            op_item->op_arg_modes[0] = LAI_DATA_MODE;
            op_item->op_arg_modes[1] = LAI_TARGET_MODE;
            op_item->op_arg_modes[2] = 0;
            op_item->op_result_mode = exec_result_mode;
            state->pc += 2;
            break;
        }

        default:
            lai_panic("unexpected opcode in lai_exec_run(), sequence %02X %02X %02X %02X\n",
                    method[state->pc + 0], method[state->pc + 1],
                    method[state->pc + 2], method[state->pc + 3]);
        }
    }

    return 0;
}

int lai_populate(lai_nsnode_t *parent, void *data, size_t size, lai_state_t *state) {
    lai_stackitem_t *item = lai_exec_push_stack_or_die(state);
    item->kind = LAI_POPULATE_CONTEXT_STACKITEM;
    item->ctx_handle = parent;
    item->ctx_limit = size;
    lai_exec_update_context(state);

    state->pc = 0;
    state->limit = size;
    int status = lai_exec_run(data, state);
    if(status)
        lai_panic("lai_exec_run() failed in lai_populate()\n");
    return 0;
}

// lai_exec_method(): Finds and executes a control method
// Param:    lai_nsnode_t *method - method to execute
// Param:    lai_state_t *state - execution engine state
// Return:    int - 0 on success

int lai_exec_method(lai_nsnode_t *method, lai_state_t *state)
{
    // Check for OS-defined methods.
    if(method->method_override)
        return method->method_override(state->arg, &state->retvalue);

    // Okay, by here it's a real method.
    //lai_debug("execute control method %s\n", method->path);
    lai_stackitem_t *item = lai_exec_push_stack_or_die(state);
    item->kind = LAI_METHOD_CONTEXT_STACKITEM;
    item->ctx_handle = method;
    lai_exec_update_context(state);

    state->pc = 0;
    state->limit = method->size;
    int status = lai_exec_run(method->pointer, state);
    if(status)
        return status;

    /*lai_debug("%s finished, ", method->path);

    if(state->retvalue.type == LAI_INTEGER)
        lai_debug("return value is integer: %d\n", state->retvalue.integer);
    else if(state->retvalue.type == LAI_STRING)
        lai_debug("return value is string: '%s'\n", state->retvalue.string);
    else if(state->retvalue.type == LAI_PACKAGE)
        lai_debug("return value is package\n");
    else if(state->retvalue.type == LAI_BUFFER)
        lai_debug("return value is buffer\n");*/

    if(state->opstack_ptr != 1) // This would be an internal error.
        lai_panic("expected exactly one return value after method invocation\n");
    lai_object_t *result = lai_exec_get_opstack(state, 0);
    lai_move_object(&state->retvalue, result);
    lai_exec_pop_opstack(state, 1);
    return 0;
}

// Evaluates an AML expression recursively.
// TODO: Eventually, we want to remove this function. However, this requires refactoring
//       lai_exec_run() to avoid all kinds of recursion.

void lai_eval_operand(lai_object_t *destination, lai_state_t *state, uint8_t *code) {
    int opstack = state->opstack_ptr;

    lai_stackitem_t *item = lai_exec_push_stack_or_die(state);
    item->kind = LAI_EVALOPERAND_STACKITEM;
    item->opstack_frame = opstack;

    int status = lai_exec_run(code, state);
    if(status)
        lai_panic("lai_exec_run() failed in lai_eval_operand()\n");

    if(state->opstack_ptr != opstack + 1) // This would be an internal error.
        lai_panic("expected exactly one opstack item after operand evaluation\n");
    lai_object_t *result = lai_exec_get_opstack(state, opstack);
    lai_load_operand(state, result, destination);
    lai_exec_pop_opstack(state, 1);
}

// lai_exec_sleep(): Executes a Sleep() opcode
// Param:    void *code - opcode data
// Param:    lai_state_t *state - AML VM state

void lai_exec_sleep(void *code, lai_state_t *state)
{
    state->pc += 2; // Skip EXTOP_PREFIX and SLEEP_OP.

    lai_object_t time = {0};
    lai_eval_operand(&time, state, code);

    if(!time.integer)
        time.integer = 1;

    lai_sleep(time.integer);
}




