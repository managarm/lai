
/*
 * Lux ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
 */

#include <lai/core.h>
#include "aml_opcodes.h"
#include "exec_impl.h"
#include "ns_impl.h"
#include "libc.h"
#include "eval.h"

static int debug_opcodes = 0;

/* ACPI Control Method Execution */
/* Type1Opcode := DefBreak | DefBreakPoint | DefContinue | DefFatal | DefIfElse |
   DefLoad | DefNoop | DefNotify | DefRelease | DefReset | DefReturn |
   DefSignal | DefSleep | DefStall | DefUnload | DefWhile */

static void lai_eval_operand(lai_object_t *destination, lai_state_t *state,
        struct lai_aml_segment *amls, uint8_t *code);

// Prepare the interpreter state for a control method call.
// Param: lai_state_t *state - will store method name and arguments
// Param: lai_nsnode_t *method - identifies the control method

void lai_init_state(lai_state_t *state) {
    memset(state, 0, sizeof(lai_state_t));
    state->stack_ptr = -1;
    state->context_ptr = -1;
}

// Finalize the interpreter state. Frees all memory owned by the state.

void lai_finalize_state(lai_state_t *state) {
    lai_free_object(&state->retvalue);
    for (int i = 0; i < 7; i++) {
        lai_free_object(&state->arg[i]);
    }

    for (int i = 0; i < 8; i++) {
        lai_free_object(&state->local[i]);
    }
}

// Pushes a new item to the opstack and returns it.
static lai_object_t *lai_exec_push_opstack_or_die(lai_state_t *state) {
    if (state->opstack_ptr == 16)
        lai_panic("operand stack overflow");
    lai_object_t *object = &state->opstack[state->opstack_ptr];
    memset(object, 0, sizeof(lai_object_t));
    state->opstack_ptr++;
    return object;
}

// Returns the n-th item from the opstack.
static lai_object_t *lai_exec_get_opstack(lai_state_t *state, int n) {
    if (n >= state->opstack_ptr)
        lai_panic("opstack access out of bounds"); // This is an internal execution error.
    return &state->opstack[n];
}

// Removes n items from the opstack.
static void lai_exec_pop_opstack(lai_state_t *state, int n) {
    for (int k = 0; k < n; k++)
        lai_free_object(&state->opstack[state->opstack_ptr - k - 1]);
    state->opstack_ptr -= n;
}

// Pushes a new item to the execution stack and returns it.
static lai_stackitem_t *lai_exec_push_stack_or_die(lai_state_t *state) {
    state->stack_ptr++;
    if (state->stack_ptr == 16)
        lai_panic("execution engine stack overflow");
    return &state->stack[state->stack_ptr];
}

// Returns the n-th item from the top of the stack.
static lai_stackitem_t *lai_exec_peek_stack(lai_state_t *state, int n) {
    if (state->stack_ptr - n < 0)
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
    while (1) {
        ctx_item = lai_exec_peek_stack(state, j);
        if (!ctx_item)
            break;
        if (ctx_item->kind == LAI_POPULATE_CONTEXT_STACKITEM
                || ctx_item->kind == LAI_METHOD_CONTEXT_STACKITEM)
            break;
        j++;
    }

    state->context_ptr = state->stack_ptr - j;
}

static int lai_compare(lai_object_t *lhs, lai_object_t *rhs) {
    // TODO: Allow comparsions of strings and buffers as in the spec.
    if (lhs->type != LAI_INTEGER || rhs->type != LAI_INTEGER)
        lai_panic("comparsion of object type %d with type %d is not implemented",
                lhs->type, rhs->type);
    return lhs->integer - rhs->integer;
}

static void lai_exec_reduce_node(int opcode, lai_state_t *state, lai_object_t *operands) {
    if (debug_opcodes)
        lai_debug("lai_exec_reduce_node: opcode 0x%02X", opcode);
    switch (opcode) {
        case BYTEFIELD_OP:
        case WORDFIELD_OP:
        case DWORDFIELD_OP:
        case QWORDFIELD_OP: {
            lai_object_t offset = {0};
            lai_load_operand(state, &operands[1], &offset);
            LAI_ENSURE(operands[0].type == LAI_UNRESOLVED_NAME);
            LAI_ENSURE(operands[2].type == LAI_UNRESOLVED_NAME);

            lai_nsnode_t *node = lai_create_nsnode_or_die();
            node->type = LAI_NAMESPACE_BUFFER_FIELD;
            lai_strcpy(node->buffer, operands[0].name);
            lai_strcpy(node->path, operands[2].name);

            switch (opcode) {
                case BYTEFIELD_OP: node->buffer_size = 8; break;
                case WORDFIELD_OP: node->buffer_size = 16; break;
                case DWORDFIELD_OP: node->buffer_size = 32; break;
                case QWORDFIELD_OP: node->buffer_size = 64; break;
            }
            node->buffer_offset = offset.integer;

            lai_install_nsnode(node);
            break;
        }
        default:
            lai_panic("undefined opcode in lai_exec_reduce_node: %02X", opcode);
    }
}

