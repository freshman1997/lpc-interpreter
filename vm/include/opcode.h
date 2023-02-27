#ifndef __VM_OPCODE_H__
#define __VM_OPCODE_H__

enum class OpCode
{
    op_load_global,
    op_load_local,

    op_load_const,
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

    op_pointor,                 // ->
    op_dup,                     // copy ref

    op_test,

    op_index,

    op_new_array,
    op_sub_arr,

    op_new_mapping,
    op_get_or_set_mapping,

    op_call_efun,               // for call native methods
    op_call_sefun,              // for the specified lpc object methods
    op_call_lfun,               // for object's local methods (include private and public methods)
    op_call_virtaul,            // 

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
