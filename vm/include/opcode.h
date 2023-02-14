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

    op_add_assign,
};

#endif
