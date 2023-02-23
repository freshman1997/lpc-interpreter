#ifndef __VM_OPCODE_H__
#define __VM_OPCODE_H__

enum class OpCode
{
    op_push,
    op_pop,

    op_add,
    op_sub,
    op_mul,
    op_div,
    op_mod,

    op_assign,
    op_add_assign,
    op_sub_assign,
    op_mul_assign,
    op_div_assign,
    op_mod_assign,

    op_sub_arr,
    op_index,

};

#endif
