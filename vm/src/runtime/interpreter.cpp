#include <iostream>
#include <string>

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
#include "runtime/vm.h"

#define ERROR(msg) std::cout << msg << std::endl; lvm->traceback(); std::cout.flush(); exit(-1);

#define EXTRACT_2_PARAMS luint16_t idx = luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc)); pc += 2;
#define EXTRACT_2_PARAMS_1 luint16_t idx1 = luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc)); pc += 2;
#define EXTRACT_4_PARAMS luint32_t idx = luint8_t(*(pc + 3)) << 24 | luint8_t(*(pc + 2)) << 16 | luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc)); pc += 4;

#define OPER(op) \
    lpc_value_t *v1 = sk->pop(); \
    lpc_value_t *v2 = sk->pop();  \
    if (v1->type == value_type::int_ && v2->type == value_type::int_) { \
        v2->pval.number = v2->pval.number op v1->pval.number; \
    } else if (v1->type == value_type::float_ && v2->type == value_type::int_) { \
        v2->type = value_type::float_; \
        v2->pval.real = v2->pval.real op v1->pval.number; \
    } else if (v1->type == value_type::int_ && v2->type == value_type::float_) { \
        v2->pval.real = v2->pval.real op v1->pval.number; \
    } else if (v1->type == value_type::float_ && v2->type == value_type::float_){ \
        v2->pval.real = v2->pval.real op v1->pval.real; \
    } \
    sk->push(v2);

#define SOME_CMP(op) \
        lpc_value_t *val1 = sk->pop(); \
        lpc_value_t *val2 = sk->pop(); \
        const0.type = value_type::int_; \
        if (val1->type == value_type::int_ && val2->type == value_type::int_) { \
            const0.pval.number = val2->pval.number op val1->pval.number; \
        } else if (val1->type == value_type::int_ && val2->type == value_type::int_) { \
            const0.pval.number = val2->pval.real op val1->pval.real; \
        } else if (val1->type == value_type::int_ && val2->type == value_type::float_) { \
            const0.pval.number = val2->pval.number op val1->pval.real; \
        } else if (val1->type == value_type::float_ && val2->type == value_type::int_) { \
            const0.pval.number = val2->pval.real op val1->pval.number; \
        } else { \
            std::cout << "cant compare type: " << (int)val1->type << " with " << (int)val2->type  << ", "#op << std::endl; \
            exit(-1); \
        } \
        sk->push(&const0);

