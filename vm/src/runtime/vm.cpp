﻿#include <string>
#include <fstream>

#include "lpc_value.h"
#include "runtime/vm.h"
#include "runtime/stack.h"
#include "type/lpc_proto.h"
#include "type/lpc_object.h"
#include "memory/memory.h"
#include "gc/gc.h"
#include "runtime/interpreter.h"

using namespace std;
extern string get_cwd();
extern void init_efuns(lpc_vm_t *);


object_proto_t * lpc_vm_t::load_object_proto(const char *name)
{
    if (!name) return nullptr;
    const string &cwd = get_cwd();
    string realName = "D:/code/src/vs/lpc-interpreter/build/compiler/Debug/1.b";

    // TODO 检查文件夹啥的

    ifstream in;
    in.open(realName.c_str(), ios_base::binary);
    if (!in.good()) {
        return nullptr;
    }

    object_proto_t *proto = alloc->allocate_object_proto();

    in.read((char *)&proto->create_idx, 2);
    in.read((char *)&proto->on_load_in_idx, 2);
    in.read((char *)&proto->on_destruct_idx, 2);

    luint32_t sz = 0;
    in.read((char *)&sz, 4);
    proto->nswitch = sz;
    lint32_t caser = 0, gotoW = 0;
    for (int i = 0; i < sz; ++i) {
        lint32_t sz1;
        in.read((char *)&sz1, 4);
        proto->lookup_table.push_back({});
        for (int j = 0; j < sz1; ++j) {
            in.read((char *)&caser, 4);
            in.read((char *)&gotoW, 4);
            if (gotoW < 0) {
                proto->defaults[i] = -gotoW;
            } else {
                proto->lookup_table.back()[caser] = gotoW;
            }
        }
    }

    in.read((char *)&sz, 4);
    function_proto_t *func_proto = new function_proto_t[sz];
    for (int i = 0; i < sz; ++i) {
        luint32_t sz1;
        in.read((char *)&sz1, 4);
        // TODO 
        char *fname = new char[sz1 + 1];
        in.read(fname, sz1);
        fname[sz1] = '\0';
        func_proto[i].name = fname;
        in.read((char *)&func_proto[i].is_static, 1);

        in.read((char *)&func_proto[i].nargs, 2);
        in.read((char *)&func_proto[i].nlocal, 2);
        in.read((char *)&func_proto[i].fromPC, 4);
        in.read((char *)&func_proto[i].toPC, 4);

        func_proto[i].offset = i;
    }

    proto->func_table = func_proto;
    proto->nfunction = sz;

    in.read((char *)&sz, 4);
    bool *loc_tag = new bool[sz];
    bool flag = false;
    for (int i = 0; i < sz; ++i) {
        in.read((char *)&flag, 1);
        loc_tag[i] = flag;
    }
    proto->loc_tags = loc_tag;
    proto->nvariable = sz;

    in.read((char *)&sz, 4);
    proto->iconst = 0;
    proto->niconst = sz;
    if (sz > 0) {
        int ival;
        constant_proto_t *iconsts = new constant_proto_t[sz];
        for (int i = 0; i < sz; ++i) {
            in.read((char *)&ival, 4);
            iconsts[i].item.number = ival;
        }
        proto->iconst = iconsts;
    }

    in.read((char *)&sz, 4);
    proto->fconst = nullptr;
    proto->nfconst = sz;
    if (sz > 0) {
        float fval;
        constant_proto_t *fconsts = new constant_proto_t[sz];
        for (int i = 0; i < sz; ++i) {
            in.read((char *)&fval, 4);
            fconsts[i].item.real = fval;
        }
        proto->fconst = fconsts;
    }

    in.read((char *)&sz, 4);
    proto->sconst = nullptr;
    proto->nsconst = sz;
    if (sz > 0) {
        constant_proto_t *sconsts = new constant_proto_t[sz];
        luint32_t len;
        for (int i = 0; i < sz; ++i) {
            in.read((char *)&len, 4);
            char *buf = new char[len + 1];
            in.read(buf, len);
            buf[len] = '\0';
            sconsts[i].item.str = alloc->allocate_string(buf);
        }
        proto->sconst = sconsts;
    }

    bool hasClazz = false;
    in.read((char *)&hasClazz, 1);
    proto->class_table = nullptr;
    if (!hasClazz) {
        in.read((char *)&sz, 4);
        class_proto_t *sproto = new class_proto_t[sz];
        for (int i = 0; i < sz; ++i) {
            in.read((char *)&sproto[i].is_static, 1);
            in.read((char *)&sproto[i].nfield, 2);
        }

        proto->class_table = sproto;
    }

    in.read((char *)&sz, 4);
    proto->init_codes = nullptr;
    proto->ninit = 0;
    if (sz > 0) {
        function_proto_t *init_fun = new function_proto_t;
        char *var_inits = new char[sz + 1];
        in.read(var_inits, sz);
        var_inits[sz + 1] = (char)OpCode::op_return;
        proto->init_codes = var_inits;
        proto->ninit = sz;
        init_fun->fromPC = 0;
        init_fun->toPC = sz + 1;
        proto->init_fun = init_fun;
    }

    in.read((char *)&sz, 4);
    proto->instructions = nullptr;
    proto->instruction_size = 0;
    if (sz > 0) {
        char *opcodes = new char[sz];
        in.read(opcodes, sz);

        proto->instructions = opcodes;
        proto->instruction_size = sz;
    }

    in.close();

    return proto;
}

