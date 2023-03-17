#include <iostream>

#include "opcode.h"
#include "lpc.h"
#include "lpc_value.h"
#include "type/lpc_object.h"
#include "type/lpc_proto.h"
#include "runtime/interpreter.h"
#include "runtime/stack.h"

static int max_call = 50;
static const int max_stack_size = 20000;

void vm::eval(lpc_vm_t *lvm)
{
    call_info_t *ci = lvm->get_call_info();
    lpc_stack_t *sk = lvm->get_stack();
    const char *sp = ci->savepc;
    ci->base = sk->top();
    

    for(;;) {
        OpCode op = (OpCode)*(sp++);
        switch (op)
        {
        case OpCode::op_load_global: {

            break;
        }
        case OpCode::op_load_local: {
            lint16_t idx = *(sp + 1) | *(sp + 1);
            sk->push(ci->base + idx);
            sp += 2;
            break;
        }

        case OpCode::op_load_iconst: {
            lint16_t idx = *(sp + 1) | *(sp + 1);
            lpc_value_t *val = sk->top();
            val->type = value_type::int_;
            val->pval.number = ci->cur_obj->get_proto()->iconst[idx].item.number;
            sk->push(val);
            sp += 2;
            break;
        }
        case OpCode::op_load_fconst: {

            break;
        }
        case OpCode::op_load_sconst: {

            break;
        }


        case OpCode::op_load_0: {

            break;
        }
        case OpCode::op_load_1: {

            break;
        }

        case OpCode::op_push: {
            lpc_value_t *v = sk->get(0);
            v->type = value_type::int_;
            v->pval.number = 100;
            sk->push(v);
            break;
        }
        case OpCode::op_pop: {

            break;
        }

        case OpCode::op_add: {
            lpc_value_t *v1 = sk->pop();
            lpc_value_t *v2 = sk->pop();
            lpc_value_t *v3 = sk->get(2);
            v3->type = value_type::int_;
            v3->pval.number = v1->pval.number + v2->pval.number;
            std::cout << "numbers: " << v1->pval.number << ", " << v2->pval.number << ", " << v3->pval.number << std::endl;
            sk->push(v3);
            return;
        }
        case OpCode::op_sub: {

            break;
        }
        case OpCode::op_mul: {

            break;
        }
        case OpCode::op_div: {

            break;
        }
        case OpCode::op_mod: {

            break;
        }

        case OpCode::op_binary_lm: {

            break;
        }
        case OpCode::op_binary_rm: {

            break;
        }
        case OpCode::op_binary_and: {

            break;
        }
        case OpCode::op_binary_or: {

            break;
        }
        case OpCode::op_binary_not: {

            break;
        }
        case OpCode::op_binary_xor: {

            break;
        }


        case OpCode::op_inc: {

            break;
        }
        case OpCode::op_dec: {

            break;
        }
        case OpCode::op_minus: {

            break;
        }


        case OpCode::op_cmp_and: {

            break;
        }
        case OpCode::op_cmp_or: {

            break;
        }
        case OpCode::op_cmp_not: {

            break;
        }
        case OpCode::op_cmp_eq: {

            break;
        }
        case OpCode::op_cmp_neq: {

            break;
        }
        case OpCode::op_cmp_gt: {

            break;
        }
        case OpCode::op_cmp_gte: {

            break;
        }
        case OpCode::op_cmp_lt: {

            break;
        }
        case OpCode::op_cmp_lte: {

            break;
        }


        case OpCode::op_or: {

            break;
        }
        case OpCode::op_pointor: {

            break;
        }

        case OpCode::op_test: {

            break;
        }

        case OpCode::op_index: {

            break;
        }
        case OpCode::op_new_array: {

            break;
        }
        case OpCode::op_sub_arr: {

            break;
        }
        case OpCode::op_new_mapping: {

            break;
        }
        case OpCode::op_upset: {

            break;
        }

        case OpCode::op_call: {

            break;
        }
        case OpCode::op_return: {

            break;
        }
        case OpCode::op_set_upvalue: {

            break;
        }
        case OpCode::op_get_upvalue: {

            break;
        }
        case OpCode::op_new_class: {

            break;
        }
        case OpCode::op_set_class_field: {

            break;
        }
        case OpCode::op_goto: {

            break;
        }
        case OpCode::op_switch_select: {

            break;
        }
        case OpCode::op_switch: {

            break;
        }

        case OpCode::op_foreach_step1: {

            break;
        }
        case OpCode::op_foreach_step2: {

            break;
        }
        case OpCode::op_foreach_step3: {

            break;
        }

        
        default:
            break;
        }
    }
}