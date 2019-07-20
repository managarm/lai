
/*
 * Lightweight ACPI Implementation
 * Copyright (C) 2018-2019 the lai authors
 */

#include <lai/core.h>
#include "aml_opcodes.h"
#include "exec_impl.h"
#include "ns_impl.h"
#include "libc.h"
#include "eval.h"
#include "util-list.h"
#include "util-macros.h"

static int debug_opcodes = 0;
static int debug_stack = 0;

static int lai_exec_process(lai_state_t *state);
static int lai_exec_parse(int parse_mode, lai_state_t *state);

// Prepare the interpreter state for a control method call.
// Param: lai_state_t *state - will store method name and arguments
// Param: lai_nsnode_t *method - identifies the control method

void lai_init_state(lai_state_t *state) {
    memset(state, 0, sizeof(lai_state_t));
    state->ctxstack_base = state->small_ctxstack;
    state->blkstack_base = state->small_blkstack;
    state->stack_base = state->small_stack;
    state->opstack_base = state->small_opstack;
    state->ctxstack_capacity = LAI_SMALL_CTXSTACK_SIZE;
    state->blkstack_capacity = LAI_SMALL_BLKSTACK_SIZE;
    state->stack_capacity = LAI_SMALL_STACK_SIZE;
    state->opstack_capacity = LAI_SMALL_OPSTACK_SIZE;
    state->ctxstack_ptr = -1;
    state->blkstack_ptr = -1;
    state->stack_ptr = -1;
}

// Finalize the interpreter state. Frees all memory owned by the state.
void lai_finalize_state(lai_state_t *state) {
    while (state->ctxstack_ptr >= 0)
        lai_exec_pop_ctxstack_back(state);
    while (state->blkstack_ptr >= 0)
        lai_exec_pop_blkstack_back(state);
    while (state->stack_ptr >= 0)
        lai_exec_pop_stack_back(state);
    lai_exec_pop_opstack(state, state->opstack_ptr);

    if (state->ctxstack_base != state->small_ctxstack)
        laihost_free(state->ctxstack_base);
    if (state->blkstack_base != state->small_blkstack)
        laihost_free(state->blkstack_base);
    if (state->stack_base != state->small_stack)
        laihost_free(state->stack_base);
    if (state->opstack_base != state->small_opstack)
        laihost_free(state->opstack_base);
}

static int lai_compare(lai_variable_t *lhs, lai_variable_t *rhs) {
    // TODO: Allow comparsions of strings and buffers as in the spec.
    if (lhs->type != LAI_INTEGER || rhs->type != LAI_INTEGER)
        lai_panic("comparsion of object type %d with type %d is not implemented",
                lhs->type, rhs->type);
    return lhs->integer - rhs->integer;
}

static void lai_exec_reduce_node(int opcode, lai_state_t *state, struct lai_operand *operands,
        lai_nsnode_t *ctx_handle) {
    if (debug_opcodes)
        lai_debug("lai_exec_reduce_node: opcode 0x%02X", opcode);
    switch (opcode) {
        case NAME_OP: {
            lai_variable_t object = {0};
            lai_exec_get_objectref(state, &operands[1], &object);
            LAI_ENSURE(operands[0].tag == LAI_UNRESOLVED_NAME);

            struct lai_amlname amln;
            lai_amlname_parse(&amln, operands[0].unres_aml);

            lai_nsnode_t *node = lai_create_nsnode_or_die();
            node->type = LAI_NAMESPACE_NAME;
            lai_do_resolve_new_node(node, ctx_handle, &amln);
            lai_var_move(&node->object, &object);
            lai_install_nsnode(node);
            struct lai_ctxitem *ctxitem = lai_exec_peek_ctxstack_back(state);
            if (ctxitem->invocation)
                lai_list_link(&ctxitem->invocation->per_method_list, &node->per_method_item);
            break;
        }
        case BYTEFIELD_OP:
        case WORDFIELD_OP:
        case DWORDFIELD_OP:
        case QWORDFIELD_OP: {
            lai_variable_t offset = {0};
            lai_exec_get_integer(state, &operands[1], &offset);
            LAI_ENSURE(operands[0].tag == LAI_UNRESOLVED_NAME);
            LAI_ENSURE(operands[2].tag == LAI_UNRESOLVED_NAME);

            struct lai_amlname buffer_amln;
            struct lai_amlname node_amln;
            lai_amlname_parse(&buffer_amln, operands[0].unres_aml);
            lai_amlname_parse(&node_amln, operands[2].unres_aml);

            lai_nsnode_t *node = lai_create_nsnode_or_die();
            node->type = LAI_NAMESPACE_BUFFER_FIELD;
            lai_do_resolve_new_node(node, operands[2].unres_ctx_handle, &node_amln);

            lai_nsnode_t *buffer_node = lai_do_resolve(operands[0].unres_ctx_handle, &buffer_amln);
            if (!buffer_node)
                lai_panic("could not resolve buffer of buffer field");
            node->bf_node = buffer_node;

            switch (opcode) {
                case BYTEFIELD_OP: node->bf_size = 8; break;
                case WORDFIELD_OP: node->bf_size = 16; break;
                case DWORDFIELD_OP: node->bf_size = 32; break;
                case QWORDFIELD_OP: node->bf_size = 64; break;
            }
            node->bf_offset = offset.integer * 8;

            lai_install_nsnode(node);
            struct lai_ctxitem *ctxitem = lai_exec_peek_ctxstack_back(state);
            if (ctxitem->invocation)
                lai_list_link(&ctxitem->invocation->per_method_list, &node->per_method_item);
            break;
        }
        case (EXTOP_PREFIX << 8) | OPREGION: {
            lai_variable_t base = {0};
            lai_variable_t size = {0};
            lai_exec_get_integer(state, &operands[2], &base);
            lai_exec_get_integer(state, &operands[3], &size);
            LAI_ENSURE(operands[0].tag == LAI_UNRESOLVED_NAME);
            LAI_ENSURE(operands[1].tag == LAI_OPERAND_OBJECT
                       && operands[1].object.type == LAI_INTEGER);

            struct lai_amlname amln;
            lai_amlname_parse(&amln, operands[0].unres_aml);

            lai_nsnode_t *node = lai_create_nsnode_or_die();
            lai_do_resolve_new_node(node, ctx_handle, &amln);
            node->op_address_space = operands[1].object.integer;
            node->op_base = base.integer;
            node->op_length = size.integer;

            lai_install_nsnode(node);
            struct lai_ctxitem *ctxitem = lai_exec_peek_ctxstack_back(state);
            if (ctxitem->invocation)
                lai_list_link(&ctxitem->invocation->per_method_list, &node->per_method_item);
            break;
        }
        default:
            lai_panic("undefined opcode in lai_exec_reduce_node: %02X", opcode);
    }
}

