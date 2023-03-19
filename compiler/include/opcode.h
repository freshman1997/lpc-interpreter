#ifndef __VM_OPCODE_H__
#define __VM_OPCODE_H__

enum class OpCode
{
    op_load_global,
    op_load_local,
    op_store_global,
    op_store_local,

    op_load_class_field,
    op_store_class_field,

    op_load_iconst,
    op_load_fconst,
    op_load_sconst,
    
    op_load_0,                  // for false, 0 
    op_load_1,                  // for true

    op_add,
    op_sub,
    op_mul,
    op_div,
    op_mod,

    op_binary_lm,               // <<
    op_binary_rm,               // >>
    op_binary_and,              // & 
    op_binary_or,               // | 
    op_binary_not,              // ~
    op_binary_xor,              // ^

    op_inc,
    op_dec,
    op_minus,

    op_cmp_and,
    op_cmp_or,
    op_cmp_not,
    op_cmp_eq,
    op_cmp_neq,
    op_cmp_gt,
    op_cmp_gte,
    op_cmp_lt,
    op_cmp_lte,

    op_or,

    op_test,                    // 一个参数，4个字节

    op_index,

    op_new_array,
    op_sub_arr,

    op_new_mapping,
    op_upset,                   // 参数：1、不存在则插入，2、获取并入栈

    op_call,                    // 参数：1、efun，2、sfun，3、local

    op_return,

    op_set_upvalue,
    op_get_upvalue,

    op_new_class,
    op_set_class_field,         // h->id

    op_goto,                    // 一个参数，4个字节

    op_switch,                  // TODO  table switch implement

    op_foreach_step1,           // setup iterator, init first or next
    op_foreach_step2,           // assign
    op_foreach_step3,           // remove from stack

};

#endif