lpc_object_t * lpc_vm_t::load_object(const char *name)
{
    lpc_object_t *obj = alloc->allocate_object();
    obj->set_proto(load_object_proto(name));
    return obj;
}

lpc_vm_t::lpc_vm_t()
{
    init_efuns(this);
    this->stack = new lpc_stack_t(20000);
    this->alloc = new lpc_allocator_t(this);
    this->gc = new lpc_gc_t;
}

lpc_vm_t * lpc_vm_t::create_vm()
{
    lpc_vm_t *vm = new lpc_vm_t();
    vm->bootstrap();
    return vm;
}

void lpc_vm_t::bootstrap()
{
    // TODO
    //sfun_object_name = "rc/simulate/main.c";
    //this->sfun_obj = load_object(sfun_object_name);

    loaded_protos = alloc->allocate_mapping();
    cur_ci = nullptr;

    entry = "1";
    lpc_object_t *obj = load_object(entry);
    if (obj->get_proto()->init_codes) {
        this->eval_init_codes(obj);
    }

    this->on_create_object(obj);
    lpc_string_t *k = alloc->allocate_string(entry);

    // TODO
    lpc_value_t key;
    key.type = value_type::string_;
    key.gcobj = reinterpret_cast<lpc_gc_object_t *>(k);

    lpc_value_t val;
    val.type = value_type::object_;
    val.gcobj = reinterpret_cast<lpc_gc_object_t *>(obj);
    loaded_protos->set(key, val);
    this->on_load_in_object(obj);
}

void lpc_vm_t::set_entry(const char *entry)
{
    this->entry = entry;
}

void lpc_vm_t::on_start()
{
    // init something
}

void lpc_vm_t::on_exit()
{

}

lpc_stack_t * lpc_vm_t::get_stack()
{
    return this->stack;
}

call_info_t * lpc_vm_t::get_call_info()
{
    return this->cur_ci;
}

 void lpc_vm_t::new_frame(lpc_object_t *obj, lint16_t idx)
{
    call_info_t *nci = new call_info_t;
    object_proto_t *proto = obj->get_proto();
    const function_proto_t &f = proto->func_table[idx];
    nci->savepc = proto->instructions + f.fromPC;
    nci->funcIdx = idx;
    nci->cur_obj = obj;
    nci->pre = cur_ci;
    nci->base = stack->top() - f.nargs;
    if (cur_ci) {
        cur_ci->next = nci;
        cur_ci = nci;
    } else {
        cur_ci = nci;
    }

    stack->set_local_size(f.nlocal);
}

void lpc_vm_t::pop_frame()
{
    call_info_t *pre = cur_ci;
    cur_ci = pre->pre;
    if (!cur_ci) {
        cur_ci = pre;
        return;
    }

    cur_ci->next = nullptr;
    const function_proto_t &f = pre->cur_obj->get_proto()->func_table[pre->funcIdx];
    stack->pop_n(f.nlocal);
    delete pre;
}

lpc_gc_t * lpc_vm_t::get_gc()
{
    return this->gc;
}

void lpc_vm_t::eval_init_codes(lpc_object_t *obj)
{
    object_proto_t *proto = obj->get_proto();
    if (!proto->init_codes) {
        return;
    }

    new_frame(obj, -1);
    vm::eval(this);
}

void lpc_vm_t::on_create_object(lpc_object_t *obj)
{
    object_proto_t *proto = obj->get_proto();
    if (proto->create_idx < 0) {
        // TODO warning
        return;
    }

    new_frame(obj, proto->create_idx);
    vm::eval(this);
}

void lpc_vm_t::on_load_in_object(lpc_object_t *obj)
{
    object_proto_t *proto = obj->get_proto();
    if (proto->on_load_in_idx < 0) {
        // TODO warning
        return;
    }

    new_frame(obj, proto->on_load_in_idx);
    vm::eval(this);
}

void lpc_vm_t::on_destruct_object(lpc_object_t *obj)
{
    object_proto_t *proto = obj->get_proto();
    if (proto->on_destruct_idx < 0) {
        // TODO warning
        return;
    }

    new_frame(obj, proto->on_destruct_idx);
    vm::eval(this);
}
