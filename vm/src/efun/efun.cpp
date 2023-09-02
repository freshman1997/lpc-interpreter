#include <iostream>
#include <cstring>
#include <string>
#include <thread>
#include <ctime>

#include "lpc.h"
#include "runtime/vm.h"
#include "lpc_value.h"
#include "runtime/stack.h"
#include "type/lpc_array.h"
#include "type/lpc_string.h"

using namespace std;

extern void debug_message(const char *fmt, ...);

static void f_print(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->pop();
    lpc_string_t *str = reinterpret_cast<lpc_string_t *>(val->gcobj);
    debug_message("%s\n", str->get_str());
}

static void build_basic(string &buf, lpc_value_t *val, int deep = 0);

static void build_array(string &buf, lpc_array_t *arr, int deep)
{
    int sz = arr->get_size();
    if (!sz) {
        buf.append("[]");
        return;
    }

    buf.append("[ \n");

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
    int sz = map->get_size();
    if (sz == 0) {
        buf.append("{}");
        return;
    }

    buf.append("{ \n");
    for (int i = 0; i < map->get_size(); ++i) {
        bucket_t *cur = map->iterate(i);

        for (int t = 0; t < deep; ++t) {
            buf.push_back('\t');
        }

        build_basic(buf, &cur->pair[0]);
        buf.append(": ");
        build_basic(buf, &cur->pair[1], deep + 1);

        if (i < sz - 1) {
            buf.append(", \n");
        }
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
        cout << "zzzzzzzzzz" << val->pval.real << endl;
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
        if (val->subtype != value_type::class_) {
            build_array(buf, arr, deep);
        } else {
            buf.append("class(");
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
            buf.push_back(')');
        }
        break;
    }
    case value_type::mappig_: {
        lpc_mapping_t *m = reinterpret_cast<lpc_mapping_t *>(val->gcobj);
        build_mapping(buf, m, deep);
        break;
    }
    case value_type::object_:{
        lpc_object_t *obj = reinterpret_cast<lpc_object_t *>(val->gcobj);
        char tmp[50] = {0};
        auto ptr = reinterpret_cast<std::uintptr_t>(obj);
        sprintf(tmp, "object@0x%llx", ptr);
        buf.append(tmp);   
        break;
    }
    case value_type::function_: {
        lpc_function_t *f = reinterpret_cast<lpc_function_t *>(val->gcobj);
        char tmp[50] = {0};
        auto ptr = reinterpret_cast<std::uintptr_t>(f);
        sprintf(tmp, "function@0x%llx", ptr);
        buf.append(tmp);
        break;
    }
    case value_type::bool_:{
        if (val->pval.number != 0) {
            buf.append("true");
        } else {
            buf.append("false");
        }
        break;
    }
    
    default:
        break;
    }
}

static void f_puts(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->pop();
    string buf;
    build_basic(buf, val, 1);
    debug_message("%s\n", buf.c_str());
}

static void f_call_other(lpc_vm_t *vm, lint32_t nparam)
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

static void f_sleep(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->pop();
    if (val->type != value_type::int_) {
        cout << "only integer can call sleep !! \n";
        exit(-1);
    }

    this_thread::sleep_for(chrono::milliseconds(val->pval.number));
}

static void f_sizeof(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->top();
    val->pval.number = 0;
    
    if (val->type == value_type::string_){
        lpc_string_t *str = reinterpret_cast<lpc_string_t *>(val->gcobj);
        val->pval.number = str->get_size();
    } else if (val->type == value_type::array_) {
        lpc_array_t *arr = reinterpret_cast<lpc_array_t *>(val->gcobj);
        val->pval.number = arr->get_size();
    } else if (val->type == value_type::mappig_) {
        lpc_array_t *arr = reinterpret_cast<lpc_array_t *>(val->gcobj);
        val->pval.number = arr->get_size();
    }
    
    val->type = value_type::int_;
}

static void f_random(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->top();
    if (val->type != value_type::int_) {
        val->pval.number = 0;
        return;
    }

    // TODO 
    unsigned char rs[4];
    rs[0] = rand() % 256;
    rs[1] = rand() % 256;
    rs[2] = rand() % 256;
    rs[3] = rand() % 256;

    int r = *(int*)rs;
    if (r < 0) r = -r;

    val->pval.number = r % val->pval.number;
}

static void mapping_kvs(lpc_vm_t *vm, lint32_t nparam, bool key)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->pop();
    if (val->type != value_type::mappig_) {
        // TODO
        return;
    }

    lpc_mapping_t *map = reinterpret_cast<lpc_mapping_t *>(val->gcobj);
    lpc_array_t *arr = nullptr;
    if (key) {
       arr = mapping_keys(map, vm->get_alloc());
    } else {
        arr = mapping_values(map, vm->get_alloc());
    }

    lpc_value_t tmp;
    tmp.type = value_type::array_;
    tmp.gcobj = reinterpret_cast<lpc_gc_object_t *>(arr);
    sk->push(&tmp);
}

static void f_keys(lpc_vm_t *vm, lint32_t nparam)
{
    mapping_kvs(vm, nparam, true);
}

static void f_values(lpc_vm_t *vm, lint32_t nparam)
{
    mapping_kvs(vm, nparam, false);
}

void init_efuns(lpc_vm_t *vm)
{
    efun_t *efuns = new efun_t[8];
    efuns[0] = f_call_other;
    efuns[1] = f_print;
    efuns[2] = f_puts;
    efuns[3] = f_sleep;
    efuns[4] = f_sizeof;
    efuns[5] = f_random;
    efuns[6] = f_keys;
    efuns[7] = f_values;

    vm->register_efun(efuns);
}

