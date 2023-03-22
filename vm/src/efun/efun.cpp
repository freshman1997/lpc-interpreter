#include <iostream>
#include <cstring>

#include "lpc.h"
#include "runtime/vm.h"
#include "lpc_value.h"
#include "runtime/stack.h"

using namespace std;

extern void debug_message(const char *fmt, ...);

static void print(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->pop();
    lpc_string_t *str = reinterpret_cast<lpc_string_t *>(val->gcobj);
    debug_message("%s\n", str->get_str());
}

static void call_other(lpc_vm_t *vm, lint32_t nparam)
{
    nparam -= 2;
    lpc_stack_t *sk = vm->get_stack();
    int idx = sk->get_idx();

    // 最开始的位置是对象名，接下来是函数名
    lpc_value_t *funcName = sk->get(idx - nparam - 1);
    lpc_string_t *fNameStr = reinterpret_cast<lpc_string_t *>(funcName->gcobj);
    if (funcName->type != value_type::string_) {
        cout << "Expecting a function name is string type but not!!" << endl;
        exit(-1);
    }

    // 入参
    lpc_value_t *object_name = sk->get(idx - nparam - 2);
    if (object_name->type != value_type::string_) {
        cout << "Not a string value to find object !!" << endl;
        exit(-1);
    }

    lpc_object_t *obj = vm->find_oject(object_name);
    if (!obj) {
        cout << "internal error !\n";
        exit(-1);
    }

    object_proto_t *proto = obj->get_proto();

    int funIdx = -1;
    for (int i = 0; i < proto->nfunction; ++i) {
        if (0 == strcmp(proto->func_table[i].name, fNameStr->get_str()) && !proto->func_table[i].is_static) {
            funIdx = i;
            break;
        }
    }

    if (funIdx < 0) {
        lpc_string_t *objName = reinterpret_cast<lpc_string_t *>(object_name->gcobj);
        cout << "no such function found: " << fNameStr->get_str() << " in object " << objName->get_str() << endl;
        exit(-1);
    }

    sk->pop_n(nparam + 2);

    call_info_t *ci = vm->new_frame(obj, -funIdx);

    // 初始化传入的参数
    for (int i = nparam; i > 0; --i) {
        *(ci->base + i) = *sk->pop();
    }

    vm->run();
}

void init_efuns(lpc_vm_t *vm)
{
    efun_t *efuns = new efun_t[2];
    efuns[1] = print;
    efuns[0] = call_other;
    vm->register_efun(efuns);
}

