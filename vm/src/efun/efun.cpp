#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <cstring>
#include <string>
#include <thread>
#include <ctime>
#include <vector>
#include <cmath>
#include <algorithm>

#include "lpc.h"
#include "runtime/vm.h"
#include "lpc_value.h"
#include "runtime/stack.h"
#include "type/lpc_array.h"
#include "type/lpc_string.h"

using namespace std;

namespace {

static void EfunPanic(lpc_vm_t *vm, const std::string &msg)
{
    std::cout << msg << std::endl;
    if (vm && vm->non_fatal_mode()) {
        vm->set_last_error(msg);
        throw vm_abort_signal();
    }
    exit(-1);
}

} // namespace

extern void debug_message(const char *fmt, ...);

static void f_print(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->pop();
    lpc_string_t *str = reinterpret_cast<lpc_string_t *>(val->get_gcobj());
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
    switch (val->type())
    {
    case value_type::int_:
        buf.append(std::to_string(val->get_int()));
        break;
    case value_type::float_:
        buf.append(std::to_string(val->get_float()));
        break;
    case value_type::string_: {
        lpc_string_t *str = reinterpret_cast<lpc_string_t *>(val->get_gcobj());
        buf.push_back('"');
        buf.append(str->get_str());
        buf.push_back('"');
        break;
    }
    case value_type::array_:{
        lpc_array_t *arr = reinterpret_cast<lpc_array_t *>(val->get_gcobj());
        if (!val->is_class()) {
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
    case value_type::mapping_: {
        lpc_mapping_t *m = reinterpret_cast<lpc_mapping_t *>(val->get_gcobj());
        build_mapping(buf, m, deep);
        break;
    }
    case value_type::object_:{
        lpc_object_t *obj = reinterpret_cast<lpc_object_t *>(val->get_gcobj());
        char tmp[50] = {0};
        auto ptr = reinterpret_cast<std::uintptr_t>(obj);
        sprintf(tmp, "object@0x%llx", ptr);
        buf.append(tmp);   
        break;
    }
    case value_type::function_: {
        lpc_function_t *f = reinterpret_cast<lpc_function_t *>(val->get_gcobj());
        char tmp[50] = {0};
        auto ptr = reinterpret_cast<std::uintptr_t>(f);
        sprintf(tmp, "function@0x%llx", ptr);
        buf.append(tmp);
        break;
    }
    case value_type::bool_:{
        if (val->get_bool()) {
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

    lpc_value_t *object_name = sk->pop();
    lpc_value_t *funcName = sk->pop();
    lpc_string_t *fNameStr = reinterpret_cast<lpc_string_t *>(funcName->get_gcobj());
    if (!funcName->is_string()) {
        EfunPanic(vm, "Expecting a function name is string type but not!!");
    }

    if (!object_name->is_string()) {
        EfunPanic(vm, "Not a string value to find object !!");
    }

    lpc_object_t *obj = vm->find_oject(object_name);
    if (!obj) {
        EfunPanic(vm, "internal error !");
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
        lpc_string_t *objName = reinterpret_cast<lpc_string_t *>(object_name->get_gcobj());
        EfunPanic(vm, string("no such function: ") + fNameStr->get_str() + " in object " + objName->get_str());
    }

    call_info_t *ci = vm->new_frame(obj, -funIdx);
    ci->call_other = true;

    ci->base = sk->top() - (nparam - 2);
    ci->base_index = sk->index_of(ci->base);

    vm->run();
}

static void f_sleep(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->pop();
    if (!val->is_int()) {
        EfunPanic(vm, "only integer can call sleep !!");
    }

    this_thread::sleep_for(chrono::milliseconds(val->get_int()));
}

static void f_sizeof(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->top();
    value_type t = val->type();
    lpc_gc_object_t *obj = val->get_gcobj();
    int sz = 0;
    
    if (t == value_type::string_){
        sz = reinterpret_cast<lpc_string_t *>(obj)->get_size();
    } else if (t == value_type::array_) {
        sz = reinterpret_cast<lpc_array_t *>(obj)->get_size();
    } else if (t == value_type::mapping_) {
        sz = reinterpret_cast<lpc_mapping_t *>(obj)->get_size();
    }
    
    val->set_int(sz);
}

static void f_random(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->top();
    if (!val->is_int()) {
        val->set_int(0);
        return;
    }

    unsigned char rs[4];
    rs[0] = rand() % 256;
    rs[1] = rand() % 256;
    rs[2] = rand() % 256;
    rs[3] = rand() % 256;

    int r = *(int*)rs;
    if (r < 0) r = -r;

    int n = val->get_int();
    val->set_int(r % n);
}

static void mapping_kvs(lpc_vm_t *vm, lint32_t nparam, bool key)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->pop();
    if (!val->is_mapping()) {
        return;
    }

    lpc_mapping_t *map = reinterpret_cast<lpc_mapping_t *>(val->get_gcobj());
    lpc_array_t *arr = nullptr;
    if (key) {
       arr = mapping_keys(map, vm->get_alloc());
    } else {
        arr = mapping_values(map, vm->get_alloc());
    }

    lpc_value_t tmp;
    tmp.set_array(reinterpret_cast<lpc_gc_object_t *>(arr));
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

static void f_typeof(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->top();
    int code = 0;
    switch (val->type()) {
    case value_type::null_:    code = 0; break;
    case value_type::int_:     code = 1; break;
    case value_type::float_:   code = 2; break;
    case value_type::string_:  code = 3; break;
    case value_type::array_:   code = 4; break;
    case value_type::mapping_: code = 5; break;
    case value_type::object_:  code = 6; break;
    case value_type::function_:
    case value_type::closure_: code = 7; break;
    case value_type::bool_:    code = 8; break;
    case value_type::buffer_:  code = 9; break;
    default:                   code = 0; break;
    }
    val->set_int(code);
}

static void f_to_string(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->pop();
    string buf;
    if (val->is_string()) {
        sk->push(val);
        return;
    }
    if (val->is_int()) {
        buf = std::to_string(val->get_int());
    } else if (val->is_float()) {
        buf = std::to_string(val->get_float());
    } else if (val->is_bool()) {
        buf = val->get_bool() ? "true" : "false";
    } else if (val->is_null()) {
        buf = "0";
    } else {
        build_basic(buf, val, 0);
    }
    lpc_string_t *str = vm->get_alloc()->allocate_string(buf.c_str());
    lpc_value_t result;
    result.set_string(reinterpret_cast<lpc_gc_object_t *>(str));
    sk->push(&result);
}

static void f_to_int(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->top();
    if (val->is_int()) {
        return;
    }
    if (val->is_float()) {
        val->set_int(static_cast<int>(val->get_float()));
        return;
    }
    if (val->is_string()) {
        lpc_string_t *str = reinterpret_cast<lpc_string_t *>(val->get_gcobj());
        val->set_int(atoi(str->get_str()));
        return;
    }
    if (val->is_bool()) {
        int b = val->get_bool() ? 1 : 0;
        val->set_int(b);
        return;
    }
    val->set_int(0);
}

static void f_this_object(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    call_info_t *ci = vm->get_call_info();
    lpc_object_t *obj = ci ? ci->cur_obj : nullptr;
    if (obj) {
        lpc_value_t result;
        result.set_object(reinterpret_cast<lpc_gc_object_t *>(obj));
        sk->push(&result);
    } else {
        lpc_value_t result;
        result.set_null();
        sk->push(&result);
    }
}

static void f_clone_object(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->pop();
    if (!val->is_string()) {
        EfunPanic(vm, "clone_object requires a string argument");
    }
    lpc_string_t *nameStr = reinterpret_cast<lpc_string_t *>(val->get_gcobj());
    lpc_object_t *obj = vm->load_object(nameStr->get_str(), true);
    if (!obj) {
        lpc_value_t result;
        result.set_null();
        sk->push(&result);
        return;
    }
    lpc_value_t result;
    result.set_object(reinterpret_cast<lpc_gc_object_t *>(obj));
    sk->push(&result);
}

static void f_destruct(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->pop();
    if (!val->is_object()) {
        EfunPanic(vm, "destruct requires an object argument");
    }
    lpc_object_t *obj = reinterpret_cast<lpc_object_t *>(val->get_gcobj());
    vm->on_destruct_object(obj);
}

static void f_sprintf(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *fmt_val = sk->top() - (nparam - 1);
    if (!fmt_val->is_string()) {
        EfunPanic(vm, "sprintf: first argument must be a format string");
    }
    lpc_string_t *fmtStr = reinterpret_cast<lpc_string_t *>(fmt_val->get_gcobj());
    const char *fmt = fmtStr->get_str();
    string buf;
    int arg_idx = 0;
    for (const char *p = fmt; *p; ++p) {
        if (*p != '%') {
            buf.push_back(*p);
            continue;
        }
        ++p;
        if (*p == '\0') break;
        if (*p == '%') { buf.push_back('%'); continue; }
        int slot = nparam - 2 - arg_idx;
        lpc_value_t *arg = (slot >= 0) ? sk->top() - slot : nullptr;
        ++arg_idx;
        switch (*p) {
        case 'd': case 'i':
            if (arg && arg->is_int()) {
                buf.append(std::to_string(arg->get_int()));
            } else {
                buf.append("0");
            }
            break;
        case 'f':
            if (arg && arg->is_float()) {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), "%g", arg->get_float());
                buf.append(tmp);
            } else if (arg && arg->is_int()) {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), "%g", static_cast<float>(arg->get_int()));
                buf.append(tmp);
            } else {
                buf.append("0");
            }
            break;
        case 's':
            if (arg && arg->is_string()) {
                lpc_string_t *s = reinterpret_cast<lpc_string_t *>(arg->get_gcobj());
                buf.append(s->get_str());
            } else if (arg) {
                string tmp;
                build_basic(tmp, arg, 0);
                buf.append(tmp);
            }
            break;
        default:
            buf.push_back('%');
            buf.push_back(*p);
            --arg_idx;
            break;
        }
    }
    for (int i = 0; i < nparam; ++i) {
        sk->pop();
    }
    lpc_string_t *resultStr = vm->get_alloc()->allocate_string(buf.c_str());
    lpc_value_t result;
    result.set_string(reinterpret_cast<lpc_gc_object_t *>(resultStr));
    sk->push(&result);
}

static void f_write(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->pop();
    if (val->is_string()) {
        lpc_string_t *str = reinterpret_cast<lpc_string_t *>(val->get_gcobj());
        debug_message("%s", str->get_str());
    } else {
        string buf;
        build_basic(buf, val, 0);
        debug_message("%s", buf.c_str());
    }
}

static void f_time(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t result;
    result.set_int(static_cast<int>(std::time(nullptr)));
    sk->push(&result);
}

static void f_member_array(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *haystack = sk->pop();
    lpc_value_t *needle = sk->pop();

    int idx = -1;
    if (haystack->is_array()) {
        lpc_array_t *arr = reinterpret_cast<lpc_array_t *>(haystack->get_gcobj());
        for (int i = 0; i < static_cast<int>(arr->get_size()); ++i) {
            lpc_value_t *elem = arr->get(i);
            if (needle->type() == elem->type()) {
                if (needle->is_int() && needle->get_int() == elem->get_int()) { idx = i; break; }
                if (needle->is_float() && needle->get_float() == elem->get_float()) { idx = i; break; }
                if (needle->is_string() && elem->is_string()) {
                    lpc_string_t *ns = reinterpret_cast<lpc_string_t *>(needle->get_gcobj());
                    lpc_string_t *es = reinterpret_cast<lpc_string_t *>(elem->get_gcobj());
                    if (strcmp(ns->get_str(), es->get_str()) == 0) { idx = i; break; }
                }
                if (needle->is_bool() && needle->get_bool() == elem->get_bool()) { idx = i; break; }
            }
        }
    } else if (haystack->is_string() && needle->is_string()) {
        lpc_string_t *hs = reinterpret_cast<lpc_string_t *>(haystack->get_gcobj());
        lpc_string_t *ns = reinterpret_cast<lpc_string_t *>(needle->get_gcobj());
        const char *pos = strstr(hs->get_str(), ns->get_str());
        if (pos) {
            idx = static_cast<int>(pos - hs->get_str());
        }
    }

    lpc_value_t result;
    result.set_int(idx);
    sk->push(&result);
}

static void f_explode(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *delim_val = sk->pop();
    lpc_value_t *str_val = sk->pop();

    if (!str_val->is_string() || !delim_val->is_string()) {
        lpc_value_t result;
        result.set_null();
        sk->push(&result);
        return;
    }

    lpc_string_t *str = reinterpret_cast<lpc_string_t *>(str_val->get_gcobj());
    lpc_string_t *delim = reinterpret_cast<lpc_string_t *>(delim_val->get_gcobj());
    const char *s = str->get_str();
    const char *d = delim->get_str();
    int dlen = static_cast<int>(strlen(d));

    std::vector<std::string> parts;
    if (dlen == 0) {
        for (const char *p = s; *p; ++p) {
            parts.push_back(std::string(1, *p));
        }
    } else {
        std::string work(s);
        size_t pos = 0;
        while (pos < work.size()) {
            size_t found = work.find(d, pos);
            if (found == std::string::npos) {
                parts.push_back(work.substr(pos));
                break;
            }
            parts.push_back(work.substr(pos, found - pos));
            pos = found + dlen;
        }
        if (pos == work.size()) {
            parts.push_back("");
        }
    }

    lpc_array_t *arr = vm->get_alloc()->allocate_array(static_cast<luint32_t>(parts.size()));
    for (int i = 0; i < static_cast<int>(parts.size()); ++i) {
        lpc_string_t *ps = vm->get_alloc()->allocate_string(parts[i].c_str());
        lpc_value_t val;
        val.set_string(reinterpret_cast<lpc_gc_object_t *>(ps));
        arr->set(&val, i);
    }

    lpc_value_t result;
    result.set_array(reinterpret_cast<lpc_gc_object_t *>(arr));
    sk->push(&result);
}

static void f_implode(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *delim_val = sk->pop();
    lpc_value_t *arr_val = sk->pop();

    if (!arr_val->is_array()) {
        lpc_value_t result;
        result.set_null();
        sk->push(&result);
        return;
    }

    lpc_array_t *arr = reinterpret_cast<lpc_array_t *>(arr_val->get_gcobj());
    std::string d = "";
    if (delim_val->is_string()) {
        lpc_string_t *ds = reinterpret_cast<lpc_string_t *>(delim_val->get_gcobj());
        d = ds->get_str();
    }

    std::string buf;
    for (int i = 0; i < static_cast<int>(arr->get_size()); ++i) {
        lpc_value_t *elem = arr->get(i);
        if (elem->is_string()) {
            lpc_string_t *es = reinterpret_cast<lpc_string_t *>(elem->get_gcobj());
            buf.append(es->get_str());
        } else if (elem->is_int()) {
            buf.append(std::to_string(elem->get_int()));
        } else if (elem->is_float()) {
            buf.append(std::to_string(elem->get_float()));
        }
        if (i < static_cast<int>(arr->get_size()) - 1) {
            buf.append(d);
        }
    }

    lpc_string_t *resultStr = vm->get_alloc()->allocate_string(buf.c_str());
    lpc_value_t result;
    result.set_string(reinterpret_cast<lpc_gc_object_t *>(resultStr));
    sk->push(&result);
}

static void f_stringp(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->top();
    lpc_value_t result;
    result.set_bool(val->is_string());
    *val = result;
}

static void f_intp(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->top();
    lpc_value_t result;
    result.set_bool(val->is_int());
    *val = result;
}

static void f_floatp(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->top();
    lpc_value_t result;
    result.set_bool(val->is_float());
    *val = result;
}

static void f_arrayp(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->top();
    lpc_value_t result;
    result.set_bool(val->is_array());
    *val = result;
}

static void f_mappingp(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->top();
    lpc_value_t result;
    result.set_bool(val->is_mapping());
    *val = result;
}

static void f_objectp(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->top();
    lpc_value_t result;
    result.set_bool(val->is_object());
    *val = result;
}

static void f_nullp(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->top();
    lpc_value_t result;
    result.set_bool(val->is_null());
    *val = result;
}

static void f_functionp(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->top();
    lpc_value_t result;
    result.set_bool(val->type() == value_type::function_ || val->type() == value_type::closure_);
    *val = result;
}

static void f_to_float(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->top();
    if (val->is_float()) {
        return;
    }
    if (val->is_int()) {
        val->set_float(static_cast<float>(val->get_int()));
        return;
    }
    if (val->is_string()) {
        lpc_string_t *str = reinterpret_cast<lpc_string_t *>(val->get_gcobj());
        val->set_float(static_cast<float>(atof(str->get_str())));
        return;
    }
    val->set_float(0.0f);
}

static void f_abs(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->top();
    if (val->is_int()) {
        int v = val->get_int();
        val->set_int(v < 0 ? -v : v);
    } else if (val->is_float()) {
        float v = val->get_float();
        val->set_float(v < 0.0f ? -v : v);
    }
}

static void f_strlen(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->top();
    int len = 0;
    if (val->is_string()) {
        lpc_string_t *str = reinterpret_cast<lpc_string_t *>(val->get_gcobj());
        len = str->get_size();
    }
    val->set_int(len);
}

static void f_map_delete(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *key = sk->pop();
    lpc_value_t *map_val = sk->pop();
    if (map_val->is_mapping()) {
        lpc_mapping_t *map = reinterpret_cast<lpc_mapping_t *>(map_val->get_gcobj());
        map_delete(map, key);
    }
}

static void f_capitalize(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->pop();
    if (!val->is_string()) {
        sk->push(val);
        return;
    }
    lpc_string_t *str = reinterpret_cast<lpc_string_t *>(val->get_gcobj());
    std::string buf(str->get_str());
    if (!buf.empty() && buf[0] >= 'a' && buf[0] <= 'z') {
        buf[0] = static_cast<char>(buf[0] - 32);
    }
    lpc_string_t *resultStr = vm->get_alloc()->allocate_string(buf.c_str());
    lpc_value_t result;
    result.set_string(reinterpret_cast<lpc_gc_object_t *>(resultStr));
    sk->push(&result);
}

static void f_lower_case(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->pop();
    if (!val->is_string()) {
        sk->push(val);
        return;
    }
    lpc_string_t *str = reinterpret_cast<lpc_string_t *>(val->get_gcobj());
    std::string buf(str->get_str());
    for (size_t i = 0; i < buf.size(); ++i) {
        if (buf[i] >= 'A' && buf[i] <= 'Z') {
            buf[i] = static_cast<char>(buf[i] + 32);
        }
    }
    lpc_string_t *resultStr = vm->get_alloc()->allocate_string(buf.c_str());
    lpc_value_t result;
    result.set_string(reinterpret_cast<lpc_gc_object_t *>(resultStr));
    sk->push(&result);
}

static void f_upper_case(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->pop();
    if (!val->is_string()) {
        sk->push(val);
        return;
    }
    lpc_string_t *str = reinterpret_cast<lpc_string_t *>(val->get_gcobj());
    std::string buf(str->get_str());
    for (size_t i = 0; i < buf.size(); ++i) {
        if (buf[i] >= 'a' && buf[i] <= 'z') {
            buf[i] = static_cast<char>(buf[i] - 32);
        }
    }
    lpc_string_t *resultStr = vm->get_alloc()->allocate_string(buf.c_str());
    lpc_value_t result;
    result.set_string(reinterpret_cast<lpc_gc_object_t *>(resultStr));
    sk->push(&result);
}

static void safe_f_call_other(lpc_vm_t *vm, lint32_t nparam) { try { f_call_other(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_print(lpc_vm_t *vm, lint32_t nparam) { try { f_print(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_puts(lpc_vm_t *vm, lint32_t nparam) { try { f_puts(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_sleep(lpc_vm_t *vm, lint32_t nparam) { try { f_sleep(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_sizeof(lpc_vm_t *vm, lint32_t nparam) { try { f_sizeof(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_random(lpc_vm_t *vm, lint32_t nparam) { try { f_random(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_keys(lpc_vm_t *vm, lint32_t nparam) { try { f_keys(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_values(lpc_vm_t *vm, lint32_t nparam) { try { f_values(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_typeof(lpc_vm_t *vm, lint32_t nparam) { try { f_typeof(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_to_string(lpc_vm_t *vm, lint32_t nparam) { try { f_to_string(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_to_int(lpc_vm_t *vm, lint32_t nparam) { try { f_to_int(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_this_object(lpc_vm_t *vm, lint32_t nparam) { try { f_this_object(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_clone_object(lpc_vm_t *vm, lint32_t nparam) { try { f_clone_object(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_destruct(lpc_vm_t *vm, lint32_t nparam) { try { f_destruct(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_sprintf(lpc_vm_t *vm, lint32_t nparam) { try { f_sprintf(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_write(lpc_vm_t *vm, lint32_t nparam) { try { f_write(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_time(lpc_vm_t *vm, lint32_t nparam) { try { f_time(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_member_array(lpc_vm_t *vm, lint32_t nparam) { try { f_member_array(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_explode(lpc_vm_t *vm, lint32_t nparam) { try { f_explode(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_implode(lpc_vm_t *vm, lint32_t nparam) { try { f_implode(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_stringp(lpc_vm_t *vm, lint32_t nparam) { try { f_stringp(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_intp(lpc_vm_t *vm, lint32_t nparam) { try { f_intp(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_floatp(lpc_vm_t *vm, lint32_t nparam) { try { f_floatp(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_arrayp(lpc_vm_t *vm, lint32_t nparam) { try { f_arrayp(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_mappingp(lpc_vm_t *vm, lint32_t nparam) { try { f_mappingp(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_objectp(lpc_vm_t *vm, lint32_t nparam) { try { f_objectp(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_nullp(lpc_vm_t *vm, lint32_t nparam) { try { f_nullp(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_functionp(lpc_vm_t *vm, lint32_t nparam) { try { f_functionp(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_to_float(lpc_vm_t *vm, lint32_t nparam) { try { f_to_float(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_abs(lpc_vm_t *vm, lint32_t nparam) { try { f_abs(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_strlen(lpc_vm_t *vm, lint32_t nparam) { try { f_strlen(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_map_delete(lpc_vm_t *vm, lint32_t nparam) { try { f_map_delete(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_capitalize(lpc_vm_t *vm, lint32_t nparam) { try { f_capitalize(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_lower_case(lpc_vm_t *vm, lint32_t nparam) { try { f_lower_case(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_upper_case(lpc_vm_t *vm, lint32_t nparam) { try { f_upper_case(vm, nparam); } catch (const vm_abort_signal &) { return; } }

static void f_allocate(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *size_val = sk->pop();
    int sz = size_val->is_int() ? size_val->get_int() : 0;
    if (sz < 0) sz = 0;

    lpc_value_t init_val;
    if (nparam >= 2) {
        lpc_value_t *iv = sk->pop();
        init_val = *iv;
    } else {
        init_val.set_int(0);
    }

    lpc_array_t *arr = vm->get_alloc()->allocate_array(static_cast<luint32_t>(sz));
    for (int i = 0; i < sz; ++i) {
        arr->set(&init_val, i);
    }
    lpc_value_t result;
    result.set_array(reinterpret_cast<lpc_gc_object_t *>(arr));
    sk->push(&result);
}

static void f_reverse(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->pop();
    if (!val->is_array()) {
        lpc_value_t result;
        result.set_null();
        sk->push(&result);
        return;
    }
    lpc_array_t *src = reinterpret_cast<lpc_array_t *>(val->get_gcobj());
    int sz = static_cast<int>(src->get_size());
    lpc_array_t *arr = vm->get_alloc()->allocate_array(static_cast<luint32_t>(sz));
    for (int i = 0; i < sz; ++i) {
        arr->set(src->get(sz - 1 - i), i);
    }
    lpc_value_t result;
    result.set_array(reinterpret_cast<lpc_gc_object_t *>(arr));
    sk->push(&result);
}

static void f_min(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *b = sk->pop();
    lpc_value_t *a = sk->pop();
    lpc_value_t result;
    if (a->is_int() && b->is_int()) {
        result.set_int(a->get_int() < b->get_int() ? a->get_int() : b->get_int());
    } else if (a->is_float() && b->is_float()) {
        result.set_float(a->get_float() < b->get_float() ? a->get_float() : b->get_float());
    } else if (a->is_int() && b->is_float()) {
        result.set_float(static_cast<float>(a->get_int()) < b->get_float() ? static_cast<float>(a->get_int()) : b->get_float());
    } else if (a->is_float() && b->is_int()) {
        result.set_float(a->get_float() < static_cast<float>(b->get_int()) ? a->get_float() : static_cast<float>(b->get_int()));
    } else {
        result.set_null();
    }
    sk->push(&result);
}

static void f_max(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *b = sk->pop();
    lpc_value_t *a = sk->pop();
    lpc_value_t result;
    if (a->is_int() && b->is_int()) {
        result.set_int(a->get_int() > b->get_int() ? a->get_int() : b->get_int());
    } else if (a->is_float() && b->is_float()) {
        result.set_float(a->get_float() > b->get_float() ? a->get_float() : b->get_float());
    } else if (a->is_int() && b->is_float()) {
        result.set_float(static_cast<float>(a->get_int()) > b->get_float() ? static_cast<float>(a->get_int()) : b->get_float());
    } else if (a->is_float() && b->is_int()) {
        result.set_float(a->get_float() > static_cast<float>(b->get_int()) ? a->get_float() : static_cast<float>(b->get_int()));
    } else {
        result.set_null();
    }
    sk->push(&result);
}

static void f_sqrt(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->pop();
    lpc_value_t result;
    if (val->is_int()) {
        result.set_float(sqrtf(static_cast<float>(val->get_int())));
    } else if (val->is_float()) {
        result.set_float(sqrtf(val->get_float()));
    } else {
        result.set_null();
    }
    sk->push(&result);
}

static void f_ctime(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *val = sk->pop();
    time_t t;
    if (val->is_int()) {
        t = static_cast<time_t>(val->get_int());
    } else {
        t = time(nullptr);
    }
    char *ts = ctime(&t);
    std::string s(ts);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
        s.pop_back();
    }
    lpc_string_t *str = vm->get_alloc()->allocate_string(s.c_str());
    lpc_value_t result;
    result.set_string(reinterpret_cast<lpc_gc_object_t *>(str));
    sk->push(&result);
}

static void f_strsrch(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    int start = 0;
    if (nparam >= 3) {
        lpc_value_t *sv = sk->pop();
        if (sv->is_int()) start = sv->get_int();
    }
    lpc_value_t *needle_val = sk->pop();
    lpc_value_t *haystack_val = sk->pop();

    if (!haystack_val->is_string() || !needle_val->is_string()) {
        lpc_value_t result;
        result.set_int(-1);
        sk->push(&result);
        return;
    }

    lpc_string_t *hs = reinterpret_cast<lpc_string_t *>(haystack_val->get_gcobj());
    lpc_string_t *nd = reinterpret_cast<lpc_string_t *>(needle_val->get_gcobj());
    std::string haystack(hs->get_str());
    std::string needle(nd->get_str());

    if (start < 0) start = 0;
    if (start > static_cast<int>(haystack.size())) start = static_cast<int>(haystack.size());

    size_t pos = haystack.find(needle, static_cast<size_t>(start));
    lpc_value_t result;
    result.set_int(pos == std::string::npos ? -1 : static_cast<int>(pos));
    sk->push(&result);
}

static void f_replace_string(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *replace_val = sk->pop();
    lpc_value_t *search_val = sk->pop();
    lpc_value_t *src_val = sk->pop();

    if (!src_val->is_string() || !search_val->is_string() || !replace_val->is_string()) {
        lpc_value_t result;
        result.set_null();
        sk->push(&result);
        return;
    }

    lpc_string_t *ss = reinterpret_cast<lpc_string_t *>(src_val->get_gcobj());
    lpc_string_t *se = reinterpret_cast<lpc_string_t *>(search_val->get_gcobj());
    lpc_string_t *re = reinterpret_cast<lpc_string_t *>(replace_val->get_gcobj());
    std::string src(ss->get_str());
    std::string search(se->get_str());
    std::string replace(re->get_str());

    if (!search.empty()) {
        size_t pos = 0;
        while ((pos = src.find(search, pos)) != std::string::npos) {
            src.replace(pos, search.size(), replace);
            pos += replace.size();
        }
    }

    lpc_string_t *str = vm->get_alloc()->allocate_string(src.c_str());
    lpc_value_t result;
    result.set_string(reinterpret_cast<lpc_gc_object_t *>(str));
    sk->push(&result);
}

static void f_sort_array(lpc_vm_t *vm, lint32_t nparam)
{
    lpc_stack_t *sk = vm->get_stack();
    lpc_value_t *arr_val = sk->pop();

    if (!arr_val->is_array()) {
        lpc_value_t result;
        result.set_null();
        sk->push(&result);
        return;
    }

    lpc_array_t *src = reinterpret_cast<lpc_array_t *>(arr_val->get_gcobj());
    int sz = static_cast<int>(src->get_size());

    std::vector<lpc_value_t> items(sz);
    for (int i = 0; i < sz; ++i) {
        items[i] = *src->get(i);
    }

    std::sort(items.begin(), items.end(), [](const lpc_value_t &a, const lpc_value_t &b) {
        if (a.is_int() && b.is_int()) return a.get_int() < b.get_int();
        if (a.is_float() && b.is_float()) return a.get_float() < b.get_float();
        if (a.is_int() && b.is_float()) return static_cast<float>(a.get_int()) < b.get_float();
        if (a.is_float() && b.is_int()) return a.get_float() < static_cast<float>(b.get_int());
        if (a.is_string() && b.is_string()) {
            lpc_string_t *sa = reinterpret_cast<lpc_string_t *>(a.get_gcobj());
            lpc_string_t *sb = reinterpret_cast<lpc_string_t *>(b.get_gcobj());
            return strcmp(sa->get_str(), sb->get_str()) < 0;
        }
        return false;
    });

    lpc_array_t *arr = vm->get_alloc()->allocate_array(static_cast<luint32_t>(sz));
    for (int i = 0; i < sz; ++i) {
        arr->set(&items[i], i);
    }
    lpc_value_t result;
    result.set_array(reinterpret_cast<lpc_gc_object_t *>(arr));
    sk->push(&result);
}

static void safe_f_allocate(lpc_vm_t *vm, lint32_t nparam) { try { f_allocate(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_reverse(lpc_vm_t *vm, lint32_t nparam) { try { f_reverse(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_min(lpc_vm_t *vm, lint32_t nparam) { try { f_min(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_max(lpc_vm_t *vm, lint32_t nparam) { try { f_max(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_sqrt(lpc_vm_t *vm, lint32_t nparam) { try { f_sqrt(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_ctime(lpc_vm_t *vm, lint32_t nparam) { try { f_ctime(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_strsrch(lpc_vm_t *vm, lint32_t nparam) { try { f_strsrch(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_replace_string(lpc_vm_t *vm, lint32_t nparam) { try { f_replace_string(vm, nparam); } catch (const vm_abort_signal &) { return; } }
static void safe_f_sort_array(lpc_vm_t *vm, lint32_t nparam) { try { f_sort_array(vm, nparam); } catch (const vm_abort_signal &) { return; } }

void init_efuns(lpc_vm_t *vm)
{
    efun_t *efuns = new efun_t[44];

    efuns[0] = safe_f_call_other;
    efuns[1] = safe_f_print;
    efuns[2] = safe_f_puts;
    efuns[3] = safe_f_sleep;
    efuns[4] = safe_f_sizeof;
    efuns[5] = safe_f_random;
    efuns[6] = safe_f_keys;
    efuns[7] = safe_f_values;
    efuns[8] = safe_f_typeof;
    efuns[9] = safe_f_to_string;
    efuns[10] = safe_f_to_int;
    efuns[11] = safe_f_this_object;
    efuns[12] = safe_f_clone_object;
    efuns[13] = safe_f_destruct;
    efuns[14] = safe_f_sprintf;
    efuns[15] = safe_f_write;
    efuns[16] = safe_f_time;
    efuns[17] = safe_f_member_array;
    efuns[18] = safe_f_explode;
    efuns[19] = safe_f_implode;
    efuns[20] = safe_f_stringp;
    efuns[21] = safe_f_intp;
    efuns[22] = safe_f_floatp;
    efuns[23] = safe_f_arrayp;
    efuns[24] = safe_f_mappingp;
    efuns[25] = safe_f_objectp;
    efuns[26] = safe_f_nullp;
    efuns[27] = safe_f_functionp;
    efuns[28] = safe_f_to_float;
    efuns[29] = safe_f_abs;
    efuns[30] = safe_f_strlen;
    efuns[31] = safe_f_map_delete;
    efuns[32] = safe_f_capitalize;
    efuns[33] = safe_f_lower_case;
    efuns[34] = safe_f_upper_case;
    efuns[35] = safe_f_allocate;
    efuns[36] = safe_f_reverse;
    efuns[37] = safe_f_min;
    efuns[38] = safe_f_max;
    efuns[39] = safe_f_sqrt;
    efuns[40] = safe_f_ctime;
    efuns[41] = safe_f_strsrch;
    efuns[42] = safe_f_replace_string;
    efuns[43] = safe_f_sort_array;

    vm->register_efun(efuns);
}
