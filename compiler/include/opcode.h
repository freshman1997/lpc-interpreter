#ifndef __VM_OPCODE_H__
#define __VM_OPCODE_H__

enum class OpCode
{
    op_load_global,
    op_load_local,

    op_load_iconst,
    op_load_fconst,
    op_load_sconst,
    op_load_0,                  // for false, 0 
    op_load_1,                  // for true

    op_push,
    op_pop,

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

    op_assign,
    op_add_assign,
    op_sub_assign,
    op_mul_assign,
    op_div_assign,
    op_mod_assign,
    op_binary_lm_assign,
    op_binary_rm_assign,
    op_binary_and_assign,
    op_binary_or_assign,
    op_binary_xor_assign,

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
    op_pointor,                 // ->

    op_test,

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

    op_goto,

    op_enter_block,             // for for loop, foreach loop block
    op_exit_block,

    op_switch_select,
    op_switch,                  // TODO  table switch implement

    op_foreach_step1,           // setup iterator
    op_foreach_step2,           // assign
    op_foreach_step3,           // next

};

#endif