static void lai_exec_reduce_op(int opcode, lai_state_t *state, struct lai_operand *operands,
        lai_variable_t *reduction_res) {
    if (debug_opcodes)
        lai_debug("lai_exec_reduce_op: opcode 0x%02X", opcode);
    lai_variable_t result = {0};
    switch (opcode) {
    case STORE_OP: {
        lai_variable_t objectref = {0};
        lai_variable_t out = {0};
        lai_exec_get_objectref(state, &operands[0], &objectref);

        lai_obj_clone(&result, &objectref);

        // Store a copy to the target operand.
        // TODO: Verify that we HAVE to make a copy.
        lai_obj_clone(&out, &result);
        lai_store(state, &operands[1], &result);

        lai_var_finalize(&objectref);
        lai_var_finalize(&out);
        break;
    }
    case NOT_OP:
    {
        lai_variable_t operand = {0};
        lai_exec_get_integer(state, operands, &operand);

        result.type = LAI_INTEGER;
        result.integer = ~operand.integer;
        lai_store(state, &operands[1], &result);
        break;
    }
    case ADD_OP:
    {
        lai_variable_t lhs = {0};
        lai_variable_t rhs = {0};
        lai_exec_get_integer(state, &operands[0], &lhs);
        lai_exec_get_integer(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = lhs.integer + rhs.integer;
        lai_store(state, &operands[2], &result);
        break;
    }
    case SUBTRACT_OP:
    {
        lai_variable_t lhs = {0};
        lai_variable_t rhs = {0};
        lai_exec_get_integer(state, &operands[0], &lhs);
        lai_exec_get_integer(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = lhs.integer - rhs.integer;
        lai_store(state, &operands[2], &result);
        break;
    }
    case MULTIPLY_OP:
    {
        lai_variable_t lhs = {0};
        lai_variable_t rhs = {0};
        lai_exec_get_integer(state, &operands[0], &lhs);
        lai_exec_get_integer(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = lhs.integer * rhs.integer;
        lai_store(state, &operands[2], &result);
        break;
    }
    case AND_OP:
    {
        lai_variable_t lhs = {0};
        lai_variable_t rhs = {0};
        lai_exec_get_integer(state, &operands[0], &lhs);
        lai_exec_get_integer(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = lhs.integer & rhs.integer;
        lai_store(state, &operands[2], &result);
        break;
    }
    case OR_OP:
    {
        lai_variable_t lhs = {0};
        lai_variable_t rhs = {0};
        lai_exec_get_integer(state, &operands[0], &lhs);
        lai_exec_get_integer(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = lhs.integer | rhs.integer;
        lai_store(state, &operands[2], &result);
        break;
    }
    case XOR_OP:
    {
        lai_variable_t lhs = {0};
        lai_variable_t rhs = {0};
        lai_exec_get_integer(state, &operands[0], &lhs);
        lai_exec_get_integer(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = lhs.integer ^ rhs.integer;
        lai_store(state, &operands[2], &result);
        break;
    }
    case SHL_OP:
    {
        lai_variable_t lhs = {0};
        lai_variable_t rhs = {0};
        lai_exec_get_integer(state, &operands[0], &lhs);
        lai_exec_get_integer(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = lhs.integer << rhs.integer;
        lai_store(state, &operands[2], &result);
        break;
    }
    case SHR_OP:
    {
        lai_variable_t lhs = {0};
        lai_variable_t rhs = {0};
        lai_exec_get_integer(state, &operands[0], &lhs);
        lai_exec_get_integer(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = lhs.integer >> rhs.integer;
        lai_store(state, &operands[2], &result);
        break;
    }
    case DIVIDE_OP:
    {
        lai_variable_t lhs = {0};
        lai_variable_t rhs = {0};
        lai_exec_get_integer(state, &operands[0], &lhs);
        lai_exec_get_integer(state, &operands[1], &rhs);

        lai_variable_t mod = {0};
        lai_variable_t div = {0};
        mod.type = LAI_INTEGER;
        div.type = LAI_INTEGER;
        mod.integer = lhs.integer % rhs.integer;
        div.integer = lhs.integer / rhs.integer;
        lai_store(state, &operands[2], &mod);
        lai_store(state, &operands[3], &div);
        break;
    }
    case INCREMENT_OP:
        lai_exec_get_integer(state, operands, &result);
        result.integer++;
        lai_store(state, operands, &result);
        break;
    case DECREMENT_OP:
        lai_exec_get_integer(state, operands, &result);
        result.integer--;
        lai_store(state, operands, &result);
        break;
    case LNOT_OP:
    {
        lai_variable_t operand = {0};
        lai_exec_get_integer(state, operands, &operand);

        result.type = LAI_INTEGER;
        result.integer = !operand.integer;
        break;
    }
    case LAND_OP:
    {
        lai_variable_t lhs = {0};
        lai_variable_t rhs = {0};
        lai_exec_get_integer(state, &operands[0], &lhs);
        lai_exec_get_integer(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = lhs.integer && rhs.integer;
        break;
    }
    case LOR_OP:
    {
        lai_variable_t lhs = {0};
        lai_variable_t rhs = {0};
        lai_exec_get_integer(state, &operands[0], &lhs);
        lai_exec_get_integer(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = lhs.integer || rhs.integer;
        break;
    }
    case LEQUAL_OP:
    {
        lai_variable_t lhs = {0};
        lai_variable_t rhs = {0};
        lai_exec_get_integer(state, &operands[0], &lhs);
        lai_exec_get_integer(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = !lai_compare(&lhs, &rhs);
        break;
    }
    case LLESS_OP:
    {
        lai_variable_t lhs = {0};
        lai_variable_t rhs = {0};
        lai_exec_get_integer(state, &operands[0], &lhs);
        lai_exec_get_integer(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = lai_compare(&lhs, &rhs) < 0;
        break;
    }
    case LGREATER_OP:
    {
        lai_variable_t lhs = {0};
        lai_variable_t rhs = {0};
        lai_exec_get_integer(state, &operands[0], &lhs);
        lai_exec_get_integer(state, &operands[1], &rhs);

        result.type = LAI_INTEGER;
        result.integer = lai_compare(&lhs, &rhs) > 0;
        break;
    }
    case INDEX_OP:
    {
        lai_variable_t object = {0};
        lai_variable_t index = {0};
        lai_exec_get_objectref(state, &operands[0], &object);
        lai_exec_get_integer(state, &operands[1], &index);
        int n = index.integer;

        switch (object.type) {
            case LAI_STRING:
                if (n >= lai_exec_string_length(&object))
                    lai_panic("string Index() out of bounds");
                result.type = LAI_STRING_INDEX;
                result.string_ptr = object.string_ptr;
                lai_rc_ref(&object.string_ptr->rc);
                result.integer = n;
                break;
            case LAI_BUFFER:
                if (n >= lai_exec_buffer_size(&object))
                    lai_panic("buffer Index() out of bounds");
                result.type = LAI_BUFFER_INDEX;
                result.buffer_ptr = object.buffer_ptr;
                lai_rc_ref(&object.buffer_ptr->rc);
                result.integer = n;
                break;
            case LAI_PACKAGE:
                if (n >= lai_exec_pkg_size(&object))
                    lai_panic("package Index() out of bounds");
                result.type = LAI_PACKAGE_INDEX;
                result.pkg_ptr = object.pkg_ptr;
                result.integer = n;
                lai_rc_ref(&object.pkg_ptr->rc);
                break;
            default:
                lai_panic("Index() is only defined for buffers, strings and packages"
                        " but object of type %d was given", object.type);
        }
        lai_var_finalize(&object);

        // TODO: Verify that we do NOT have to make a copy.
        lai_store(state, &operands[2], &result);
        break;
    }
    case DEREF_OP:
    {
        lai_variable_t ref = {0};
        lai_exec_get_objectref(state, &operands[0], &ref);

        switch (ref.type) {
            case LAI_STRING_INDEX: {
                char *window = ref.string_ptr->content;
                result.type = LAI_INTEGER;
                result.integer = window[ref.integer];
                break;
            }
            case LAI_BUFFER_INDEX: {
                uint8_t *window = ref.buffer_ptr->content;
                result.type = LAI_INTEGER;
                result.integer = window[ref.integer];
                break;
            }
            case LAI_PACKAGE_INDEX:
                lai_exec_pkg_var_load(&result, ref.pkg_ptr, ref.integer);
                break;
            default:
                lai_panic("DeRefOf() is only defined for references");
        }

        break;
    }
    case SIZEOF_OP:
    {
        lai_variable_t object = {0};
        lai_exec_get_objectref(state, &operands[0], &object);

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
        struct lai_operand *operand = &operands[0];
        struct lai_operand *target = &operands[1];

        // TODO: The resolution code should be shared with REF_OP.
        lai_variable_t ref = {0};
        switch (operand->tag) {
            case LAI_UNRESOLVED_NAME:
            {
                struct lai_amlname amln;
                lai_amlname_parse(&amln, operand->unres_aml);

                lai_nsnode_t *handle = lai_do_resolve(operand->unres_ctx_handle, &amln);
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
            lai_store(state, target, &ref);
        } else {
            result.type = LAI_INTEGER;
            result.integer = 0;
        }

        break;
    }
    case (EXTOP_PREFIX << 8) | SLEEP_OP: {
        if (!laihost_sleep)
            lai_panic("host does not provide timer functions required by Sleep()");

        lai_variable_t time = {0};
        lai_exec_get_integer(state, &operands[0], &time);

        if (!time.integer)
            time.integer = 1;
        laihost_sleep(time.integer);
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

    lai_var_move(reduction_res, &result);
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

// lai_exec_run(): This is the main AML interpreter function.
static int lai_exec_run(lai_state_t *state) {
    while (lai_exec_peek_stack_back(state)) {
        if (debug_stack)
            for (int i = 0; ; i++) {
                lai_stackitem_t *trace_item = lai_exec_peek_stack(state, i);
                if (!trace_item)
                    break;
                switch (trace_item->kind) {
                    case LAI_OP_STACKITEM:
                        lai_debug("stack item %d is of type %d, opcode is 0x%x",
                                i, trace_item->kind, trace_item->op_opcode);
                        break;
                    default:
                        lai_debug("stack item %d is of type %d", i, trace_item->kind);
                }
            }

        int e;
        if((e = lai_exec_process(state)))
            return e;
    }

    return 0;
}

// Process the top-most item of the execution stack.
static int lai_exec_process(lai_state_t *state) {
    lai_stackitem_t *item = lai_exec_peek_stack_back(state);
    struct lai_ctxitem *ctxitem = lai_exec_peek_ctxstack_back(state);
    struct lai_blkitem *block = lai_exec_peek_blkstack_back(state);
    LAI_ENSURE(ctxitem);
    LAI_ENSURE(block);
    struct lai_aml_segment *amls = ctxitem->amls;
    uint8_t *method = ctxitem->code;
    lai_nsnode_t *ctx_handle = ctxitem->handle;
    struct lai_invocation *invocation = ctxitem->invocation;

    // Package-size encoding (and similar) needs to know the PC of the opcode.
    // If an opcode sequence contains a pkgsize, the sequence generally ends at:
    //     opcode_pc + pkgsize + opcode size.
    int opcode_pc = block->pc;

    // PC relative to the start of the table.
    // This matches the offsets in the output of 'iasl -l'.
    size_t table_pc = sizeof(acpi_header_t)
                      + (method - amls->table->data)
                      + opcode_pc;
    size_t table_limit_pc = sizeof(acpi_header_t)
                      + (method - amls->table->data)
                      + block->limit;

    // This would be an interpreter bug.
    if (block->pc > block->limit)
        lai_panic("execution escaped out of code range"
                  " [0x%x, limit 0x%x])",
                  table_pc, table_limit_pc);

    if (item->kind == LAI_POPULATE_STACKITEM) {
        if (block->pc == block->limit) {
            lai_exec_pop_blkstack_back(state);
            lai_exec_pop_ctxstack_back(state);
            lai_exec_pop_stack_back(state);
            return 0;
        } else {
            return lai_exec_parse(LAI_EXEC_MODE, state);
        }
    } else if(item->kind == LAI_METHOD_STACKITEM) {
        // ACPI does an implicit Return(0) at the end of a control method.
        if (block->pc == block->limit) {
            if (lai_exec_reserve_opstack(state))
                return 1;

            if (state->opstack_ptr) // This is an internal error.
                lai_panic("opstack is not empty before return");
            if (item->mth_want_result) {
                struct lai_operand *result = lai_exec_push_opstack(state);
                result->tag = LAI_OPERAND_OBJECT;
                result->object.type = LAI_INTEGER;
                result->object.integer = 0;
            }

            // Clean up all per-method namespace nodes.
            struct lai_list_item *pmi;
            while ((pmi = lai_list_first(&invocation->per_method_list))) {
                lai_nsnode_t *node = LAI_CONTAINER_OF(pmi, lai_nsnode_t, per_method_item);
                lai_uninstall_nsnode(node);
                lai_list_unlink(&node->per_method_item);
            }

            lai_exec_pop_blkstack_back(state);
            lai_exec_pop_ctxstack_back(state);
            lai_exec_pop_stack_back(state);
            return 0;
        } else {
            return lai_exec_parse(LAI_EXEC_MODE, state);
        }
    } else if (item->kind == LAI_BUFFER_STACKITEM) {
        int k = state->opstack_ptr - item->opstack_frame;
        LAI_ENSURE(k <= 1);
        if(k == 1) {
            LAI_CLEANUP_VAR lai_variable_t size = LAI_VAR_INITIALIZER;
            struct lai_operand *operand = lai_exec_get_opstack(state, item->opstack_frame);
            lai_exec_get_objectref(state, operand, &size);
            lai_exec_pop_opstack_back(state);

            // Note that not all elements of the buffer need to be initialized.
            LAI_CLEANUP_VAR lai_variable_t result = LAI_VAR_INITIALIZER;
            if (lai_create_buffer(&result, size.integer))
                 lai_panic("failed to allocate memory for AML buffer");

            int initial_size = block->limit - block->pc;
            if (initial_size < 0)
                lai_panic("buffer initializer has negative size");
            if (initial_size > lai_exec_buffer_size(&result))
                lai_panic("buffer initializer overflows buffer");
            memcpy(lai_exec_buffer_access(&result), method + block->pc, initial_size);

            if (item->buf_want_result) {
                // Note: there is no need to reserve() as we pop an operand above.
                struct lai_operand *opstack_res = lai_exec_push_opstack(state);
                opstack_res->tag = LAI_OPERAND_OBJECT;
                lai_var_move(&opstack_res->object, &result);
            }

            lai_exec_pop_blkstack_back(state);
            lai_exec_pop_stack_back(state);
            return 0;
        } else {
            return lai_exec_parse(LAI_OBJECT_MODE, state);
        }
    } else if (item->kind == LAI_PACKAGE_STACKITEM) {
        struct lai_operand *frame = lai_exec_get_opstack(state, item->opstack_frame);

        if (state->opstack_ptr == item->opstack_frame + 2) {
            struct lai_operand *package = &frame[0];
            LAI_ENSURE(package->tag == LAI_OPERAND_OBJECT);
            struct lai_operand *initializer = &frame[1];
            LAI_ENSURE(initializer->tag == LAI_OPERAND_OBJECT);

            if (item->pkg_index == lai_exec_pkg_size(&package->object))
                lai_panic("package initializer overflows its size");
            LAI_ENSURE(item->pkg_index < lai_exec_pkg_size(&package->object));

            lai_exec_pkg_store(&initializer->object, &package->object, item->pkg_index);
            item->pkg_index++;
            lai_exec_pop_opstack_back(state);
        }
        LAI_ENSURE(state->opstack_ptr == item->opstack_frame + 1);

        if (block->pc == block->limit) {
            if (!item->pkg_want_result)
                lai_exec_pop_opstack_back(state);

            lai_exec_pop_blkstack_back(state);
            lai_exec_pop_stack_back(state);
            return 0;
        } else {
            return lai_exec_parse(LAI_DATA_MODE, state);
        }
    } else if (item->kind == LAI_NODE_STACKITEM) {
        int k = state->opstack_ptr - item->opstack_frame;
        if (!item->node_arg_modes[k]) {
            struct lai_operand *operands = lai_exec_get_opstack(state, item->opstack_frame);
            lai_exec_reduce_node(item->node_opcode, state, operands, ctx_handle);
            lai_exec_pop_opstack(state, k);

            lai_exec_pop_stack_back(state);
            return 0;
        } else {
            return lai_exec_parse(item->node_arg_modes[k], state);
        }
    } else if (item->kind == LAI_OP_STACKITEM) {
        int k = state->opstack_ptr - item->opstack_frame;
//            lai_debug("got %d parameters", k);
        if (!item->op_arg_modes[k]) {
            if (lai_exec_reserve_opstack(state))
                return 1;

            lai_variable_t result = {0};
            struct lai_operand *operands = lai_exec_get_opstack(state, item->opstack_frame);
            lai_exec_reduce_op(item->op_opcode, state, operands, &result);
            lai_exec_pop_opstack(state, k);

            if (item->op_want_result) {
                struct lai_operand *opstack_res = lai_exec_push_opstack(state);
                opstack_res->tag = LAI_OPERAND_OBJECT;
                lai_var_move(&opstack_res->object, &result);
            } else {
                lai_var_finalize(&result);
            }

            lai_exec_pop_stack_back(state);
            return 0;
        } else {
            return lai_exec_parse(item->op_arg_modes[k], state);
        }
    } else if (item->kind == LAI_INVOKE_STACKITEM) {
        int argc = item->ivk_argc;
        int want_result = item->ivk_want_result;
        int k = state->opstack_ptr - item->opstack_frame;
        LAI_ENSURE(k <= argc + 1);
        if (k == argc + 1) { // First operand is the method name.
            if (lai_exec_reserve_ctxstack(state)
                    || lai_exec_reserve_blkstack(state))
                return 1;

            struct lai_operand *opstack_method
                    = lai_exec_get_opstack(state, item->opstack_frame);
            LAI_ENSURE(opstack_method->tag == LAI_RESOLVED_NAME);

            lai_nsnode_t *handle = opstack_method->handle;
            LAI_ENSURE(handle->type == LAI_NAMESPACE_METHOD);

            // TODO: Make sure that this does not leak memory.
            lai_variable_t args[7];
            memset(args, 0, sizeof(lai_variable_t) * 7);

            for(int i = 0; i < argc; i++) {
                struct lai_operand *operand
                        = lai_exec_get_opstack(state, item->opstack_frame + 1 + i);
                lai_exec_get_objectref(state, operand, &args[i]);
            }

            lai_exec_pop_opstack(state, argc + 1);
            lai_exec_pop_stack_back(state);

            if (handle->method_override) {
                // It's an OS-defined method.
                // TODO: Verify the number of argument to the overridden method.
                LAI_CLEANUP_VAR lai_variable_t method_result = LAI_VAR_INITIALIZER;
                int e = handle->method_override(args, &method_result);

                if (e)
                    return e;
                if (want_result) {
                    // Note: there is no need to reserve() as we pop an operand above.
                    struct lai_operand *opstack_res = lai_exec_push_opstack(state);
                    opstack_res->tag = LAI_OPERAND_OBJECT;
                    lai_var_move(&opstack_res->object, &method_result);
                }
            } else {
                // It's an AML method.
                LAI_ENSURE(handle->amls);

                struct lai_ctxitem *method_ctxitem = lai_exec_push_ctxstack(state);
                method_ctxitem->amls = handle->amls;
                method_ctxitem->code = handle->pointer;
                method_ctxitem->handle = handle;
                method_ctxitem->invocation = laihost_malloc(sizeof(struct lai_invocation));
                if (!method_ctxitem->invocation)
                    lai_panic("could not allocate memory for method invocation");
                memset(method_ctxitem->invocation, 0, sizeof(struct lai_invocation));
                lai_list_init(&method_ctxitem->invocation->per_method_list);

                for (int i = 0; i < argc; i++)
                    lai_var_move(&method_ctxitem->invocation->arg[i], &args[i]);

                struct lai_blkitem *blkitem = lai_exec_push_blkstack(state);
                blkitem->pc = 0;
                blkitem->limit = handle->size;

                // Note: there is no need to reserve() as we pop a stackitem above.
                lai_stackitem_t *item = lai_exec_push_stack(state);
                item->kind = LAI_METHOD_STACKITEM;
                item->mth_want_result = want_result;
            }
            return 0;
        } else {
            return lai_exec_parse(LAI_OBJECT_MODE, state);
        }
    } else if (item->kind == LAI_RETURN_STACKITEM) {
        int k = state->opstack_ptr - item->opstack_frame;
        LAI_ENSURE(k <= 1);
        if(k == 1) {
            LAI_CLEANUP_VAR lai_variable_t result = LAI_VAR_INITIALIZER;
            struct lai_operand *operand = lai_exec_get_opstack(state, item->opstack_frame);
            lai_exec_get_objectref(state, operand, &result);
            lai_exec_pop_opstack_back(state);

            // Find the last LAI_METHOD_STACKITEM on the stack.
            int m = 0;
            lai_stackitem_t *method_item;
            while (1) {
                // Ignore the top-most LAI_RETURN_STACKITEM.
                method_item = lai_exec_peek_stack(state, 1 + m);
                if (!method_item)
                    lai_panic("Return() outside of control method()");
                if (method_item->kind == LAI_METHOD_STACKITEM)
                    break;
                if (method_item->kind != LAI_COND_STACKITEM
                        && method_item->kind != LAI_LOOP_STACKITEM)
                    lai_panic("Return() cannot skip item of type %d", method_item->kind);
                m++;
            }

            // Push the return value.
            if (method_item->mth_want_result) {
                // Note: there is no need to reserve() as we pop an operand above.
                struct lai_operand *opstack_res = lai_exec_push_opstack(state);
                opstack_res->tag = LAI_OPERAND_OBJECT;
                lai_obj_clone(&opstack_res->object, &result);
            }

            // Clean up all per-method namespace nodes.
            struct lai_list_item *pmi;
            while ((pmi = lai_list_first(&invocation->per_method_list))) {
                lai_nsnode_t *node = LAI_CONTAINER_OF(pmi, lai_nsnode_t, per_method_item);
                lai_uninstall_nsnode(node);
                lai_list_unlink(&node->per_method_item);
            }

            // Pop the LAI_RETURN_STACKITEM.
            lai_exec_pop_stack_back(state);

            // Pop all nested loops/conditions.
            for (int i = 0; i < m; i++) {
                lai_stackitem_t *pop_item = lai_exec_peek_stack_back(state);
                LAI_ENSURE(pop_item->kind == LAI_COND_STACKITEM
                        || pop_item->kind == LAI_LOOP_STACKITEM);
                lai_exec_pop_blkstack_back(state);
                lai_exec_pop_stack_back(state);
            }

            // Pop the LAI_METHOD_STACKITEM.
            lai_exec_pop_ctxstack_back(state);
            lai_exec_pop_blkstack_back(state);
            lai_exec_pop_stack_back(state);
            return 0;
        } else {
            return lai_exec_parse(LAI_OBJECT_MODE, state);
        }
    } else if (item->kind == LAI_LOOP_STACKITEM) {
        if (!item->loop_state) {
            // We are at the beginning of a loop and need to check the predicate.
            int k = state->opstack_ptr - item->opstack_frame;
            LAI_ENSURE(k <= 1);
            if(k == 1) {
                LAI_CLEANUP_VAR lai_variable_t predicate = LAI_VAR_INITIALIZER;
                struct lai_operand *operand = lai_exec_get_opstack(state, item->opstack_frame);
                lai_exec_get_integer(state, operand, &predicate);
                lai_exec_pop_opstack_back(state);

                if (predicate.integer) {
                    item->loop_state = LAI_LOOP_ITERATION;
                }else{
                    lai_exec_pop_blkstack_back(state);
                    lai_exec_pop_stack_back(state);
                }
                return 0;
            } else {
                return lai_exec_parse(LAI_OBJECT_MODE, state);
            }
        } else {
            LAI_ENSURE(item->loop_state == LAI_LOOP_ITERATION);
            // Unconditionally reset the loop's state to recheck the predicate.
            if (block->pc == block->limit) {
                item->loop_state = 0;
                block->pc = item->loop_pred;
                return 0;
            } else {
                return lai_exec_parse(LAI_EXEC_MODE, state);
            }
        }
    } else if (item->kind == LAI_COND_STACKITEM) {
        if (!item->cond_state) {
            // We are at the beginning of the condition and need to check the predicate.
            int k = state->opstack_ptr - item->opstack_frame;
            LAI_ENSURE(k <= 1);
            if(k == 1) {
                LAI_CLEANUP_VAR lai_variable_t predicate = LAI_VAR_INITIALIZER;
                struct lai_operand *operand = lai_exec_get_opstack(state, item->opstack_frame);
                lai_exec_get_integer(state, operand, &predicate);
                lai_exec_pop_opstack_back(state);

                if (predicate.integer) {
                    item->cond_state = LAI_COND_BRANCH;
                } else {
                    if (item->cond_has_else) {
                        item->cond_state = LAI_COND_BRANCH;
                        block->pc = item->cond_else_pc;
                        block->limit = item->cond_else_limit;
                    } else {
                        lai_exec_pop_blkstack_back(state);
                        lai_exec_pop_stack_back(state);
                    }
                }
                return 0;
            } else {
                return lai_exec_parse(LAI_OBJECT_MODE, state);
            }
        } else {
            LAI_ENSURE(item->cond_state == LAI_COND_BRANCH);
            if (block->pc == block->limit) {
                lai_exec_pop_blkstack_back(state);
                lai_exec_pop_stack_back(state);
                return 0;
            } else {
                return lai_exec_parse(LAI_EXEC_MODE, state);
            }
        }
    } else
        lai_panic("unexpected lai_stackitem_t");
}

// Advances the PC of the current block.
// lai_exec_parse() calls this function after successfully parsing a full opcode.
// Even if parsing fails, this mechanism makes sure that the PC never points to
// the middle of an opcode.
static inline void lai_exec_commit_pc(lai_state_t *state, int pc) {
    // Note that we re-read the block pointer, as the block stack might have been reallocated.
    struct lai_blkitem *block = lai_exec_peek_blkstack_back(state);
    LAI_ENSURE(block);
    block->pc = pc;
}

static int lai_exec_parse(int parse_mode, lai_state_t *state) {
    struct lai_ctxitem *ctxitem = lai_exec_peek_ctxstack_back(state);
    struct lai_blkitem *block = lai_exec_peek_blkstack_back(state);
    LAI_ENSURE(ctxitem);
    LAI_ENSURE(block);
    struct lai_aml_segment *amls = ctxitem->amls;
    uint8_t *method = ctxitem->code;
    lai_nsnode_t *ctx_handle = ctxitem->handle;
    struct lai_invocation *invocation = ctxitem->invocation;

    int pc = block->pc;

    // Package-size encoding (and similar) needs to know the PC of the opcode.
    // If an opcode sequence contains a pkgsize, the sequence generally ends at:
    //     opcode_pc + pkgsize + opcode size.
    int opcode_pc = pc;

    // PC relative to the start of the table.
    // This matches the offsets in the output of 'iasl -l'.
    size_t table_pc = sizeof(acpi_header_t)
                      + (method - amls->table->data)
                      + opcode_pc;
    size_t table_limit_pc = sizeof(acpi_header_t)
                      + (method - amls->table->data)
                      + block->limit;

    if (!(pc < block->limit))
        lai_panic("execution escaped out of code range"
                  " [0x%x, limit 0x%x])",
                  table_pc, table_limit_pc);

    // Whether we use the result of an expression or not.
    // If yes, it will be pushed onto the opstack after the expression is computed.
    int want_result = (parse_mode != LAI_EXEC_MODE);

    if (parse_mode == LAI_IMMEDIATE_BYTE_MODE) {
        uint8_t value = method[pc];
        pc++;

        if (lai_exec_reserve_opstack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        struct lai_operand *result = lai_exec_push_opstack(state);
        result->tag = LAI_OPERAND_OBJECT;
        result->object.type = LAI_INTEGER;
        result->object.integer = value;
        return 0;
    } else if (parse_mode == LAI_IMMEDIATE_WORD_MODE) {
        uint16_t value = (method[pc + 1] << 8) | method[pc];
        pc += 2;

        if (lai_exec_reserve_opstack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        struct lai_operand *result = lai_exec_push_opstack(state);
        result->tag = LAI_OPERAND_OBJECT;
        result->object.type = LAI_INTEGER;
        result->object.integer = value;
        return 0;
    }

    // Process names.
    if (lai_is_name(method[pc])) {
        struct lai_amlname amln;
        pc += lai_amlname_parse(&amln, method + pc);

        if (lai_exec_reserve_opstack(state)
                || lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        LAI_CLEANUP_FREE_STRING char *path = NULL;
        if (debug_opcodes)
            path = lai_stringify_amlname(&amln);

        if (parse_mode == LAI_REFERENCE_MODE) {
            if (debug_opcodes)
                lai_debug("parsing name %s [@ 0x%x]", path, table_pc);

            struct lai_operand *opstack_res = lai_exec_push_opstack(state);
            opstack_res->tag = LAI_UNRESOLVED_NAME;
            opstack_res->unres_ctx_handle = ctx_handle;
            opstack_res->unres_aml = method + opcode_pc;
        }else if (parse_mode == LAI_DATA_MODE) {
            if (debug_opcodes)
                lai_debug("parsing name %s [@ 0x%x]", path, table_pc);

            struct lai_operand *opstack_res = lai_exec_push_opstack(state);
            opstack_res->tag = LAI_OPERAND_OBJECT;
            opstack_res->object.type = LAI_LAZY_HANDLE;
            opstack_res->object.unres_ctx_handle = ctx_handle;
            opstack_res->object.unres_aml = method + opcode_pc;
        } else {
            LAI_ENSURE(parse_mode == LAI_OBJECT_MODE
                       || parse_mode == LAI_EXEC_MODE);
            lai_nsnode_t *handle = lai_do_resolve(ctx_handle, &amln);
            if (!handle)
                lai_panic("undefined reference %s in object mode",
                        lai_stringify_amlname(&amln));

            if(handle->type == LAI_NAMESPACE_METHOD) {
                if (debug_opcodes)
                    lai_debug("parsing invocation %s [@ 0x%x]", path, table_pc);

                lai_stackitem_t *node_item = lai_exec_push_stack(state);
                node_item->kind = LAI_INVOKE_STACKITEM;
                node_item->opstack_frame = state->opstack_ptr;
                node_item->ivk_argc = handle->method_flags & METHOD_ARGC_MASK;
                node_item->ivk_want_result = want_result;

                struct lai_operand *opstack_method = lai_exec_push_opstack(state);
                opstack_method->tag = LAI_RESOLVED_NAME;
                opstack_method->handle = handle;
            } else {
                if (debug_opcodes)
                    lai_debug("parsing name %s [@ 0x%x]", path, table_pc);

                if (want_result) {
                    struct lai_operand *opstack_res = lai_exec_push_opstack(state);
                    opstack_res->tag = LAI_RESOLVED_NAME;
                    opstack_res->handle = handle;
                }
            }
        }
        return 0;
    }

    /* General opcodes */
    int opcode;
    if (method[pc] == EXTOP_PREFIX) {
        if (pc + 1 == block->limit)
            lai_panic("two-byte opcode on method boundary");
        opcode = (EXTOP_PREFIX << 8) | method[pc + 1];
    } else
        opcode = method[pc];
    if (debug_opcodes) {
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
        pc++;

        lai_exec_commit_pc(state, pc);
        break;

    case ZERO_OP:
        pc++;

        if (lai_exec_reserve_opstack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        if (parse_mode == LAI_DATA_MODE || parse_mode == LAI_OBJECT_MODE) {
            struct lai_operand *result = lai_exec_push_opstack(state);
            result->tag = LAI_OPERAND_OBJECT;
            result->object.type = LAI_INTEGER;
            result->object.integer = 0;
        } else if (parse_mode == LAI_REFERENCE_MODE) {
            // In target mode, ZERO_OP generates a null target and not an integer!
            struct lai_operand *result = lai_exec_push_opstack(state);
            result->tag = LAI_NULL_NAME;
        } else {
            lai_warn("Zero() in execution mode has no effect");
            LAI_ENSURE(parse_mode == LAI_EXEC_MODE);
        }
        break;
    case ONE_OP:
        pc++;

        if (lai_exec_reserve_opstack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        if (parse_mode == LAI_DATA_MODE || parse_mode == LAI_OBJECT_MODE) {
            struct lai_operand *result = lai_exec_push_opstack(state);
            result->tag = LAI_OPERAND_OBJECT;
            result->object.type = LAI_INTEGER;
            result->object.integer = 1;
        } else {
            lai_warn("One() in execution mode has no effect");
            LAI_ENSURE(parse_mode == LAI_EXEC_MODE);
        }
        break;
    case ONES_OP:
        pc++;

        if (lai_exec_reserve_opstack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        if (parse_mode == LAI_DATA_MODE || parse_mode == LAI_OBJECT_MODE) {
            struct lai_operand *result = lai_exec_push_opstack(state);
            result->tag = LAI_OPERAND_OBJECT;
            result->object.type = LAI_INTEGER;
            result->object.integer = ~((uint64_t)0);
        } else {
            lai_warn("Ones() in execution mode has no effect");
            LAI_ENSURE(parse_mode == LAI_EXEC_MODE);
        }
        break;

    case BYTEPREFIX:
    case WORDPREFIX:
    case DWORDPREFIX:
    case QWORDPREFIX:
    {
        uint64_t integer;
        size_t integer_size = lai_parse_integer(method + pc, &integer);
        if (!integer_size)
            lai_panic("failed to parse integer opcode");
        pc += integer_size;

        if (lai_exec_reserve_opstack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        if (parse_mode == LAI_DATA_MODE || parse_mode == LAI_OBJECT_MODE) {
            struct lai_operand *result = lai_exec_push_opstack(state);
            result->tag = LAI_OPERAND_OBJECT;
            result->object.type = LAI_INTEGER;
            result->object.integer = integer;
        } else
            LAI_ENSURE(parse_mode == LAI_EXEC_MODE);
        break;
    }
    case STRINGPREFIX:
    {
        int data_pc;
        size_t n = 0; // Length of null-terminated string.
        pc++;
        while (pc + n < block->limit && method[pc + n])
            n++;
        if (pc + n == block->limit)
            lai_panic("unterminated string in AML code");
        data_pc = pc;
        pc += n + 1;

        if (lai_exec_reserve_opstack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        if (parse_mode == LAI_DATA_MODE || parse_mode == LAI_OBJECT_MODE) {
            struct lai_operand *opstack_res = lai_exec_push_opstack(state);
            opstack_res->tag = LAI_OPERAND_OBJECT;
            if(lai_create_string(&opstack_res->object, n))
                lai_panic("could not allocate memory for string");
            memcpy(lai_exec_string_access(&opstack_res->object), method + data_pc, n);
        } else
            LAI_ENSURE(parse_mode == LAI_EXEC_MODE);
        break;
    }
    case BUFFER_OP:
    {
        int data_pc;
        size_t encoded_size; // Size of the buffer initializer.
        pc++;
        pc += lai_parse_pkgsize(method + pc, &encoded_size);
        data_pc = pc;
        pc = opcode_pc + 1 + encoded_size;

        if (lai_exec_reserve_blkstack(state)
                || lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        struct lai_blkitem *blkitem = lai_exec_push_blkstack(state);
        blkitem->pc = data_pc;
        blkitem->limit = opcode_pc + 1 + encoded_size;

        lai_stackitem_t *buf_item = lai_exec_push_stack(state);
        buf_item->kind = LAI_BUFFER_STACKITEM;
        buf_item->opstack_frame = state->opstack_ptr;
        buf_item->buf_want_result = want_result;
        break;
    }
    case PACKAGE_OP:
    {
        int data_pc;
        size_t encoded_size; // Size of the package initializer.
        int num_ents; // The number of elements of the package.
        pc++;
        pc += lai_parse_pkgsize(method + pc, &encoded_size);
        num_ents = method[pc];
        pc++;
        data_pc = pc;
        pc = opcode_pc + 1 + encoded_size;

        if (lai_exec_reserve_opstack(state)
                || lai_exec_reserve_blkstack(state)
                || lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        // Note that not all elements of the package need to be initialized.

        struct lai_blkitem *blkitem = lai_exec_push_blkstack(state);
        blkitem->pc = data_pc;
        blkitem->limit = opcode_pc + 1 + encoded_size;

        lai_stackitem_t *pkg_item = lai_exec_push_stack(state);
        pkg_item->kind = LAI_PACKAGE_STACKITEM;
        pkg_item->opstack_frame = state->opstack_ptr;
        pkg_item->pkg_index = 0;
        pkg_item->pkg_want_result = want_result;

        struct lai_operand *opstack_pkg = lai_exec_push_opstack(state);
        opstack_pkg->tag = LAI_OPERAND_OBJECT;
        if (lai_create_pkg(&opstack_pkg->object, num_ents))
            lai_panic("could not allocate memory for package");
        break;
    }

    /* A control method can return literally any object */
    /* So we need to take this into consideration */
    case RETURN_OP:
    {
        pc++;

        if (lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        lai_stackitem_t *node_item = lai_exec_push_stack(state);
        node_item->kind = LAI_RETURN_STACKITEM;
        node_item->opstack_frame = state->opstack_ptr;
        break;
    }
    /* While Loops */
    case WHILE_OP:
    {
        int body_pc;
        size_t loop_size;
        pc++;
        pc += lai_parse_pkgsize(&method[pc], &loop_size);
        body_pc = pc;
        pc = opcode_pc + 1 + loop_size;

        if (lai_exec_reserve_blkstack(state)
                || lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        struct lai_blkitem *blkitem = lai_exec_push_blkstack(state);
        blkitem->pc = body_pc;
        blkitem->limit = opcode_pc + 1 + loop_size;

        lai_stackitem_t *loop_item = lai_exec_push_stack(state);
        loop_item->kind = LAI_LOOP_STACKITEM;
        loop_item->opstack_frame = state->opstack_ptr;
        loop_item->loop_state = 0;
        loop_item->loop_pred = body_pc;
        break;
    }
    /* Continue Looping */
    case CONTINUE_OP:
    {
        // Find the last LAI_LOOP_STACKITEM on the stack.
        int m = 0;
        lai_stackitem_t *loop_item;
        while (1) {
            loop_item = lai_exec_peek_stack(state, m);
            if (!loop_item)
                lai_panic("Continue() outside of While()");
            if (loop_item->kind == LAI_LOOP_STACKITEM)
                break;
            if (loop_item->kind != LAI_COND_STACKITEM
                    && loop_item->kind != LAI_LOOP_STACKITEM)
                lai_panic("Continue() cannot skip item of type %d", loop_item->kind);
            m++;
        }

        // Pop all nested loops/conditions.
        for (int i = 0; i < m; i++) {
            lai_stackitem_t *pop_item = lai_exec_peek_stack_back(state);
            LAI_ENSURE(pop_item->kind == LAI_COND_STACKITEM
                    || pop_item->kind == LAI_LOOP_STACKITEM);
            lai_exec_pop_blkstack_back(state);
            lai_exec_pop_stack_back(state);
        }

        // Keep the LAI_LOOP_STACKITEM but reset the PC.
        pc = loop_item->loop_pred;
        break;
    }
    /* Break Loop */
    case BREAK_OP:
    {
        // Find the last LAI_LOOP_STACKITEM on the stack.
        int m = 0;
        lai_stackitem_t *loop_item;
        while (1) {
            loop_item = lai_exec_peek_stack(state, m);
            if (!loop_item)
                lai_panic("Break() outside of While()");
            if (loop_item->kind == LAI_LOOP_STACKITEM)
                break;
            if (loop_item->kind != LAI_COND_STACKITEM
                    && loop_item->kind != LAI_LOOP_STACKITEM)
                lai_panic("Break() cannot skip item of type %d", loop_item->kind);
            m++;
        }

        // Pop all nested loops/conditions.
        for (int i = 0; i < m; i++) {
            lai_stackitem_t *pop_item = lai_exec_peek_stack_back(state);
            LAI_ENSURE(pop_item->kind == LAI_COND_STACKITEM
                    || pop_item->kind == LAI_LOOP_STACKITEM);
            lai_exec_pop_blkstack_back(state);
            lai_exec_pop_stack_back(state);
        }

        // Pop the LAI_LOOP_STACKITEM item.
        lai_exec_pop_blkstack_back(state);
        lai_exec_pop_stack_back(state);
        break;
    }
    /* If/Else Conditional */
    case IF_OP:
    {
        int if_pc;
        int else_pc;
        int has_else = 0;
        size_t if_size;
        size_t else_size;
        pc++;
        pc += lai_parse_pkgsize(method + pc, &if_size);
        if_pc = pc;
        pc = opcode_pc + 1 + if_size;
        if (pc < block->limit && method[pc] == ELSE_OP) {
            has_else = 1;
            pc++;
            pc += lai_parse_pkgsize(method + pc, &else_size);
            else_pc = pc;
            pc = opcode_pc + 1 + if_size + 1 + else_size;
        }

        if (lai_exec_reserve_blkstack(state)
                || lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        struct lai_blkitem *blkitem = lai_exec_push_blkstack(state);
        blkitem->pc = if_pc;
        blkitem->limit = opcode_pc + 1 + if_size;

        lai_stackitem_t *cond_item = lai_exec_push_stack(state);
        cond_item->kind = LAI_COND_STACKITEM;
        cond_item->opstack_frame = state->opstack_ptr;
        cond_item->cond_state = 0;
        cond_item->cond_has_else = has_else;
        cond_item->cond_else_pc = else_pc;
        cond_item->cond_else_limit = opcode_pc + 1 + if_size + 1 + else_size;
        break;
    }
    case ELSE_OP:
        lai_panic("Else() outside of If()");
        break;

    // Scope-like objects in the ACPI namespace.
    case SCOPE_OP:
    {
        int nested_pc;
        size_t encoded_size;
        struct lai_amlname amln;
        pc++;
        pc += lai_parse_pkgsize(method + pc, &encoded_size);
        pc += lai_amlname_parse(&amln, method + pc);
        nested_pc = pc;
        pc = opcode_pc + 1 + encoded_size;

        if (lai_exec_reserve_ctxstack(state)
                || lai_exec_reserve_blkstack(state)
                || lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        lai_nsnode_t *scoped_ctx_handle = lai_do_resolve(ctx_handle, &amln);
        if (!scoped_ctx_handle)
            lai_panic("could not resolve node referenced in scope");

        struct lai_ctxitem *populate_ctxitem = lai_exec_push_ctxstack(state);
        populate_ctxitem->amls = amls;
        populate_ctxitem->code = method;
        populate_ctxitem->handle = scoped_ctx_handle;

        struct lai_blkitem *blkitem = lai_exec_push_blkstack(state);
        blkitem->pc = nested_pc;
        blkitem->limit = opcode_pc + 1 + encoded_size;

        lai_stackitem_t *item = lai_exec_push_stack(state);
        item->kind = LAI_POPULATE_STACKITEM;
        break;
    }
    case (EXTOP_PREFIX << 8) | DEVICE:
    {
        int nested_pc;
        size_t encoded_size;
        struct lai_amlname amln;
        pc += 2;
        pc += lai_parse_pkgsize(method + pc, &encoded_size);
        pc += lai_amlname_parse(&amln, method + pc);
        nested_pc = pc;
        pc = opcode_pc + 2 + encoded_size;

        if (lai_exec_reserve_ctxstack(state)
                || lai_exec_reserve_blkstack(state)
                || lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        lai_nsnode_t *node = lai_create_nsnode_or_die();
        node->type = LAI_NAMESPACE_DEVICE;
        lai_do_resolve_new_node(node, ctx_handle, &amln);
        lai_install_nsnode(node);
        if (invocation)
            lai_list_link(&invocation->per_method_list, &node->per_method_item);

        struct lai_ctxitem *populate_ctxitem = lai_exec_push_ctxstack(state);
        populate_ctxitem->amls = amls;
        populate_ctxitem->code = method;
        populate_ctxitem->handle = node;

        struct lai_blkitem *blkitem = lai_exec_push_blkstack(state);
        blkitem->pc = nested_pc;
        blkitem->limit = opcode_pc + 2 + encoded_size;

        lai_stackitem_t *item = lai_exec_push_stack(state);
        item->kind = LAI_POPULATE_STACKITEM;
        break;
    }
    case (EXTOP_PREFIX << 8) | PROCESSOR: {
        pc += 2;            // skip over PROCESSOR_OP
        size_t tmp_pc = pc;

        size_t pkgsize;
        struct lai_amlname amln;
        tmp_pc += lai_parse_pkgsize(method + tmp_pc, &pkgsize);
        tmp_pc += lai_amlname_parse(&amln, method + tmp_pc);
        pc += pkgsize;

        lai_exec_commit_pc(state, pc);

        lai_nsnode_t *node = lai_create_nsnode_or_die();
        node->type = LAI_NAMESPACE_PROCESSOR;
        node->cpu_id = *(method + tmp_pc);

        // TODO: parse rest of Processor() data

        lai_do_resolve_new_node(node, ctx_handle, &amln);
        lai_install_nsnode(node);
        if (invocation)
            lai_list_link(&invocation->per_method_list, &node->per_method_item);
        break;
    }
    case (EXTOP_PREFIX << 8) | POWER_RES:
    {
        int nested_pc;
        size_t encoded_size;
        struct lai_amlname amln;
        pc += 2;
        pc += lai_parse_pkgsize(method + pc, &encoded_size);
        pc += lai_amlname_parse(&amln, method + pc);
//            uint8_t system_level = method[pc];
        pc++;
//            uint16_t resource_order = *(uint16_t*)&method[pc];
        pc += 2;
        nested_pc = pc;
        pc = opcode_pc + 2 + encoded_size;

        if (lai_exec_reserve_ctxstack(state)
                || lai_exec_reserve_blkstack(state)
                || lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        lai_nsnode_t *node = lai_create_nsnode_or_die();
        node->type = LAI_NAMESPACE_POWER_RES;
        lai_do_resolve_new_node(node, ctx_handle, &amln);
        lai_install_nsnode(node);
        if (invocation)
            lai_list_link(&invocation->per_method_list, &node->per_method_item);

        struct lai_ctxitem *populate_ctxitem = lai_exec_push_ctxstack(state);
        populate_ctxitem->amls = amls;
        populate_ctxitem->code = method;
        populate_ctxitem->handle = node;

        struct lai_blkitem *blkitem = lai_exec_push_blkstack(state);
        blkitem->pc = nested_pc;
        blkitem->limit = opcode_pc + 2 + encoded_size;

        lai_stackitem_t *item = lai_exec_push_stack(state);
        item->kind = LAI_POPULATE_STACKITEM;
        break;
    }
    case (EXTOP_PREFIX << 8) | THERMALZONE:
    {
        int nested_pc;
        size_t encoded_size;
        struct lai_amlname amln;
        pc += 2;
        pc += lai_parse_pkgsize(method + pc, &encoded_size);
        pc += lai_amlname_parse(&amln, method + pc);
        nested_pc = pc;
        pc = opcode_pc + 2 + encoded_size;

        if (lai_exec_reserve_ctxstack(state)
                || lai_exec_reserve_blkstack(state)
                || lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        lai_nsnode_t *node = lai_create_nsnode_or_die();
        node->type = LAI_NAMESPACE_THERMALZONE;
        lai_do_resolve_new_node(node, ctx_handle, &amln);
        lai_install_nsnode(node);
        if (invocation)
            lai_list_link(&invocation->per_method_list, &node->per_method_item);

        struct lai_ctxitem *populate_ctxitem = lai_exec_push_ctxstack(state);
        populate_ctxitem->amls = amls;
        populate_ctxitem->code = method;
        populate_ctxitem->handle = node;

        struct lai_blkitem *blkitem = lai_exec_push_blkstack(state);
        blkitem->pc = nested_pc;
        blkitem->limit = opcode_pc + 2 + encoded_size;

        lai_stackitem_t *item = lai_exec_push_stack(state);
        item->kind = LAI_POPULATE_STACKITEM;
        break;
    }

    // Leafs in the ACPI namespace.
    case METHOD_OP:
        pc += lai_create_method(ctx_handle, amls, method + pc);

        lai_exec_commit_pc(state, pc);
        break;
    case NAME_OP: {
        pc++;

        if (lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        lai_stackitem_t *node_item = lai_exec_push_stack(state);
        node_item->kind = LAI_NODE_STACKITEM;
        node_item->node_opcode = opcode;
        node_item->opstack_frame = state->opstack_ptr;
        node_item->node_arg_modes[0] = LAI_REFERENCE_MODE;
        node_item->node_arg_modes[1] = LAI_OBJECT_MODE;
        node_item->node_arg_modes[2] = 0;
        break;
    }
    case ALIAS_OP: {
        struct lai_amlname target_amln;
        struct lai_amlname dest_amln;
        pc++;
        pc += lai_amlname_parse(&target_amln, method + pc);
        pc += lai_amlname_parse(&dest_amln, method + pc);

        lai_exec_commit_pc(state, pc);

        lai_nsnode_t *node = lai_create_nsnode_or_die();
        node->type = LAI_NAMESPACE_ALIAS;
        node->al_target = lai_do_resolve(ctx_handle, &target_amln);
        if (!node->al_target)
            lai_panic("cannot resolve target %s of Alias()", lai_stringify_amlname(&target_amln));
        lai_do_resolve_new_node(node, ctx_handle, &dest_amln);

        lai_install_nsnode(node);
        if (invocation)
            lai_list_link(&invocation->per_method_list, &node->per_method_item);
        break;
    }
    case BYTEFIELD_OP:
    case WORDFIELD_OP:
    case DWORDFIELD_OP:
    case QWORDFIELD_OP: {
        pc++;

        if (lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        lai_stackitem_t *node_item = lai_exec_push_stack(state);
        node_item->kind = LAI_NODE_STACKITEM;
        node_item->node_opcode = opcode;
        node_item->opstack_frame = state->opstack_ptr;
        node_item->node_arg_modes[0] = LAI_REFERENCE_MODE;
        node_item->node_arg_modes[1] = LAI_OBJECT_MODE;
        node_item->node_arg_modes[2] = LAI_REFERENCE_MODE;
        node_item->node_arg_modes[3] = 0;
        break;
    }
    case (EXTOP_PREFIX << 8) | MUTEX: {
        struct lai_amlname amln;
        pc += 2;
        pc += lai_amlname_parse(&amln, method + pc);
        pc++; // skip over trailing 0x02

        lai_exec_commit_pc(state, pc);

        lai_nsnode_t *node = lai_create_nsnode_or_die();
        node->type = LAI_NAMESPACE_MUTEX;
        lai_do_resolve_new_node(node, ctx_handle, &amln);
        lai_install_nsnode(node);
        if (invocation)
            lai_list_link(&invocation->per_method_list, &node->per_method_item);
        break;
    }
    case (EXTOP_PREFIX << 8) | EVENT: {
        struct lai_amlname amln;
        pc += 2;
        pc += lai_amlname_parse(&amln, method + pc);

        lai_exec_commit_pc(state, pc);

        lai_nsnode_t* node = lai_create_nsnode_or_die();
        node->type = LAI_NAMESPACE_EVENT;
        lai_do_resolve_new_node(node, ctx_handle, &amln);
        lai_install_nsnode(node);
        if (invocation)
            lai_list_link(&invocation->per_method_list, &node->per_method_item);
        break;
    }
    case (EXTOP_PREFIX << 8) | OPREGION: {
        pc += 2;

        if (lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        lai_stackitem_t *node_item = lai_exec_push_stack(state);
        node_item->kind = LAI_NODE_STACKITEM;
        node_item->node_opcode = opcode;
        node_item->opstack_frame = state->opstack_ptr;
        node_item->node_arg_modes[0] = LAI_REFERENCE_MODE;
        node_item->node_arg_modes[1] = LAI_IMMEDIATE_BYTE_MODE;
        node_item->node_arg_modes[2] = LAI_OBJECT_MODE;
        node_item->node_arg_modes[3] = LAI_OBJECT_MODE;
        node_item->node_arg_modes[4] = 0;
        break;
    }
    case (EXTOP_PREFIX << 8) | FIELD: {
        struct lai_amlname region_amln;
        size_t pkgsize;
        pc += 2;
        pc += lai_parse_pkgsize(method + pc, &pkgsize);
        pc += lai_amlname_parse(&region_amln, method + pc);

        int end_pc = opcode_pc + 2 + pkgsize;

        lai_nsnode_t *region_node = lai_do_resolve(ctx_handle, &region_amln);
        if (!region_node) {
            lai_panic("error parsing field for non-existant OpRegion, ignoring...");
            pc = end_pc;
            break;
        }

        uint8_t access_type = *(method + pc);
        pc++;

        // parse FieldList
        struct lai_amlname field_amln;
        uint64_t curr_off = 0;
        size_t skip_bits;
        while (pc < end_pc) {
            switch (*(method + pc)) {
                case 0: // ReservedField
                    pc++;
                    pc += lai_parse_pkgsize(method + pc, &skip_bits);
                    curr_off += skip_bits;
                    break;
                case 1: // AccessField
                    pc++;
                    access_type = *(method + pc);
                    pc += 2;
                    break;
                case 2: // TODO: ConnectField
                    lai_panic("ConnectField parsing isn't implemented");
                    break;
                default: // NamedField
                    pc += lai_amlname_parse(&field_amln, method + pc);
                    pc += lai_parse_pkgsize(method + pc, &skip_bits);

                    lai_nsnode_t *node = lai_create_nsnode_or_die();
                    node->type = LAI_NAMESPACE_FIELD;
                    node->fld_region_node = region_node;
                    node->fld_flags = access_type;
                    node->fld_size = skip_bits;
                    node->fld_offset = curr_off;
                    lai_do_resolve_new_node(node, ctx_handle, &field_amln);
                    lai_install_nsnode(node);
                    if (invocation)
                        lai_list_link(&invocation->per_method_list,
                                      &node->per_method_item);

                    curr_off += skip_bits;
            }
        }
        lai_exec_commit_pc(state, pc);

        break;
    }
    case (EXTOP_PREFIX << 8) | INDEXFIELD: {
        struct lai_amlname index_amln;
        struct lai_amlname data_amln;
        size_t pkgsize;
        pc += 2;
        pc += lai_parse_pkgsize(method + pc, &pkgsize);
        pc += lai_amlname_parse(&index_amln, method + pc);
        pc += lai_amlname_parse(&data_amln, method + pc);

        int end_pc = opcode_pc + 2 + pkgsize;

        lai_nsnode_t *index_node = lai_do_resolve(ctx_handle, &index_amln);
        lai_nsnode_t *data_node = lai_do_resolve(ctx_handle, &data_amln);
        if (!index_node || !data_node)
            lai_panic("could not resolve index register of IndexField()");

        uint8_t access_type = *(method + pc);
        pc++;

        // parse FieldList
        struct lai_amlname field_amln;
        uint64_t curr_off = 0;
        size_t skip_bits;
        while (pc < end_pc) {
            switch (*(method + pc)) {
                case 0: // ReservedField
                    pc++;
                    pc += lai_parse_pkgsize(method + pc, &skip_bits);
                    curr_off += skip_bits;
                    break;
                case 1: // AccessField
                    pc++;
                    access_type = *(method + pc);
                    pc += 2;
                    break;
                case 2: // TODO: ConnectField
                    lai_panic("ConnectField parsing isn't implemented");
                    break;
                default: // NamedField
                    pc += lai_amlname_parse(&field_amln, method + pc);
                    pc += lai_parse_pkgsize(method + pc, &skip_bits);

                    lai_nsnode_t *node = lai_create_nsnode_or_die();
                    node->type = LAI_NAMESPACE_INDEXFIELD;
                    node->idxf_index_node = index_node;
                    node->idxf_data_node = data_node;
                    node->idxf_flags = access_type;
                    node->idxf_size = skip_bits;
                    node->idxf_offset = curr_off;
                    lai_do_resolve_new_node(node, ctx_handle, &field_amln);
                    lai_install_nsnode(node);
                    if (invocation)
                        lai_list_link(&invocation->per_method_list,
                                      &node->per_method_item);

                    curr_off += skip_bits;
            }
        }
        lai_exec_commit_pc(state, pc);

        break;
    }

    case ARG0_OP:
    case ARG1_OP:
    case ARG2_OP:
    case ARG3_OP:
    case ARG4_OP:
    case ARG5_OP:
    case ARG6_OP: {
        pc++;

        if (lai_exec_reserve_opstack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        if (parse_mode == LAI_OBJECT_MODE
                || parse_mode == LAI_REFERENCE_MODE) {
            struct lai_operand *result = lai_exec_push_opstack(state);
            result->tag = LAI_ARG_NAME;
            result->index = opcode - ARG0_OP;
        }
        break;
    }

    case LOCAL0_OP:
    case LOCAL1_OP:
    case LOCAL2_OP:
    case LOCAL3_OP:
    case LOCAL4_OP:
    case LOCAL5_OP:
    case LOCAL6_OP:
    case LOCAL7_OP: {
        pc++;

        if (lai_exec_reserve_opstack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        if(parse_mode == LAI_OBJECT_MODE
                || parse_mode == LAI_REFERENCE_MODE) {
            struct lai_operand *result = lai_exec_push_opstack(state);
            result->tag = LAI_LOCAL_NAME;
            result->index = opcode - LOCAL0_OP;
        }
        break;
    }

    case (EXTOP_PREFIX << 8) | DEBUG_OP: {
        pc += 2;

        if (lai_exec_reserve_opstack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        if(parse_mode == LAI_OBJECT_MODE
                || parse_mode == LAI_REFERENCE_MODE) {
            struct lai_operand *result = lai_exec_push_opstack(state);
            result->tag = LAI_DEBUG_NAME;
        }
        break;
    }

    case STORE_OP:
    case NOT_OP: {
        pc++;

        if (lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        lai_stackitem_t *op_item = lai_exec_push_stack(state);
        op_item->kind = LAI_OP_STACKITEM;
        op_item->op_opcode = opcode;
        op_item->opstack_frame = state->opstack_ptr;
        op_item->op_arg_modes[0] = LAI_OBJECT_MODE;
        op_item->op_arg_modes[1] = LAI_REFERENCE_MODE;
        op_item->op_arg_modes[2] = 0;
        op_item->op_want_result = want_result;
        break;
    }
    case ADD_OP:
    case SUBTRACT_OP:
    case MULTIPLY_OP:
    case AND_OP:
    case OR_OP:
    case XOR_OP:
    case SHR_OP:
    case SHL_OP: {
        pc++;

        if (lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        lai_stackitem_t *op_item = lai_exec_push_stack(state);
        op_item->kind = LAI_OP_STACKITEM;
        op_item->op_opcode = opcode;
        op_item->opstack_frame = state->opstack_ptr;
        op_item->op_arg_modes[0] = LAI_OBJECT_MODE;
        op_item->op_arg_modes[1] = LAI_OBJECT_MODE;
        op_item->op_arg_modes[2] = LAI_REFERENCE_MODE;
        op_item->op_arg_modes[3] = 0;
        op_item->op_want_result = want_result;
        break;
    }
    case DIVIDE_OP: {
        pc++;

        if (lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        lai_stackitem_t *op_item = lai_exec_push_stack(state);
        op_item->kind = LAI_OP_STACKITEM;
        op_item->op_opcode = opcode;
        op_item->opstack_frame = state->opstack_ptr;
        op_item->op_arg_modes[0] = LAI_OBJECT_MODE;
        op_item->op_arg_modes[1] = LAI_OBJECT_MODE;
        op_item->op_arg_modes[2] = LAI_REFERENCE_MODE;
        op_item->op_arg_modes[3] = LAI_REFERENCE_MODE;
        op_item->op_arg_modes[4] = 0;
        op_item->op_want_result = want_result;
        break;
    }

    case INCREMENT_OP:
    case DECREMENT_OP: {
        pc++;

        if (lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        lai_stackitem_t *op_item = lai_exec_push_stack(state);
        op_item->kind = LAI_OP_STACKITEM;
        op_item->op_opcode = opcode;
        op_item->opstack_frame = state->opstack_ptr;
        op_item->op_arg_modes[0] = LAI_REFERENCE_MODE;
        op_item->op_arg_modes[1] = 0;
        op_item->op_want_result = want_result;
        break;
    }

    case LNOT_OP: {
        pc++;

        if (lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        lai_stackitem_t *op_item = lai_exec_push_stack(state);
        op_item->kind = LAI_OP_STACKITEM;
        op_item->op_opcode = opcode;
        op_item->opstack_frame = state->opstack_ptr;
        op_item->op_arg_modes[0] = LAI_OBJECT_MODE;
        op_item->op_arg_modes[1] = 0;
        op_item->op_want_result = want_result;
        break;
    }
    case LAND_OP:
    case LOR_OP:
    case LEQUAL_OP:
    case LLESS_OP:
    case LGREATER_OP: {
        pc++;

        if (lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        lai_stackitem_t *op_item = lai_exec_push_stack(state);
        op_item->kind = LAI_OP_STACKITEM;
        op_item->op_opcode = opcode;
        op_item->opstack_frame = state->opstack_ptr;
        op_item->op_arg_modes[0] = LAI_OBJECT_MODE;
        op_item->op_arg_modes[1] = LAI_OBJECT_MODE;
        op_item->op_arg_modes[2] = 0;
        op_item->op_want_result = want_result;
        break;
    }

    case INDEX_OP: {
        pc++;

        if (lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        lai_stackitem_t *op_item = lai_exec_push_stack(state);
        op_item->kind = LAI_OP_STACKITEM;
        op_item->op_opcode = opcode;
        op_item->opstack_frame = state->opstack_ptr;
        op_item->op_arg_modes[0] = LAI_OBJECT_MODE;
        op_item->op_arg_modes[1] = LAI_OBJECT_MODE;
        op_item->op_arg_modes[2] = LAI_REFERENCE_MODE;
        op_item->op_arg_modes[3] = 0;
        op_item->op_want_result = want_result;
        break;
    }
    case DEREF_OP:
    case SIZEOF_OP: {
        pc++;

        if (lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        lai_stackitem_t *op_item = lai_exec_push_stack(state);
        op_item->kind = LAI_OP_STACKITEM;
        op_item->op_opcode = opcode;
        op_item->opstack_frame = state->opstack_ptr;
        op_item->op_arg_modes[0] = LAI_OBJECT_MODE;
        op_item->op_arg_modes[1] = 0;
        op_item->op_want_result = want_result;
        break;
    }
    case (EXTOP_PREFIX << 8) | CONDREF_OP: {
        pc += 2;

        if (lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        lai_stackitem_t *op_item = lai_exec_push_stack(state);
        op_item->kind = LAI_OP_STACKITEM;
        op_item->op_opcode = opcode;
        op_item->opstack_frame = state->opstack_ptr;
        // TODO: There should be a NAME_MODE that allows only names to be parsed.
        op_item->op_arg_modes[0] = LAI_REFERENCE_MODE;
        op_item->op_arg_modes[1] = LAI_REFERENCE_MODE;
        op_item->op_arg_modes[2] = 0;
        op_item->op_want_result = want_result;
        break;
    }

    case (EXTOP_PREFIX << 8) | SLEEP_OP: {
        pc += 2;

        if (lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        lai_stackitem_t *op_item = lai_exec_push_stack(state);
        op_item->kind = LAI_OP_STACKITEM;
        op_item->op_opcode = opcode;
        op_item->opstack_frame = state->opstack_ptr;
        op_item->op_arg_modes[0] = LAI_OBJECT_MODE;
        op_item->op_arg_modes[1] = 0;
        op_item->op_want_result = want_result;
        break;
    }

    case (EXTOP_PREFIX << 8) | ACQUIRE_OP: {
        pc += 2;

        if (lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        lai_stackitem_t *op_item = lai_exec_push_stack(state);
        op_item->kind = LAI_OP_STACKITEM;
        op_item->op_opcode = opcode;
        op_item->opstack_frame = state->opstack_ptr;
        op_item->op_arg_modes[0] = LAI_REFERENCE_MODE;
        op_item->op_arg_modes[1] = LAI_IMMEDIATE_WORD_MODE;
        op_item->op_arg_modes[2] = 0;
        op_item->op_want_result = want_result;
        break;
    }
    case (EXTOP_PREFIX << 8) | RELEASE_OP: {
        pc += 2;

        if (lai_exec_reserve_stack(state))
            return 1;
        lai_exec_commit_pc(state, pc);

        lai_stackitem_t *op_item = lai_exec_push_stack(state);
        op_item->kind = LAI_OP_STACKITEM;
        op_item->op_opcode = opcode;
        op_item->opstack_frame = state->opstack_ptr;
        op_item->op_arg_modes[0] = LAI_REFERENCE_MODE;
        op_item->op_arg_modes[1] = 0;
        op_item->op_want_result = want_result;
        break;
    }

    default:
        lai_panic("unexpected opcode in lai_exec_run(), sequence %02X %02X %02X %02X",
                method[pc + 0], method[pc + 1],
                method[pc + 2], method[pc + 3]);
    }
    return 0;
}

int lai_populate(lai_nsnode_t *parent, struct lai_aml_segment *amls, lai_state_t *state) {
    if (lai_exec_reserve_ctxstack(state)
            || lai_exec_reserve_blkstack(state)
            || lai_exec_reserve_stack(state))
        return 1;

    size_t size = amls->table->header.length - sizeof(acpi_header_t);

    struct lai_ctxitem *populate_ctxitem = lai_exec_push_ctxstack(state);
    populate_ctxitem->amls = amls;
    populate_ctxitem->code = amls->table->data;
    populate_ctxitem->handle = parent;

    struct lai_blkitem *blkitem = lai_exec_push_blkstack(state);
    blkitem->pc = 0;
    blkitem->limit = size;

    lai_stackitem_t *item = lai_exec_push_stack(state);
    item->kind = LAI_POPULATE_STACKITEM;

    int status = lai_exec_run(state);
    if (status)
        lai_panic("lai_exec_run() failed in lai_populate()");
    LAI_ENSURE(state->ctxstack_ptr == -1);
    LAI_ENSURE(state->stack_ptr == -1);
    LAI_ENSURE(!state->opstack_ptr);
    return 0;
}

// lai_eval_args(): Evaluates a node of the ACPI namespace (including control methods).
int lai_eval_args(lai_variable_t *result, lai_nsnode_t *handle, lai_state_t *state,
        int n, lai_variable_t *args) {
    LAI_ENSURE(handle);
    LAI_ENSURE(handle->type != LAI_NAMESPACE_ALIAS);

    switch (handle->type) {
        case LAI_NAMESPACE_NAME:
            if (n) {
                lai_warn("non-empty argument list given when evaluating Name()");
                return 1;
            }
            if (result)
                lai_obj_clone(result, &handle->object);
            return 0;
        case LAI_NAMESPACE_METHOD: {
            if (lai_exec_reserve_ctxstack(state)
                    || lai_exec_reserve_blkstack(state)
                    || lai_exec_reserve_stack(state))
                return 1;

            LAI_CLEANUP_VAR lai_variable_t method_result = LAI_VAR_INITIALIZER;
            int e;
            if (handle->method_override) {
                // It's an OS-defined method.
                // TODO: Verify the number of argument to the overridden method.
                e = handle->method_override(args, &method_result);
            } else {
                // It's an AML method.
                LAI_ENSURE(handle->amls);

                struct lai_ctxitem *method_ctxitem = lai_exec_push_ctxstack(state);
                method_ctxitem->amls = handle->amls;
                method_ctxitem->code = handle->pointer;
                method_ctxitem->handle = handle;
                method_ctxitem->invocation = laihost_malloc(sizeof(struct lai_invocation));
                if (!method_ctxitem->invocation)
                    lai_panic("could not allocate memory for method invocation");
                memset(method_ctxitem->invocation, 0, sizeof(struct lai_invocation));
                lai_list_init(&method_ctxitem->invocation->per_method_list);

                for (int i = 0; i < n; i++)
                    lai_var_assign(&method_ctxitem->invocation->arg[i], &args[i]);

                struct lai_blkitem *blkitem = lai_exec_push_blkstack(state);
                blkitem->pc = 0;
                blkitem->limit = handle->size;

                lai_stackitem_t *item = lai_exec_push_stack(state);
                item->kind = LAI_METHOD_STACKITEM;
                item->mth_want_result = 1;

                e = lai_exec_run(state);

                if (!e) {
                    LAI_ENSURE(state->ctxstack_ptr == -1);
                    LAI_ENSURE(state->stack_ptr == -1);
                    if (state->opstack_ptr != 1) // This would be an internal error.
                        lai_panic("expected exactly one return value after method invocation");
                    struct lai_operand *opstack_top = lai_exec_get_opstack(state, 0);
                    lai_variable_t objectref = {0};
                    lai_exec_get_objectref(state, opstack_top, &objectref);
                    lai_obj_clone(&method_result, &objectref);
                    lai_var_finalize(&objectref);
                    lai_exec_pop_opstack(state, 1);
                }
            }
            if (!e && result)
                lai_var_move(result, &method_result);
            return e;
        }

        default:
            return 1;
    }
}

int lai_eval_vargs(lai_variable_t *result, lai_nsnode_t *handle, lai_state_t *state, va_list vl) {
    int n = 0;
    lai_variable_t args[7];
    memset(args, 0, sizeof(lai_variable_t) * 7);

    for (;;) {
        LAI_ENSURE(n < 7 && "ACPI supports at most 7 arguments");
        lai_variable_t *object = va_arg(vl, lai_variable_t *);
        if (!object)
            break;
        lai_var_assign(&args[n++], object);
    }

    return lai_eval_args(result, handle, state, n, args);
}

int lai_eval_largs(lai_variable_t *result, lai_nsnode_t *handle, lai_state_t *state, ...) {
    va_list vl;
    va_start(vl, state);
    int e = lai_eval_vargs(result, handle, state, vl);
    va_end(vl);
    return e;
}

int lai_eval(lai_variable_t *result, lai_nsnode_t *handle, lai_state_t *state) {
    return lai_eval_args(result, handle, state, 0, NULL);
}

void lai_enable_tracing(int enable) {
    debug_opcodes = enable;
}