static void lai_exec_reduce_op(int opcode, lai_state_t *state, lai_object_t *operands,
        lai_object_t *reduction_res) {
    if (debug_opcodes)
        lai_debug("lai_exec_reduce_op: opcode 0x%02X", opcode);
    lai_object_t result = {0};
    switch (opcode) {
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

        switch (storage.type) {
            case LAI_STRING_REFERENCE:
                if (n >= lai_exec_string_length(&storage))
                    lai_panic("string Index() out of bounds");
                result.type = LAI_STRING_INDEX;
                result.string_ptr = storage.string_ptr;
                lai_rc_ref(&storage.string_ptr->rc);
                result.integer = n;
                break;
            case LAI_BUFFER_REFERENCE:
                if (n >= lai_exec_buffer_size(&storage))
                    lai_panic("buffer Index() out of bounds");
                result.type = LAI_BUFFER_INDEX;
                result.buffer_ptr = storage.buffer_ptr;
                lai_rc_ref(&storage.buffer_ptr->rc);
                result.integer = n;
                break;
            case LAI_PACKAGE_REFERENCE:
                if (n >= lai_exec_pkg_size(&storage))
                    lai_panic("package Index() out of bounds");
                result.type = LAI_PACKAGE_INDEX;
                result.pkg_ptr = storage.pkg_ptr;
                result.integer = n;
                lai_rc_ref(&storage.pkg_ptr->rc);
                break;
            default:
                lai_panic("Index() is only defined for buffers, strings and packages");
        }
        lai_free_object(&storage);

        lai_store_operand(state, &operands[2], &result);
        break;
    }
    case DEREF_OP:
    {
        lai_object_t ref = {0};
        lai_load_operand(state, &operands[0], &ref);

        switch (ref.type) {
            case LAI_STRING_INDEX:
                result.type = LAI_INTEGER;
                result.integer = lai_exec_string_access(&ref)[ref.integer];
                break;
            case LAI_BUFFER_INDEX: {
                uint8_t *window = lai_exec_buffer_access(&ref);
                result.type = LAI_INTEGER;
                result.integer = window[ref.integer];
                break;
            }
            case LAI_PACKAGE_INDEX:
                lai_exec_pkg_load(&result, &ref, ref.integer);
                break;
            default:
                lai_panic("DeRefOf() is only defined for references");
        }

        break;
    }
    case SIZEOF_OP:
    {
        lai_object_t object = {0};
        lai_load_operand(state, &operands[0], &object);

        switch (object.type) {
            case LAI_STRING:
                result.type = LAI_INTEGER;
                result.integer = lai_exec_string_length(&object);
                break;
            case LAI_BUFFER:
                result.type = LAI_INTEGER;
                result.integer = lai_exec_buffer_size(&object);
                break;
            case LAI_PACKAGE:
                result.type = LAI_INTEGER;
                result.integer = lai_exec_pkg_size(&object);
                break;
            default:
                lai_panic("SizeOf() is only defined for buffers, strings and packages");
        }

        break;
    }
    case (EXTOP_PREFIX << 8) | CONDREF_OP:
    {
        lai_object_t *operand = &operands[0];
        lai_object_t *target = &operands[1];

        // TODO: The resolution code should be shared with REF_OP.
        lai_object_t ref = {0};
        switch (operand->type) {
            case LAI_UNRESOLVED_NAME:
            {
                lai_nsnode_t *handle = lai_exec_resolve(operand->name);
                if (handle) {
                    ref.type = LAI_HANDLE;
                    ref.handle = handle;
                }
                break;
            }
            default:
                lai_panic("CondRefOp() is only defined for names");
        }

        if (ref.type) {
            result.type = LAI_INTEGER;
            result.integer = 1;
            lai_store_operand(state, target, &ref);
        } else {
            result.type = LAI_INTEGER;
            result.integer = 0;
        }

        break;
    }
    case (EXTOP_PREFIX << 8) | ACQUIRE_OP:
    {
        lai_debug("Acquire() is a stub");
        result.type = LAI_INTEGER;
        result.integer = 1;
        break;
    }
    case (EXTOP_PREFIX << 8) | RELEASE_OP:
    {
        lai_debug("Release() is a stub");
        break;
    }

    default:
        lai_panic("undefined opcode in lai_exec_reduce_op: %02X", opcode);
    }

    lai_move_object(reduction_res, &result);
}

// TODO: Make this static.
size_t lai_parse_integer(uint8_t *object, uint64_t *out) {
    uint16_t u16;
    uint32_t u32;
    uint64_t u64;
    switch (*object) {
        case BYTEPREFIX:
            *out = *(object + 1);
            return 2;
        case WORDPREFIX:
            memcpy(&u16, object + 1, sizeof(uint16_t));
            *out = u16;
            return 3;
        case DWORDPREFIX:
            memcpy(&u32, object + 1, sizeof(uint32_t));
            *out = u32;
            return 5;
        case QWORDPREFIX:
            memcpy(&u64, object + 1, sizeof(uint64_t));
            *out = u64;
            return 9;
        default:
            lai_panic("unexpected prefix for lai_parse_integer()");
    }
}

