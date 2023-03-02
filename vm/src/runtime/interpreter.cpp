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

    for(;;) {
        OpCode op = (OpCode)*(sp++);
        switch (op)
        {
        case OpCode::op_push: {
            lpc_value_t *v = sk->get(0);
            v->type = value_type::int_;
            v->val.number = 100;
            sk->push(v);
            break;
        }
        case OpCode::op_load_const: {
            lpc_value_t *v = sk->get(1);
            v->type = value_type::int_;
            v->val.number = 200;
            sk->push(v);
            break;
        }
        case OpCode::op_add: {
            lpc_value_t *v1 = sk->pop();
            lpc_value_t *v2 = sk->pop();
            lpc_value_t *v3 = sk->get(2);
            v3->type = value_type::int_;
            v3->val.number = v1->val.number + v2->val.number;
            std::cout << "numbers: " << v1->val.number << ", " << v2->val.number << ", " << v3->val.number << std::endl;
            sk->push(v3);
            return;
        }
        
        default:
            break;
        }
    }
}