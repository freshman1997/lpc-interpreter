#include <iostream>
#include <cstring>
#include <string>
#include <thread>

#include "lpc.h"
#include "runtime/vm.h"
#include "lpc_value.h"
#include "runtime/stack.h"
#include "type/lpc_string.h"

using namespace std;

extern void debug_message(const char *fmt, ...);

static void print(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->pop();
    lpc_string_t *str = reinterpret_cast<lpc_string_t *>(val->gcobj);
    debug_message("%s\n", str->get_str());
}

static void build_basic(string &buf, lpc_value_t *val, int deep = 0);

static void build_array(string &buf, lpc_array_t *arr, int deep)
{
    buf.append("[ \n");
    int sz = arr->get_size();

    for (int i = 0; i < sz; ++i) {
        for (int t = 0; t < deep; ++t) {
            buf.push_back('\t');
        }
        build_basic(buf, arr->get(i), deep + 1);
        if (i < sz - 1) {
            buf.append(",\n");
        }
    }

    buf.push_back('\n');
    for (int t = 0; t < deep - 1; ++t) {
        buf.push_back('\t');
    }
    buf.push_back(']');
}

static void build_mapping(string &buf, lpc_mapping_t *map, int deep)
{
    buf.append("{ \n");
    
    int sz = map->get_size();
    if (sz > 0 && !map->get_begin()) {
        cout << "internal error!!!\n";
        exit(-1);
    }
    
    bucket_t *cur = map->get_begin();
    int i = 0;
    while (cur) {
        for (int t = 0; t < deep; ++t) {
            buf.push_back('\t');
        }

        build_basic(buf, &cur->pair[0]);
        buf.append(": ");
        build_basic(buf, &cur->pair[1], deep + 1);

        if (i < sz - 1) {
            buf.append(", \n");
        }

        ++i;
        cur = cur->next;
    }

    buf.push_back('\n');
    for (int t = 0; t < deep - 1; ++t) {
        buf.push_back('\t');
    }
    buf.push_back('}');
}

static void build_basic(string &buf, lpc_value_t *val, int deep)
{
    switch (val->type)
    {
    case value_type::int_:
        buf.append(std::to_string(val->pval.number));
        break;
    case value_type::float_:
        buf.append(std::to_string(val->pval.real));
        break;
    case value_type::string_: {
        lpc_string_t *str = reinterpret_cast<lpc_string_t *>(val->gcobj);
        buf.push_back('"');
        buf.append(str->get_str());
        buf.push_back('"');
        break;
    }
    case value_type::array_:{
        lpc_array_t *arr = reinterpret_cast<lpc_array_t *>(val->gcobj);
        build_array(buf, arr, deep);
        break;
    }
    case value_type::mappig_: {
        lpc_mapping_t *m = reinterpret_cast<lpc_mapping_t *>(val->gcobj);
        build_mapping(buf, m, deep);
        break;
    }
    case value_type::object_:
        
        break;
    case value_type::function_:
        
        break;
    
    default:
        break;
    }
}

static void puts(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->pop();
    string buf;
    build_basic(buf, val, 1);
    debug_message("%s\n", buf.c_str());
}

static void call_other(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();

    // 最后的位置是对象名，接下来是函数名
    lpc_value_t *object_name = sk->pop();
    lpc_value_t *funcName = sk->pop();
    lpc_string_t *fNameStr = reinterpret_cast<lpc_string_t *>(funcName->gcobj);
    if (funcName->type != value_type::string_) {
        cout << "Expecting a function name is string type but not!!" << endl;
        exit(-1);
    }

    // 入参
    
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
        cout << "no such function: " << fNameStr->get_str() << " in object " << objName->get_str() << endl;
        exit(-1);
    }

    call_info_t *ci = vm->new_frame(obj, -funIdx);
    ci->call_other = true;

    // 修正位置
    ci->base = sk->top() - (nparam - 2);

    vm->run();
}

static void sleep(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->pop();
    if (val->type != value_type::int_) {
        cout << "only integer can call sleep !! \n";
        exit(-1);
    }

    this_thread::sleep_for(chrono::milliseconds(val->pval.number));
}

void init_efuns(lpc_vm_t *vm)
{
    efun_t *efuns = new efun_t[4];
    efuns[0] = call_other;
    efuns[1] = print;
    efuns[2] = puts;
    efuns[3] = sleep;
    vm->register_efun(efuns);
}

