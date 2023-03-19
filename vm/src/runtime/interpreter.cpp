#include <iostream>

#include "opcode.h"
#include "lpc.h"
#include "lpc_value.h"
#include "type/lpc_object.h"
#include "type/lpc_proto.h"
#include "type/lpc_array.h"
#include "runtime/interpreter.h"
#include "runtime/stack.h"
#include "memory/memory.h"
#include "type/lpc_string.h"

#define EXTRACT_2_PARAMS lint16_t idx = *(pc) | *(pc + 1); pc += 2;
#define EXTRACT_4_PARAMS lint32_t idx = *(pc) | *(pc + 1) | *(pc + 2) | *(pc + 3); pc += 4;

#define OPER(op) \
    lpc_value_t *v1 = sk->pop(); \
    lpc_value_t *v2 = sk->pop();  \
    if (v1->type == value_type::int_ && v2->type == value_type::int_) { \
        v2->pval.number = v1->pval.number op v2->pval.number; \
    } else if (v1->type == value_type::float_ && v2->type == value_type::int_) { \
        v2->type = value_type::float_; \
        v2->pval.real = v1->pval.real op v2->pval.number; \
    } else if (v1->type == value_type::int_ && v2->type == value_type::float_) { \
        v2->pval.real = v1->pval.real op v2->pval.number; \
    } else if (v1->type == value_type::float_ && v2->type == value_type::float_){ \
        v2->pval.real = v1->pval.real op v2->pval.real; \
    } \
    sk->push(v2);