void vm::eval(lpc_vm_t *lvm)
{
    lpc_value_t const0;

new_frame:
    call_info_t *ci = lvm->get_call_info();
    lpc_stack_t *sk = lvm->get_stack();
    function_proto_t *fun;
    const char *pc = ci->savepc, *start;
    if (!ci->father) {
        if (ci->funcIdx >= 0) {
            start = ci->cur_obj->get_proto()->instructions;
            fun = &ci->cur_obj->get_proto()->func_table[ci->funcIdx];
        } else {
            start = ci->cur_obj->get_proto()->init_codes;
            fun = ci->cur_obj->get_proto()->init_fun;
        }
    } else {
        start = ci->father->instructions;
        fun = &ci->father->func_table[ci->funcIdx];
    }

    for(;;) {
        OpCode op = (OpCode)*(pc++);
        switch (op)
        {
        case OpCode::op_load_global: {
            EXTRACT_2_PARAMS
            if (idx >= ci->cur_obj->get_proto()->nvariable || idx < 0) {
                std::string msg = "invalid number to indexing glable varibal: " + std::to_string(idx);
                ERROR(msg);
            }
            lpc_value_t *val = &ci->cur_obj->get_locals()[idx + ci->inherit_offset];
            sk->push(val);
            break;
        }
        case OpCode::op_store_global: {
            EXTRACT_2_PARAMS
            if (idx >= ci->cur_obj->get_proto()->nvariable || idx < 0) {
                std::string msg = "invalid number to indexing glable varibal: " + std::to_string(idx);
                ERROR(msg);
            }
            lpc_value_t *val = &ci->cur_obj->get_locals()[idx + ci->inherit_offset];
            lpc_value_t *val1 = sk->pop();
            *val = *val1;
            break;
        }
        case OpCode::op_load_local: {
            EXTRACT_2_PARAMS
            if (idx >= fun->nlocal || idx < 0) {
                std::string msg = "invalid number to indexing local varibal: " + std::to_string(idx);
                ERROR(msg);
            }
            sk->push(ci->base + idx);
            break;
        }
        case OpCode::op_store_local: {
            EXTRACT_2_PARAMS
            if (idx >= fun->nlocal || idx < 0) {
                std::string msg = "invalid number to indexing local varibal: " + std::to_string(idx);
                ERROR(msg);
            }
            lpc_value_t *val = ci->base + idx;
            lpc_value_t *val1 = sk->pop();
            *val = *val1;
            break;
        }
        case OpCode::op_load_iconst: {
            EXTRACT_2_PARAMS
            const0.type = value_type::int_;
            if (ci->father) {
                if ((ci->father->niconst <= idx || idx < 0)) {
                    ERROR("error found: float const index over range!!!");
                }

                const0.pval.number = ci->father->iconst[idx].item.number;
            } else {
                if ((ci->cur_obj->get_proto()->niconst <= idx || idx < 0)) {
                    ERROR("error found: float const index over range!!!");
                }

                const0.pval.number = ci->cur_obj->get_proto()->iconst[idx].item.number;
            }

            sk->push(&const0);
            break;
        }
        case OpCode::op_load_fconst: {
            EXTRACT_2_PARAMS
            const0.type = value_type::float_;
            if (ci->father) {
                if ((ci->father->nfconst <= idx || idx < 0)) {
                    ERROR("error found: float const index over range!!!");
                }

                const0.pval.number = ci->father->iconst[idx].item.real;
            } else {
                if ((ci->cur_obj->get_proto()->nfconst <= idx || idx < 0)) {
                    ERROR("error found: float const index over range!!!");
                }

                const0.pval.real = ci->cur_obj->get_proto()->fconst[idx].item.real;
            }

            sk->push(&const0);
            break;
        }
        case OpCode::op_load_sconst: {
            EXTRACT_2_PARAMS
            const0.type = value_type::string_;
            if (ci->father) {
                if ((ci->father->nsconst <= idx || idx < 0)) {
                    ERROR("error found: string const index over range!!!");
                }

                lpc_string_t *s = ci->father->sconst[idx].item.str;
                const0.gcobj = reinterpret_cast<lpc_gc_object_t *>(s);
            } else {
                if ((ci->cur_obj->get_proto()->nsconst <= idx || idx < 0)) {
                    ERROR("error found: string const index over range!!!");
                }

                lpc_string_t *s = ci->cur_obj->get_proto()->sconst[idx].item.str;
                const0.gcobj = reinterpret_cast<lpc_gc_object_t *>(s);
            }

            sk->push(&const0);
            break;
        }
        case OpCode::op_load_func: {
            EXTRACT_2_PARAMS
            const0.type = value_type::function_;
            object_proto_t *proto = ci->cur_obj->get_proto();
            if (proto->nfunction <= idx || idx < 0) {
                ERROR("error found: function index over range!!!");
            }

            function_proto_t *f = &proto->func_table[idx];
            lpc_function_t *func = lvm->get_alloc()->allocate_function(f, ci->cur_obj, idx);
            const0.gcobj = reinterpret_cast<lpc_gc_object_t *>(func);
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
                v2->pval.number = v2->pval.number % v1->pval.number;
            } else {
                ERROR("error found!");
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
                ERROR("error found!");
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
                ERROR("error found!");
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
                ERROR("error found!");
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
                ERROR("error found!");
            }
            sk->push(v2);
            break;
        }
        case OpCode::op_binary_not: {
            lpc_value_t *v1 = sk->pop();
            if (v1->type ==  value_type::int_) {
                v1->pval.number = ~v1->pval.number;
            } else {
                ERROR("error found!");
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
                ERROR("error found!");
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
            const0.type = value_type::int_;
            if (val1->type == value_type::int_) {
                const0.pval.number = !val1->pval.number;
            } else if (val1->type == value_type::float_) {
                const0.pval.number = !val1->pval.real;
            } else {
                const0.pval.number = val1->subtype == value_type::null_ ? 1 : 0;
            }
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
                const0.pval.number = str1->get_hash() == str2->get_hash();
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
                const0.pval.number = str1->get_hash() != str2->get_hash();
            } else {
                const0.pval.number = val1->gcobj != val2->gcobj;
            }
            sk->push(&const0);
            break;
        }
        case OpCode::op_cmp_gt: {
            SOME_CMP(>)
            break;
        }
        case OpCode::op_cmp_gte: {
            SOME_CMP(>=)
            break;
        }
        case OpCode::op_cmp_lt: {
            SOME_CMP(<)
            break;
        }
        case OpCode::op_cmp_lte: {
            SOME_CMP(<=)
            break;
        }
        case OpCode::op_or: {
            lpc_value_t *val1 = sk->pop();
            lpc_value_t *val2 = sk->pop();
            if (val1->subtype == value_type::null_) {
                sk->push(val2);
            } else {
                if ((val1->type == value_type::int_ && val1->pval.number == 0) || (val1->type == value_type::float_ && val1->pval.real == 0)) {
                    sk->push(val2);
                } else {
                    sk->push(val1);
                }
            }
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
            lpc_value_t *key = sk->pop();
            lpc_value_t *con = sk->pop();
            if (con->type == value_type::mappig_) {
                lpc_mapping_t *mapping = reinterpret_cast<lpc_mapping_t *>(con->gcobj);
                lpc_value_t *val = mapping->get_value(key);
                if (!val) {
                    const0.subtype = value_type::null_;
                    sk->push(&const0);
                } else {
                    sk->push(val);
                }
            } else if (con->type == value_type::array_) {
                if (key->type != value_type::int_) {
                    ERROR("Only integer can index an array!!");
                }

                lpc_array_t *arr = reinterpret_cast<lpc_array_t *>(con->gcobj);
                if (arr->get_size() <= key->pval.number || key->pval.number < 0) {
                    std::string msg = "Negative indexing key: " + std::to_string(key->pval.number);
                    ERROR(msg);
                }

                lpc_value_t *val = arr->get(key->pval.number);
                sk->push(val);
            } else {
                std::string msg = "error type to index: " + std::to_string((int)con->type);
                ERROR(msg);
            }
            break;
        }
        case OpCode::op_new_array: {
            EXTRACT_4_PARAMS
            const0.type = value_type::array_;
            lpc_array_t *arr = lvm->get_alloc()->allocate_array(idx);
            for (int i = idx; i >= 1; --i) {
                arr->set(sk->pop(), i - 1);
            }
            const0.gcobj = reinterpret_cast<lpc_gc_object_t *>(arr);
            sk->push(&const0);
            break;
        }
        case OpCode::op_sub_arr: {
            lpc_value_t *end = sk->pop();
            lpc_value_t *con = sk->pop();
            lpc_value_t *start = sk->pop();
            if (con->type != value_type::array_) {
                ERROR("only array can be sub!!");
            }

            lpc_array_t *arr = reinterpret_cast<lpc_array_t *>(con->gcobj);
            if (start->pval.number < 0 || start->pval.number > end->pval.number || start->pval.number >= arr->get_size() || end->pval.number >= arr->get_size()) {
                ERROR("negtive param to sub an array!!");
            }

            const0.type = value_type::array_;
            lpc_array_t *tmp;
            if (start->pval.number != end->pval.number) {
                tmp = lvm->get_alloc()->allocate_array(0);
            } else {
                tmp = lvm->get_alloc()->allocate_array(end->pval.number - start->pval.number);
                for (int i = start->pval.number, j = 0; i <= end->pval.number; ++i, ++j) {
                    tmp->set(arr->get(i), j);
                }
            }

            const0.gcobj = reinterpret_cast<lpc_gc_object_t *>(tmp);
            sk->push(&const0);
            break;
        }
        case OpCode::op_new_mapping: {
            EXTRACT_4_PARAMS
            const0.type = value_type::mappig_;
            lpc_mapping_t *map = lvm->get_alloc()->allocate_mapping();
            for (int i = idx; i > 0; i -= 2) {
                lpc_value_t *v = sk->pop();
                lpc_value_t *k = sk->pop();
                map->set(k, v);
            }
            const0.gcobj = reinterpret_cast<lpc_gc_object_t *>(map);
            sk->push(&const0);
            break;
        }
        case OpCode::op_upset: {
            OpCode op = (OpCode)*(pc++);
            lpc_value_t *k = sk->pop();
            lpc_value_t *con = sk->pop();
            lpc_value_t *v = nullptr;
            if (op != OpCode::op_inc && op != OpCode::op_dec && op != OpCode::op_minus) {
                v = sk->pop();
            }

            if (con->type == value_type::array_) {
                if (k->type != value_type::int_) {
                    ERROR("error found!");
                }

                lpc_array_t *arr = reinterpret_cast<lpc_array_t *>(con->gcobj);
                bool res = arr->upset(v, k->pval.number, op);
                if (!res) {
                    ERROR("upset array failed!!");
                }
            } else if (con->type == value_type::mappig_) {
                lpc_mapping_t *map = reinterpret_cast<lpc_mapping_t *>(con->gcobj);
                bool res = map->upset(k, v, op);
                if (!res) {
                    ERROR("upset mapping failed!!");
                }
            } else {
                std::string msg = "error type to index: " + std::to_string((int)con->type);
                ERROR(msg);
            }
            break;
        }
        case OpCode::op_call: {
            lint8_t type = *(pc++);
            if (type != 0) {
                EXTRACT_2_PARAMS
                ci->savepc = pc;
                if (type == 3) {
                    lvm->new_frame(ci->cur_obj, idx);
                    goto new_frame;
                } else if (type == 2) {
                    // sfun
                    lvm->new_frame(lvm->get_sfun_object(), idx);
                    goto new_frame;
                } else if (type == 1) {
                    // efun
                    lint8_t nargs = *(pc++);
                    efun_t *efuns = lvm->get_efuns();
                    efuns[idx](lvm, nargs);
                }
            } else {
                lpc_value_t *val = sk->pop();
                if (val->type != value_type::function_) {
                    ERROR("error found!");
                }

                lpc_function_t *f = reinterpret_cast<lpc_function_t *>(val->gcobj);
                if (f->idx < 0) {
                    ERROR("error found!");
                }
                
                ci->savepc = pc;
                lvm->new_frame(ci->cur_obj, f->idx);
                goto new_frame;
            }
            break;
        }
        case OpCode::op_call_virtual: {
            EXTRACT_2_PARAMS
            EXTRACT_2_PARAMS_1
            object_proto_t *proto = ci->cur_obj->get_proto();
            if (proto->ninherit <= idx) {
                ERROR("cant index inherit table!!!");
            }

            ci->savepc = pc;
            const0.type = value_type::string_;
            const0.gcobj = reinterpret_cast<lpc_gc_object_t *>(proto->inherits[idx]);
            lpc_object_t *father = lvm->find_oject(&const0);
            call_info_t *call = lvm->new_frame(father, idx1);
            call->cur_obj = ci->cur_obj;
            call->father = father->get_proto();
            call->inherit_offset = proto->inherit_offsets[idx];
            goto new_frame;
        }
        case OpCode::op_return: {
            if (ci->call_other || ci->call_init) {
                lvm->pop_frame();
                return;
            }

            lvm->pop_frame();
            goto new_frame;
        }
        case OpCode::op_set_upvalue: {
            ERROR("error found!");
            break;
        }
        case OpCode::op_get_upvalue: {
            ERROR("error found!");
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
                ERROR("error found!");
            }

            lpc_array_t &arr = clazz->gcobj->arr;
            if (arr.get_size() <= idx) {
                ERROR("error found!");
            }
            arr.set(val, idx);
            break;
        }
        case OpCode::op_load_class_field: {
            EXTRACT_2_PARAMS
            lpc_value_t *clazz = sk->pop();
            lpc_value_t *val = sk->pop(); 
            if (clazz->type != value_type::array_ || clazz->subtype != value_type::class_) {
                ERROR("error found!");
            }
            lpc_array_t &arr = clazz->gcobj->arr;
            if (arr.get_size() <= idx) {
                ERROR("error found!");
            }
            const0 = *arr.get(idx);
            sk->push(&const0);
            break;
        }
        case OpCode::op_goto: {
            EXTRACT_4_PARAMS
            pc = start + idx;
            break;
        }
        case OpCode::op_switch: {
            EXTRACT_2_PARAMS
            lpc_value_t *val = sk->pop();
            if (val->type == value_type::string_) {
                lpc_string_t *str = reinterpret_cast<lpc_string_t *>(val->gcobj);
                const0.pval.number = str->get_hash();
            } else if (val->type == value_type::int_) {
                const0.pval.number = val->pval.number;
            } else {
                ERROR("error found!");
            }
            
            // 查找跳转表
            object_proto_t *proto = ci->cur_obj->get_proto();
            std::unordered_map<lint32_t, lint32_t> &map = (*proto->lookup_table)[idx];
            if (map.count(const0.pval.number)) {
                pc = start + map[const0.pval.number];
            } else if (proto->defaults->count(idx)) {
                pc = start + (*proto->defaults)[idx];
            } else {
                luint32_t idx1 = luint8_t(*(pc + 3)) << 24 | luint8_t(*(pc + 2)) << 16 | luint8_t(*(pc + 1)) << 8 | luint8_t(*(pc));
                pc = start + idx1;
            }
            break;
        }
        case OpCode::op_foreach_step1: {
            lpc_value_t *val = sk->top();
            // setup iterator
            if (val->type != value_type::array_ && val->type != value_type::mappig_) {
                std::string msg = "cant traverse type: " + std::to_string((lint32_t)val->type);
                ERROR(msg)
            }
            const0.type = value_type::int_;
            const0.pval.number = 0;
            sk->push(&const0);
            break;
        }
        case OpCode::op_foreach_step2: {
            lpc_value_t *iter = sk->pop();
            lpc_value_t *val = sk->top();
            lint8_t sz = *(pc++);
            EXTRACT_4_PARAMS

            if (val->type != value_type::array_ && val->type != value_type::mappig_) {
                ERROR("only array or mapping container can be traversed!!");
            }
            
            if (val->type == value_type::array_ && sz != 1) {
                ERROR("not array container to traverse!!");
            }

            if (val->type == value_type::mappig_ && sz != 2) {
                ERROR("not mapping container to traverse!!");
            }

            lint32_t index = iter->pval.number;
            ++iter->pval.number;
            sk->push(iter);

            if (sz > 1) {
                lpc_mapping_t *map = reinterpret_cast<lpc_mapping_t *>(val->gcobj);
                if (index >= map->get_size()) {
                    sk->pop();
                    pc = start + idx;
                    map->reset_iterator();
                    break;
                }

                bucket_t *b = map->iterate(index);
                sk->push(&b->pair[1]);
                sk->push(&b->pair[0]);
            } else {
                lpc_array_t *arr = reinterpret_cast<lpc_array_t *>(val->gcobj);
                if (index >= arr->get_size()) {
                    // 退出循环
                    sk->pop();
                    pc = start + idx;
                    break;
                }

                lpc_value_t *field = arr->get(index);
                sk->push(field);
            }
            break;
        }
        default:
            std::string msg = "unkown instrucion found: " + std::to_string((int)op);
            ERROR(msg)
            break;
        }
    }
}