// lai_exec_run(): Internal function, executes actual AML opcodes
// Param:  uint8_t *method - pointer to method opcodes
// Param:  lai_state_t *state - machine state
// Return: int - 0 on success

static int lai_exec_run(struct lai_aml_segment *amls, uint8_t *method, lai_state_t *state) {
    lai_stackitem_t *item;
    while ((item = lai_exec_peek_stack_back(state))) {
        // Package-size encoding (and similar) needs to know the PC of the opcode.
        // If an opcode sequence contains a pkgsize, the sequence generally ends at:
        //     opcode_pc + pkgsize + opcode size.
        int opcode_pc = state->pc;

        // Whether we use the result of an expression or not.
        // If yes, it will be pushed onto the opstack after the expression is computed.
        int exec_result_mode = LAI_EXEC_MODE;

        if (item->kind == LAI_POPULATE_CONTEXT_STACKITEM) {
    		if (state->pc == item->ctx_limit) {
    			lai_exec_pop_stack_back(state);
                lai_exec_update_context(state);
    			continue;
    		}

            if (state->pc > item->ctx_limit) // This would be an interpreter bug.
                lai_panic("namespace population escaped out of code range");
        } else if(item->kind == LAI_METHOD_CONTEXT_STACKITEM) {
    		// ACPI does an implicit Return(0) at the end of a control method.
    		if (state->pc == state->limit) {
    			if (state->opstack_ptr) // This is an internal error.
    				lai_panic("opstack is not empty before return");
    			lai_object_t *result = lai_exec_push_opstack_or_die(state);
    			result->type = LAI_INTEGER;
    			result->integer = 0;

    			lai_exec_pop_stack_back(state);
                lai_exec_update_context(state);
    			continue;
    		}
        } else if (item->kind == LAI_EVALOPERAND_STACKITEM) {
            if (state->opstack_ptr == item->opstack_frame + 1) {
                lai_exec_pop_stack_back(state);
                return 0;
            }

            exec_result_mode = LAI_OBJECT_MODE;
        } else if (item->kind == LAI_PKG_INITIALIZER_STACKITEM) {
            lai_object_t *frame = lai_exec_get_opstack(state, item->opstack_frame);
            lai_object_t *package = &frame[0];
            lai_object_t *initializer = &frame[1];
            if (state->opstack_ptr == item->opstack_frame + 2) {
                if (item->pkg_index == lai_exec_pkg_size(package))
                    lai_panic("package initializer overflows its size");
                LAI_ENSURE(item->pkg_index < lai_exec_pkg_size(package));

                lai_exec_pkg_store(initializer, package, item->pkg_index);
                item->pkg_index++;
                lai_exec_pop_opstack(state, 1);
            }
            LAI_ENSURE(state->opstack_ptr == item->opstack_frame + 1);

            if (state->pc == item->pkg_end) {
                if (item->pkg_result_mode == LAI_EXEC_MODE)
                    lai_exec_pop_opstack(state, 1);
                else
                    LAI_ENSURE(item->pkg_result_mode == LAI_DATA_MODE
                               || item->pkg_result_mode == LAI_OBJECT_MODE);

                lai_exec_pop_stack_back(state);
                continue;
            }

            if (state->pc > item->pkg_end) // This would be an interpreter bug.
                lai_panic("package initializer escaped out of code range");

            exec_result_mode = LAI_DATA_MODE;
        } else if (item->kind == LAI_NODE_STACKITEM) {
            int k = state->opstack_ptr - item->opstack_frame;
            if (!item->node_arg_modes[k]) {
                lai_object_t *operands = lai_exec_get_opstack(state, item->opstack_frame);
                lai_exec_reduce_node(item->node_opcode, state, operands);
                lai_exec_pop_opstack(state, k);

                lai_exec_pop_stack_back(state);
                continue;
            }

            exec_result_mode = item->node_arg_modes[k];
        } else if (item->kind == LAI_OP_STACKITEM) {
            int k = state->opstack_ptr - item->opstack_frame;
//            lai_debug("got %d parameters", k);
            if (!item->op_arg_modes[k]) {
                lai_object_t result = {0};
                lai_object_t *operands = lai_exec_get_opstack(state, item->opstack_frame);
                lai_exec_reduce_op(item->op_opcode, state, operands, &result);
                lai_exec_pop_opstack(state, k);

                if (item->op_result_mode == LAI_OBJECT_MODE
                        || item->op_result_mode == LAI_TARGET_MODE) {
                    lai_object_t *opstack_res = lai_exec_push_opstack_or_die(state);
                    lai_move_object(opstack_res, &result);
                } else {
                    LAI_ENSURE(item->op_result_mode == LAI_EXEC_MODE);
                    lai_free_object(&result);
                }

                lai_exec_pop_stack_back(state);
                continue;
            }

            exec_result_mode = item->op_arg_modes[k];
        } else if (item->kind == LAI_LOOP_STACKITEM) {
            if (state->pc == item->loop_pred) {
                // We are at the beginning of a loop. We check the predicate; if it is false,
                // we jump to the end of the loop and remove the stack item.
                lai_object_t predicate = {0};
                lai_eval_operand(&predicate, state, amls, method);
                if (!predicate.integer) {
                    state->pc = item->loop_end;
                    lai_exec_pop_stack_back(state);
                }
                continue;
            } else if (state->pc == item->loop_end) {
                // Unconditionally jump to the loop's predicate.
                state->pc = item->loop_pred;
                continue;
            }

            if (state->pc > item->loop_end) // This would be an interpreter bug.
                lai_panic("execution escaped out of While() body");
        } else if (item->kind == LAI_COND_STACKITEM) {
            // If the condition wasn't taken, execute the Else() block if it exists
            if (!item->cond_taken) {
                if (method[state->pc] == ELSE_OP) {
                    size_t else_size;
                    state->pc++;
                    state->pc += lai_parse_pkgsize(method + state->pc, &else_size);
                }

                lai_exec_pop_stack_back(state);
                continue;
            }

            // Clean up the execution stack at the end of If().
            if (state->pc == item->cond_end) {
                // Consume a follow-up Else() opcode.
                if (state->pc < state->limit && method[state->pc] == ELSE_OP) {
                    state->pc++;
                    size_t else_size;
                    state->pc += lai_parse_pkgsize(method + state->pc, &else_size);
                    state->pc = opcode_pc + else_size + 1;
                }

                lai_exec_pop_stack_back(state);
                continue;
            }
        } else
            lai_panic("unexpected lai_stackitem_t");

        if (state->pc >= state->limit) // This would be an interpreter bug.
            lai_panic("execution escaped out of code range (PC is 0x%x with limit 0x%x)",
                    state->pc, state->limit);

        lai_stackitem_t *ctx_item = lai_exec_context(state);
        lai_nsnode_t *ctx_handle = ctx_item->ctx_handle;

        if (exec_result_mode == LAI_IMMEDIATE_WORD_MODE) {
            lai_object_t *result = lai_exec_push_opstack_or_die(state);
            result->type = LAI_INTEGER;
            result->integer = (method[state->pc + 1] << 8) | method[state->pc];
            state->pc += 2;
            continue;
        }

        // Process names.
        if (lai_is_name(method[state->pc])) {
            lai_object_t unresolved = {0};
            unresolved.type = LAI_UNRESOLVED_NAME;
            state->pc += lai_resolve_path(ctx_handle, unresolved.name, method + state->pc);

            if (exec_result_mode == LAI_DATA_MODE || exec_result_mode == LAI_TARGET_MODE) {
                if (debug_opcodes)
                    lai_debug("parsing name %s [@ %d]", unresolved.name, opcode_pc);

                lai_object_t *opstack_res = lai_exec_push_opstack_or_die(state);
                lai_move_object(opstack_res, &unresolved);
            } else {
                LAI_ENSURE(exec_result_mode == LAI_OBJECT_MODE
                           || exec_result_mode == LAI_EXEC_MODE);
                lai_nsnode_t *handle = lai_exec_resolve(unresolved.name);
                if (!handle)
                    lai_panic("undefined reference %s in object mode", unresolved.name);

                lai_object_t result = {0};
                if(handle->type == LAI_NAMESPACE_METHOD) {
                    if (debug_opcodes)
                        lai_debug("parsing invocation %s [@ %d]", unresolved.name, opcode_pc);

                    lai_state_t nested_state;
                    lai_init_state(&nested_state);
                    int argc = handle->method_flags & METHOD_ARGC_MASK;
                    for(int i = 0; i < argc; i++)
                        lai_eval_operand(&nested_state.arg[i], state, amls, method);

                    lai_exec_method(handle, &nested_state);
                    lai_move_object(&result, &nested_state.retvalue);
                    lai_finalize_state(&nested_state);
                } else {
                    if (debug_opcodes)
                        lai_debug("parsing name %s [@ %d]", unresolved.name, opcode_pc);

                    lai_load_ns(handle, &result);
                }

                if (exec_result_mode == LAI_OBJECT_MODE) {
                    lai_object_t *opstack_res = lai_exec_push_opstack_or_die(state);
                    lai_move_object(opstack_res, &result);
                }
            }
            lai_free_object(&unresolved);
            continue;
        }

        /* General opcodes */
        int opcode;
        if (method[state->pc] == EXTOP_PREFIX) {
            if (state->pc + 1 == state->limit)
                lai_panic("two-byte opcode on method boundary");
            opcode = (EXTOP_PREFIX << 8) | method[state->pc + 1];
        } else
            opcode = method[state->pc];
        if (debug_opcodes) {
            size_t table_pc = sizeof(acpi_header_t)
                              + (method - amls->table->data)
                              + opcode_pc;
            lai_debug("parsing opcode 0x%02x [0x%x @ %c%c%c%c %d]", opcode, table_pc,
                    amls->table->header.signature[0],
                    amls->table->header.signature[1],
                    amls->table->header.signature[2],
                    amls->table->header.signature[3],
                    amls->index);
        }

        // This switch handles the majority of all opcodes.
        switch (opcode) {
        case NOP_OP:
            state->pc++;
            break;

        case ZERO_OP:
            if (exec_result_mode == LAI_DATA_MODE || exec_result_mode == LAI_OBJECT_MODE) {
                lai_object_t *result = lai_exec_push_opstack_or_die(state);
                result->type = LAI_INTEGER;
                result->integer = 0;
            } else if (exec_result_mode == LAI_TARGET_MODE) {
                // In target mode, ZERO_OP generates a null target and not an integer!
                lai_object_t *result = lai_exec_push_opstack_or_die(state);
                result->type = LAI_NULL_NAME;
            } else {
                lai_warn("Zero() in execution mode has no effect");
                LAI_ENSURE(exec_result_mode == LAI_EXEC_MODE);
            }
            state->pc++;
            break;
        case ONE_OP:
            if (exec_result_mode == LAI_DATA_MODE || exec_result_mode == LAI_OBJECT_MODE) {
                lai_object_t *result = lai_exec_push_opstack_or_die(state);
                result->type = LAI_INTEGER;
                result->integer = 1;
            } else
                LAI_ENSURE(exec_result_mode == LAI_EXEC_MODE);
            state->pc++;
            break;
        case ONES_OP:
            if (exec_result_mode == LAI_DATA_MODE || exec_result_mode == LAI_OBJECT_MODE) {
                lai_object_t *result = lai_exec_push_opstack_or_die(state);
                result->type = LAI_INTEGER;
                result->integer = ~((uint64_t)0);
            } else
                LAI_ENSURE(exec_result_mode == LAI_EXEC_MODE);
            state->pc++;
            break;

        case BYTEPREFIX:
        case WORDPREFIX:
        case DWORDPREFIX:
        case QWORDPREFIX:
        {
            uint64_t integer;
            size_t integer_size = lai_parse_integer(method + state->pc, &integer);
            if (!integer_size)
                lai_panic("failed to parse integer opcode");
            if (exec_result_mode == LAI_DATA_MODE || exec_result_mode == LAI_OBJECT_MODE) {
                lai_object_t *result = lai_exec_push_opstack_or_die(state);
                result->type = LAI_INTEGER;
                result->integer = integer;
            } else
                LAI_ENSURE(exec_result_mode == LAI_EXEC_MODE);
            state->pc += integer_size;
            break;
        }
        case STRINGPREFIX:
        {
            state->pc++;

            size_t n = 0; // Determine the length of null-terminated string.
            while (state->pc + n < state->limit && method[state->pc + n])
                n++;
            if (state->pc + n == state->limit)
                lai_panic("unterminated string in AML code");

            if (exec_result_mode == LAI_DATA_MODE || exec_result_mode == LAI_OBJECT_MODE) {
                lai_object_t *opstack_res = lai_exec_push_opstack_or_die(state);
                if(lai_create_string(opstack_res, n))
                    lai_panic("could not allocate memory for string");
                memcpy(lai_exec_string_access(opstack_res), method + state->pc, n);
            } else
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
            lai_eval_operand(&buffer_size, state, amls, method);

            lai_object_t result = {0};
            if (lai_create_buffer(&result, buffer_size.integer))
                 lai_panic("failed to allocate memory for AML buffer");

            int initial_size = (opcode_pc + encoded_size + 1) - state->pc;
            if (initial_size < 0)
                lai_panic("buffer initializer has negative size");
            if (initial_size > lai_exec_buffer_size(&result))
                lai_panic("buffer initializer overflows buffer");
            memcpy(lai_exec_buffer_access(&result), method + state->pc, initial_size);
            state->pc += initial_size;

            if (exec_result_mode == LAI_DATA_MODE || exec_result_mode == LAI_OBJECT_MODE) {
                lai_object_t *opstack_res = lai_exec_push_opstack_or_die(state);
                lai_move_object(opstack_res, &result);
            } else
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
            if (lai_create_pkg(opstack_pkg, num_ents))
                lai_panic("could not allocate memory for package");
            break;
        }

        case (EXTOP_PREFIX << 8) | SLEEP_OP:
            lai_exec_sleep(amls, method, state);
            break;

        /* A control method can return literally any object */
        /* So we need to take this into consideration */
        case RETURN_OP:
        {
            state->pc++;
            lai_object_t result = {0};
            lai_eval_operand(&result, state, amls, method);

            // Find the last LAI_METHOD_CONTEXT_STACKITEM on the stack.
            int j = 0;
            lai_stackitem_t *method_item;
            while (1) {
                method_item = lai_exec_peek_stack(state, j);
                if (!method_item)
                    lai_panic("Return() outside of control method()");
                if (method_item->kind == LAI_METHOD_CONTEXT_STACKITEM)
                    break;
                // TODO: Verify that we only cross conditions/loops.
                j++;
            }

            // Remove the method stack item and push the return value.
            if (state->opstack_ptr) // This is an internal error.
                lai_panic("opstack is not empty before return");
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
            while (1) {
                loop_item = lai_exec_peek_stack(state, j);
                if (!loop_item)
                    lai_panic("Continue() outside of While()");
                if (loop_item->kind == LAI_LOOP_STACKITEM)
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
            while (1) {
                loop_item = lai_exec_peek_stack(state, j);
                if (!loop_item)
                    lai_panic("Break() outside of While()");
                if (loop_item->kind == LAI_LOOP_STACKITEM)
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
            lai_eval_operand(&predicate, state, amls, method);

            lai_stackitem_t *cond_item = lai_exec_push_stack_or_die(state);
            cond_item->kind = LAI_COND_STACKITEM;
            cond_item->cond_taken = predicate.integer;
            cond_item->cond_end = opcode_pc + if_size + 1;

            if (!cond_item->cond_taken)
                state->pc = cond_item->cond_end;

            break;
        }
        case ELSE_OP:
            lai_panic("Else() outside of If()");
            break;

        // "Simple" objects in the ACPI namespace.
        case NAME_OP:
        {
            state->pc++;

            char path[ACPI_MAX_NAME];
            state->pc += lai_resolve_path(ctx_handle, path, method + state->pc);

            lai_nsnode_t *handle = lai_resolve(path);
            if (!handle) {
                // Create it if it does not already exist.
                handle = lai_create_nsnode_or_die();
                handle->type = LAI_NAMESPACE_NAME;
                lai_strcpy(handle->path, path);
                lai_install_nsnode(handle);
            }

            lai_eval_operand(&handle->object, state, amls, method);
            break;
        }

        // Scope-like objects in the ACPI namespace.
        case SCOPE_OP:
        {
            state->pc++;

            size_t encoded_size;
            state->pc += lai_parse_pkgsize(method + state->pc, &encoded_size);
            char name[ACPI_MAX_NAME];
            state->pc += lai_resolve_path(ctx_handle, name, method + state->pc);

            lai_nsnode_t *node = lai_create_nsnode_or_die();
            node->type = LAI_NAMESPACE_SCOPE;
            lai_strcpy(node->path, name);
            lai_install_nsnode(node);

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
            state->pc += lai_resolve_path(ctx_handle, name, method + state->pc);

            lai_nsnode_t *node = lai_create_nsnode_or_die();
            node->type = LAI_NAMESPACE_DEVICE;
            lai_strcpy(node->path, name);
            lai_install_nsnode(node);

            lai_stackitem_t *item = lai_exec_push_stack_or_die(state);
            item->kind = LAI_POPULATE_CONTEXT_STACKITEM;
            item->ctx_handle = node;
            item->ctx_limit = opcode_pc + encoded_size + 2;
            lai_exec_update_context(state);
            break;
        }
        case (EXTOP_PREFIX << 8) | PROCESSOR:
            state->pc += lai_create_processor(ctx_handle, method + state->pc);
            break;
        case (EXTOP_PREFIX << 8) | POWER_RES:
        {
            state->pc += 2;

            size_t encoded_size;
            state->pc += lai_parse_pkgsize(method + state->pc, &encoded_size);
            char name[ACPI_MAX_NAME];
            state->pc += lai_resolve_path(ctx_handle, name, method + state->pc);

            lai_nsnode_t *node = lai_create_nsnode_or_die();
            node->type = LAI_NAMESPACE_POWER_RES;
            lai_strcpy(node->path, name);
            lai_install_nsnode(node);

//            uint8_t system_level = method[state->pc];
            state->pc++;

//            uint16_t resource_order = *(uint16_t*)&method[state->pc];
            state->pc += 2;

            lai_stackitem_t *item = lai_exec_push_stack_or_die(state);
            item->kind = LAI_POPULATE_CONTEXT_STACKITEM;
            item->ctx_handle = node;
            item->ctx_limit = opcode_pc + encoded_size + 2;
            lai_exec_update_context(state);
            break;
        }
        case (EXTOP_PREFIX << 8) | THERMALZONE:
        {
            state->pc += 2;

            size_t encoded_size;
            state->pc += lai_parse_pkgsize(method + state->pc, &encoded_size);
            char name[ACPI_MAX_NAME];
            state->pc += lai_resolve_path(ctx_handle, name, method + state->pc);

            lai_nsnode_t *node = lai_create_nsnode_or_die();
            node->type = LAI_NAMESPACE_THERMALZONE;
            lai_strcpy(node->path, name);
            lai_install_nsnode(node);

            lai_stackitem_t *item = lai_exec_push_stack_or_die(state);
            item->kind = LAI_POPULATE_CONTEXT_STACKITEM;
            item->ctx_handle = node;
            item->ctx_limit = opcode_pc + encoded_size + 2;
            lai_exec_update_context(state);
            break;
        }

        // Leafs in the ACPI namespace.
        case METHOD_OP:
            state->pc += lai_create_method(ctx_handle, amls, method + state->pc);
            break;
        case ALIAS_OP:
            state->pc += lai_create_alias(ctx_handle, method + state->pc);
            break;
        case BYTEFIELD_OP:
        case WORDFIELD_OP:
        case DWORDFIELD_OP:
        case QWORDFIELD_OP:
        {
            lai_stackitem_t *node_item = lai_exec_push_stack_or_die(state);
            node_item->kind = LAI_NODE_STACKITEM;
            node_item->node_opcode = opcode;
            node_item->opstack_frame = state->opstack_ptr;
            node_item->node_arg_modes[0] = LAI_DATA_MODE;
            node_item->node_arg_modes[1] = LAI_OBJECT_MODE;
            node_item->node_arg_modes[2] = LAI_DATA_MODE;
            node_item->node_arg_modes[3] = 0;
            state->pc++;
            break;
        }
        case (EXTOP_PREFIX << 8) | MUTEX:
            state->pc += lai_create_mutex(ctx_handle, method + state->pc);
            break;
        case (EXTOP_PREFIX << 8) | EVENT:
        {
            state->pc += 2;
            lai_nsnode_t* node = lai_create_nsnode_or_die();
            node->type = LAI_NAMESPACE_EVENT;
            state->pc += lai_resolve_path(ctx_handle, node->path, method + state->pc);
            break;
        }
        case (EXTOP_PREFIX << 8) | OPREGION:
        {
            state->pc += 2;
            char name[ACPI_MAX_NAME];
            state->pc += lai_resolve_path(ctx_handle, name, method + state->pc);

            // First byte identifies the address space (memory, I/O ports, PCI, etc.).
            int address_space = method[state->pc];
            state->pc++;

            // Now, parse the offset and length of the opregion.
            lai_object_t disp = {0};
            lai_object_t length = {0};
            lai_eval_operand(&disp, state, amls, method);
            lai_eval_operand(&length, state, amls, method);

            lai_nsnode_t *node = lai_create_nsnode_or_die();
            lai_strcpy(node->path, name);
            node->op_address_space = address_space;
            node->op_base = disp.integer;
            node->op_length = length.integer;
            lai_install_nsnode(node);
            break;
        }
        case (EXTOP_PREFIX << 8) | FIELD:
            state->pc += lai_create_field(ctx_handle, method + state->pc);
            break;
        case (EXTOP_PREFIX << 8) | INDEXFIELD:
            state->pc += lai_create_indexfield(ctx_handle, method + state->pc);
            break;

        case ARG0_OP:
        case ARG1_OP:
        case ARG2_OP:
        case ARG3_OP:
        case ARG4_OP:
        case ARG5_OP:
        case ARG6_OP:
        {
            if (exec_result_mode == LAI_OBJECT_MODE
                    || exec_result_mode == LAI_TARGET_MODE) {
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
                    || exec_result_mode == LAI_TARGET_MODE) {
                lai_object_t *result = lai_exec_push_opstack_or_die(state);
                result->type = LAI_LOCAL_NAME;
                result->index = opcode - LOCAL0_OP;
            }
            state->pc++;
            break;
        }

        case (EXTOP_PREFIX << 8) | DEBUG_OP:
        {
            if(exec_result_mode == LAI_OBJECT_MODE
                    || exec_result_mode == LAI_TARGET_MODE) {
                lai_object_t *result = lai_exec_push_opstack_or_die(state);
                result->type = LAI_DEBUG_NAME;
            }
            state->pc += 2;
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
            op_item->op_arg_modes[0] = LAI_OBJECT_MODE;
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
        case (EXTOP_PREFIX << 8) | ACQUIRE_OP:
        {
            lai_stackitem_t *op_item = lai_exec_push_stack_or_die(state);
            op_item->kind = LAI_OP_STACKITEM;
            op_item->op_opcode = opcode;
            op_item->opstack_frame = state->opstack_ptr;
            op_item->op_arg_modes[0] = LAI_DATA_MODE;
            op_item->op_arg_modes[1] = LAI_IMMEDIATE_WORD_MODE;
            op_item->op_arg_modes[2] = 0;
            op_item->op_result_mode = exec_result_mode;
            state->pc += 2;
            break;
        }
        case (EXTOP_PREFIX << 8) | RELEASE_OP:
        {
            lai_stackitem_t *op_item = lai_exec_push_stack_or_die(state);
            op_item->kind = LAI_OP_STACKITEM;
            op_item->op_opcode = opcode;
            op_item->opstack_frame = state->opstack_ptr;
            op_item->op_arg_modes[0] = LAI_DATA_MODE;
            op_item->op_arg_modes[1] = 0;
            op_item->op_result_mode = exec_result_mode;
            state->pc += 2;
            break;
        }

        default:
            lai_panic("unexpected opcode in lai_exec_run(), sequence %02X %02X %02X %02X",
                    method[state->pc + 0], method[state->pc + 1],
                    method[state->pc + 2], method[state->pc + 3]);
        }
    }

    return 0;
}

int lai_populate(lai_nsnode_t *parent, struct lai_aml_segment *amls, lai_state_t *state) {
    size_t size = amls->table->header.length - sizeof(acpi_header_t);

    lai_stackitem_t *item = lai_exec_push_stack_or_die(state);
    item->kind = LAI_POPULATE_CONTEXT_STACKITEM;
    item->ctx_handle = parent;
    item->ctx_limit = size;
    lai_exec_update_context(state);

    state->pc = 0;
    state->limit = size;
    int status = lai_exec_run(amls, amls->table->data, state);
    if (status)
        lai_panic("lai_exec_run() failed in lai_populate()");
    return 0;
}

// lai_exec_method(): Finds and executes a control method
// Param:    lai_nsnode_t *method - method to execute
// Param:    lai_state_t *state - execution engine state
// Return:    int - 0 on success

int lai_exec_method(lai_nsnode_t *method, lai_state_t *state) {
    // Check for OS-defined methods.
    if (method->method_override)
        return method->method_override(state->arg, &state->retvalue);
    LAI_ENSURE(method->amls);

    // Okay, by here it's a real method.
    //lai_debug("execute control method %s", method->path);
    lai_stackitem_t *item = lai_exec_push_stack_or_die(state);
    item->kind = LAI_METHOD_CONTEXT_STACKITEM;
    item->ctx_handle = method;
    lai_exec_update_context(state);

    state->pc = 0;
    state->limit = method->size;
    int status = lai_exec_run(method->amls, method->pointer, state);
    if (status)
        return status;

    /*lai_debug("%s finished, ", method->path);

    if(state->retvalue.type == LAI_INTEGER)
        lai_debug("return value is integer: %d", state->retvalue.integer);
    else if(state->retvalue.type == LAI_STRING)
        lai_debug("return value is string: '%s'", state->retvalue.string);
    else if(state->retvalue.type == LAI_PACKAGE)
        lai_debug("return value is package");
    else if(state->retvalue.type == LAI_BUFFER)
        lai_debug("return value is buffer");*/

    if (state->opstack_ptr != 1) // This would be an internal error.
        lai_panic("expected exactly one return value after method invocation");
    lai_object_t *result = lai_exec_get_opstack(state, 0);
    lai_move_object(&state->retvalue, result);
    lai_exec_pop_opstack(state, 1);
    return 0;
}

// lai_eval_node(): Evaluates a named AML object.
// Param:    lai_nsnode_t *handle - node to evaluate
// Param:    lai_state_t *state - execution engine state
// Return:   int - 0 on success

int lai_eval_node(lai_nsnode_t *handle, lai_state_t *state) {
    while (handle->type == LAI_NAMESPACE_ALIAS) {
        handle = lai_resolve(handle->alias);
        if (!handle)
            return 1;
    }

    if (handle->type == LAI_NAMESPACE_NAME) {
        lai_copy_object(&state->retvalue, &handle->object);
        return 0;
    } else if (handle->type == LAI_NAMESPACE_METHOD) {
        return lai_exec_method(handle, state);
    }

    return 1;
}

// Evaluates an AML expression recursively.
// TODO: Eventually, we want to remove this function. However, this requires refactoring
//       lai_exec_run() to avoid all kinds of recursion.

static void lai_eval_operand(lai_object_t *destination, lai_state_t *state,
        struct lai_aml_segment *amls, uint8_t *code) {
    int opstack = state->opstack_ptr;

    lai_stackitem_t *item = lai_exec_push_stack_or_die(state);
    item->kind = LAI_EVALOPERAND_STACKITEM;
    item->opstack_frame = opstack;

    int status = lai_exec_run(amls, code, state);
    if (status)
        lai_panic("lai_exec_run() failed in lai_eval_operand()");

    if (state->opstack_ptr != opstack + 1) // This would be an internal error.
        lai_panic("expected exactly one opstack item after operand evaluation");
    lai_object_t *result = lai_exec_get_opstack(state, opstack);
    lai_load_operand(state, result, destination);
    lai_exec_pop_opstack(state, 1);
}

// lai_exec_sleep(): Executes a Sleep() opcode
// Param:    void *code - opcode data
// Param:    lai_state_t *state - AML VM state

void lai_exec_sleep(struct lai_aml_segment *amls, void *code, lai_state_t *state) {
    state->pc += 2; // Skip EXTOP_PREFIX and SLEEP_OP.

    lai_object_t time = {0};
    lai_eval_operand(&time, state, amls, code);

    if (!time.integer)
        time.integer = 1;

    if (!laihost_sleep)
        lai_panic("host does not provide timer functions required by Sleep()");
    laihost_sleep(time.integer);
}

void lai_enable_tracing(int enable) {
    debug_opcodes = enable;
}