void vm::eval(lpc_vm_t *lvm)
{
    lpc_value_t const0;

new_frame:
    call_info_t *ci = lvm->get_call_info();
    lpc_stack_t *sk = lvm->get_stack();
    const char *start = ci->cur_obj->get_proto()->instructions;
    const char *pc = ci->savepc;
    lint32_t sz = ci->cur_obj->get_proto()->instruction_size;
    function_proto_t *fun = &ci->cur_obj->get_proto()->func_table[ci->funcIdx];
    sk->set_local_size(fun->nlocal);

    for(;;) {
        std::cout << "diff: " << pc - start << std::endl;
        OpCode op = (OpCode)*(pc++);
        if (pc - start > sz) {
            std::cout << "Exit normally.\n";
            break;
        }

        switch (op)
        {
        case OpCode::op_load_global: {
            EXTRACT_2_PARAMS
            lpc_value_t *val = &ci->cur_obj->get_locals()[idx];
            sk->push(val);
            break;
        }
        case OpCode::op_store_global: {
            EXTRACT_2_PARAMS
            lpc_value_t *val = &ci->cur_obj->get_locals()[idx];
            lpc_value_t *val1 = sk->pop();
            *val = *val1;
            break;
        }
        case OpCode::op_load_local: {
            EXTRACT_2_PARAMS
            sk->push(ci->base + idx);
            break;
        }
        case OpCode::op_store_local: {
            EXTRACT_2_PARAMS
            lpc_value_t *val = ci->base + idx;
            lpc_value_t *val1 = sk->pop();
            *val = *val1;
            break;
        }

        case OpCode::op_load_iconst: {
            EXTRACT_2_PARAMS
            const0.type = value_type::int_;
            const0.pval.number = ci->cur_obj->get_proto()->iconst[idx].item.number;
            sk->push(&const0);
            break;
        }
        case OpCode::op_load_fconst: {
            EXTRACT_2_PARAMS
            const0.type = value_type::float_;
            const0.pval.real = ci->cur_obj->get_proto()->fconst[idx].item.real;
            sk->push(&const0);
            break;
        }
        case OpCode::op_load_sconst: {
            EXTRACT_2_PARAMS
            const0.type = value_type::string_;
            lpc_string_t *s = lvm->get_alloc()->allocate_string(ci->cur_obj->get_proto()->sconst[idx].item.str);
            const0.gcobj = reinterpret_cast<lpc_gc_object_t *>(s);
            sk->push(&const0);
            break;
        }

        case OpCode::op_load_0: {
            const0.type = value_type::int_;
            const0.pval.number = 0;
            sk->push(&const0);
            break;
        }
        case OpCode::op_load_1: {
            const0.type = value_type::int_;
            const0.pval.number = 1;
            sk->push(&const0);
            break;
        }

        case OpCode::op_add: {
            OPER(+)
            break;
        }
        case OpCode::op_sub: {
            OPER(-)
            break;
        }
        case OpCode::op_mul: {
            OPER(*)
            break;
        }
        case OpCode::op_div: {
            OPER(/)
            break;
        }
        case OpCode::op_mod: {
            lpc_value_t *v1 = sk->pop();
            lpc_value_t *v2 = sk->pop();
            if (v1->type ==  value_type::int_ && v2->type == value_type::int_) {
                v2->pval.number = v1->pval.number % v2->pval.number;
            } else {
                // TODO report error
            }
            sk->push(v2);
            break;
        }

        case OpCode::op_binary_lm: {
            lpc_value_t *v1 = sk->pop();
            lpc_value_t *v2 = sk->pop();
            if (v1->type ==  value_type::int_ && v2->type == value_type::int_) {
                v2->pval.number = v1->pval.number << v2->pval.number;
            } else {
                // TODO report error
            }
            sk->push(v2);
            break;
        }
        case OpCode::op_binary_rm: {
            lpc_value_t *v1 = sk->pop();
            lpc_value_t *v2 = sk->pop();
            if (v1->type ==  value_type::int_ && v2->type == value_type::int_) {
                v2->pval.number = v1->pval.number >> v2->pval.number;
            } else {
                // TODO report error
            }
            sk->push(v2);
            break;
        }
        case OpCode::op_binary_and: {
            lpc_value_t *v1 = sk->pop();
            lpc_value_t *v2 = sk->pop();
            if (v1->type ==  value_type::int_ && v2->type == value_type::int_) {
                v2->pval.number = v1->pval.number & v2->pval.number;
            } else {
                // TODO report error
            }
            sk->push(v2);
            break;
        }
        case OpCode::op_binary_or: {
            lpc_value_t *v1 = sk->pop();
            lpc_value_t *v2 = sk->pop();
            if (v1->type ==  value_type::int_ && v2->type == value_type::int_) {
                v2->pval.number = v1->pval.number | v2->pval.number;
            } else {
                // TODO report error
            }
            sk->push(v2);
            break;
        }
        case OpCode::op_binary_not: {
            lpc_value_t *v1 = sk->pop();
            if (v1->type ==  value_type::int_) {
                v1->pval.number = ~v1->pval.number;
            } else {
                // TODO report error
            }
            sk->push(v1);
            break;
        }
        case OpCode::op_binary_xor: {
            lpc_value_t *v1 = sk->pop();
            lpc_value_t *v2 = sk->pop();
            if (v1->type ==  value_type::int_ && v2->type == value_type::int_) {
                v2->pval.number = v1->pval.number ^ v2->pval.number;
            } else {
                // TODO report error
            }
            sk->push(v2);
            break;
        }

        case OpCode::op_inc: {
            lpc_value_t *val = sk->pop();
            if (val->type == value_type::int_) {
                ++val->pval.number;
            } else if (val->type == value_type::float_){
                ++val->pval.real;
            }
            sk->push(val);
            break;
        }
        case OpCode::op_dec: {
            lpc_value_t *val = sk->pop();
            if (val->type == value_type::int_) {
                --val->pval.number;
            } else if (val->type == value_type::float_){
                --val->pval.real;
            }
            sk->push(val);
            break;
        }
        case OpCode::op_minus: {
            lpc_value_t *val = sk->pop();
            if (val->type == value_type::int_) {
                val->pval.number = -val->pval.number;
            } else if (val->type == value_type::float_){
                val->pval.real = -val->pval.real;
            }
            sk->push(val);
            break;
        }

        case OpCode::op_cmp_and: {
            lpc_value_t *val1 = sk->pop();
            lpc_value_t *val2 = sk->pop();
            const0.type = value_type::int_;
            const0.pval.number = val1->pval.number != 0 && val2->pval.number != 0;
            sk->push(&const0);
            break;
        }
        case OpCode::op_cmp_or: {
            lpc_value_t *val1 = sk->pop();
            lpc_value_t *val2 = sk->pop();
            const0.type = value_type::int_;
            const0.pval.number = val1->pval.number != 0 || val2->pval.number != 0;
            sk->push(&const0);
            break;
        }
        case OpCode::op_cmp_not: {
            lpc_value_t *val1 = sk->pop();
            lpc_value_t *val2 = sk->pop();
            const0.type = value_type::int_;
            const0.pval.number = val1->pval.number != 0 || val2->pval.number != 0;
            sk->push(&const0);
            break;
        }
        case OpCode::op_cmp_eq: {
            lpc_value_t *val1 = sk->pop();
            lpc_value_t *val2 = sk->pop();
            const0.type = value_type::int_;
            if (val1->type == value_type::int_ && val2->type == value_type::int_) {
                const0.pval.number = val1->pval.number == val2->pval.number;
            } else if (val1->type == value_type::int_ && val2->type == value_type::int_) {
                const0.pval.number = val1->pval.real == val2->pval.real;
            } else if (val1->type == value_type::int_ && val2->type == value_type::float_) {
                const0.pval.number = val1->pval.number == val2->pval.real;
            } else if (val1->type == value_type::float_ && val2->type == value_type::int_) {
                const0.pval.number = val1->pval.real == val2->pval.number;
            } else if (val1->type == value_type::string_ && val2->type == value_type::string_) {
                lpc_string_t *str1 = reinterpret_cast<lpc_string_t *>(val1->gcobj);
                lpc_string_t *str2 = reinterpret_cast<lpc_string_t *>(val2->gcobj);

                // TODO
            } else {
                const0.pval.number = val1->gcobj == val2->gcobj;
            }
            sk->push(&const0);
            break;
        }
        case OpCode::op_cmp_neq: {
            lpc_value_t *val1 = sk->pop();
            lpc_value_t *val2 = sk->pop();
            const0.type = value_type::int_;
            if (val1->type == value_type::int_ && val2->type == value_type::int_) {
                const0.pval.number = val1->pval.number != val2->pval.number;
            } else if (val1->type == value_type::int_ && val2->type == value_type::int_) {
                const0.pval.number = val1->pval.real != val2->pval.real;
            } else if (val1->type == value_type::int_ && val2->type == value_type::float_) {
                const0.pval.number = val1->pval.number != val2->pval.real;
            } else if (val1->type == value_type::float_ && val2->type == value_type::int_) {
                const0.pval.number = val1->pval.real != val2->pval.number;
            } else if (val1->type == value_type::string_ && val2->type == value_type::string_) {
                lpc_string_t *str1 = reinterpret_cast<lpc_string_t *>(val1->gcobj);
                lpc_string_t *str2 = reinterpret_cast<lpc_string_t *>(val2->gcobj);

                // TODO
            } else {
                const0.pval.number = val1->gcobj != val2->gcobj;
            }
            sk->push(&const0);
            break;
        }
        case OpCode::op_cmp_gt: {
            lpc_value_t *val1 = sk->pop();
            lpc_value_t *val2 = sk->pop();
            const0.type = value_type::int_;
            if (val1->type == value_type::int_ && val2->type == value_type::int_) {
                const0.pval.number = val2->pval.number > val1->pval.number;
            } else if (val1->type == value_type::int_ && val2->type == value_type::int_) {
                const0.pval.number = val2->pval.real > val1->pval.real;
            } else if (val1->type == value_type::int_ && val2->type == value_type::float_) {
                const0.pval.number = val2->pval.number > val1->pval.real;
            } else if (val1->type == value_type::float_ && val2->type == value_type::int_) {
                const0.pval.number = val2->pval.real > val1->pval.number;
            } else {
                // TODO error
            }
            sk->push(&const0);
            break;
        }
        case OpCode::op_cmp_gte: {
            lpc_value_t *val1 = sk->pop();
            lpc_value_t *val2 = sk->pop();
            const0.type = value_type::int_;
            if (val1->type == value_type::int_ && val2->type == value_type::int_) {
                const0.pval.number = val2->pval.number >= val1->pval.number;
            } else if (val1->type == value_type::int_ && val2->type == value_type::int_) {
                const0.pval.number = val2->pval.real >= val1->pval.real;
            } else if (val1->type == value_type::int_ && val2->type == value_type::float_) {
                const0.pval.number = val2->pval.number >= val1->pval.real;
            } else if (val1->type == value_type::float_ && val2->type == value_type::int_) {
                const0.pval.number = val2->pval.real >= val1->pval.number;
            } else {
                // TODO error
            }
            sk->push(&const0);
            break;
        }
        case OpCode::op_cmp_lt: {
            lpc_value_t *val1 = sk->pop();
            lpc_value_t *val2 = sk->pop();
            const0.type = value_type::int_;
            if (val1->type == value_type::int_ && val2->type == value_type::int_) {
                const0.pval.number = val2->pval.number < val1->pval.number;
            } else if (val1->type == value_type::int_ && val2->type == value_type::int_) {
                const0.pval.number = val2->pval.real < val1->pval.real;
            } else if (val1->type == value_type::int_ && val2->type == value_type::float_) {
                const0.pval.number = val2->pval.number < val1->pval.real;
            } else if (val1->type == value_type::float_ && val2->type == value_type::int_) {
                const0.pval.number = val2->pval.real < val1->pval.number;
            } else {
                // TODO error
            }
            sk->push(&const0);
            break;
        }
        case OpCode::op_cmp_lte: {
            lpc_value_t *val1 = sk->pop();
            lpc_value_t *val2 = sk->pop();
            const0.type = value_type::int_;
            if (val1->type == value_type::int_ && val2->type == value_type::int_) {
                const0.pval.number = val2->pval.number <= val1->pval.number;
            } else if (val1->type == value_type::int_ && val2->type == value_type::int_) {
                const0.pval.number = val2->pval.real <= val1->pval.real;
            } else if (val1->type == value_type::int_ && val2->type == value_type::float_) {
                const0.pval.number = val2->pval.number <= val1->pval.real;
            } else if (val1->type == value_type::float_ && val2->type == value_type::int_) {
                const0.pval.number = val2->pval.real <= val1->pval.number;
            } else {
                // TODO error
            }
            sk->push(&const0);
            break;
        }


        case OpCode::op_or: {
            lpc_value_t *val1 = sk->pop();
            lpc_value_t *val2 = sk->pop();
            break;
        }

        case OpCode::op_test: {
            lpc_value_t *val = sk->pop();
            EXTRACT_4_PARAMS
            if (!val->pval.number) {  // false 跳走
                pc = start + idx;
            }
            break;
        }

        case OpCode::op_index: {
            lpc_value_t *val1 = sk->pop();
            lpc_value_t *val2 = sk->pop();
            break;
        }
        case OpCode::op_new_array: {
            EXTRACT_4_PARAMS
            const0.type = value_type::array_;
            lpc_array_t *arr = lvm->get_alloc()->allocate_array(idx);
            for (int i = 0; i < idx; ++i) {
                arr->set(sk->pop(), i, OpCode::op_load_global);
            }
            const0.gcobj = reinterpret_cast<lpc_gc_object_t *>(arr);
            break;
        }
        case OpCode::op_sub_arr: {

            break;
        }
        case OpCode::op_new_mapping: {
            EXTRACT_4_PARAMS
            const0.type = value_type::mappig_;
            lpc_mapping_t *map = lvm->get_alloc()->allocate_mapping();
            for (int i = 0; i < idx; ++i) {
                lpc_value_t *v = sk->pop();
                lpc_value_t *k = sk->pop();
                map->set(k, v);
            }
            const0.gcobj = reinterpret_cast<lpc_gc_object_t *>(map);
            break;
        }
        case OpCode::op_upset: {
            lpc_value_t *v = sk->pop();
            lpc_value_t *k = sk->pop();
            lpc_value_t *con = sk->pop();
            lint8_t op = *(pc++);
            if (con->type == value_type::array_) {
                if (k->type != value_type::int_) {
                    // TODO error
                }

                lpc_array_t *arr = reinterpret_cast<lpc_array_t *>(con->gcobj);
                arr->set(v, k->pval.number, OpCode(op));
            } else if (con->type == value_type::mappig_) {
                lpc_mapping_t *map = reinterpret_cast<lpc_mapping_t *>(con->gcobj);
                map->set(k, v);
            } else {
                // TODO error
            }
            break;
        }

        case OpCode::op_call: {
            lint8_t type = *(pc++);
            EXTRACT_2_PARAMS
            if (type == 3) {
                ci->savepc = pc;
                lvm->new_frame(ci->cur_obj, idx);
                goto new_frame;
            } else if (type == 2) {
                // sfun

            } else if (type == 1) {
                // efun
                lint8_t nargs = *(pc++);

            }
            break;
        }
        case OpCode::op_return: {
            ci->savepc = pc;
            lvm->pop_frame();
            goto new_frame;
            break;
        }

        case OpCode::op_set_upvalue: {

            break;
        }
        case OpCode::op_get_upvalue: {

            break;
        }

        case OpCode::op_new_class: {
            EXTRACT_2_PARAMS
            class_proto_t &cl = ci->cur_obj->get_proto()->class_table[idx];
            lpc_array_t *clazz = lvm->get_alloc()->allocate_array(cl.nfield);
            const0.type = value_type::array_;
            const0.subtype = value_type::class_;
            const0.gcobj = reinterpret_cast<lpc_gc_object_t *>(clazz);
            sk->push(&const0);
            break;
        }
        case OpCode::op_set_class_field: {
            EXTRACT_2_PARAMS
            lpc_value_t *clazz = sk->pop();
            lpc_value_t *val = sk->pop(); 
            if (clazz->type != value_type::array_ || clazz->subtype != value_type::class_) {
                // TODO
                std::cout << "error found!\n";
                exit(-1);
            }

            lpc_array_t &arr = clazz->gcobj->arr;
            if (arr.get_size() <= idx) {
                // TODO
            }
            arr.set(val, idx, OpCode::op_load_global);
            break;
        }
        case OpCode::op_goto: {
            EXTRACT_4_PARAMS
            pc = start + idx;
            break;
        }
        case OpCode::op_switch: {
            lpc_value_t *val = sk->pop();
            if (val->type == value_type::string_) {
                lpc_string_t *str = reinterpret_cast<lpc_string_t *>(val->gcobj);
                const0.pval.number = str->get_hash();
            } else if (val->type == value_type::int_) {
                const0.pval.number = val->pval.number;
            } else {
                // TODO report error
            }
            // TODO 查找跳转表
            
            break;
        }

        case OpCode::op_foreach_step1: {
            lpc_value_t *val = sk->top();
            // setup iterator
            if (val->type == value_type::array_) {

            } else if (val->type == value_type::mappig_) {

            } else {
                // TODO report error
            }
            break;
        }
        case OpCode::op_foreach_step2: {
            lpc_value_t *val = sk->top();
            lint8_t i = *(pc++);

            if (val->type == value_type::array_) {

            } else if (val->type == value_type::mappig_) {

            } else {
                // TODO report error
            }
            sk->push(&const0);
            break;
        }
        case OpCode::op_foreach_step3: {
            sk->pop();
            break;
        }

        default:
            std::cout << "xxxxxxxxxxxxxxxxxx \n";
            break;
        }
    }
}