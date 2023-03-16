﻿#include <string>
#include <fstream>

#include "runtime/vm.h"
#include "runtime/stack.h"
#include "type/lpc_proto.h"
#include "type/lpc_object.h"
#include "memory/memory.h"
#include "gc/gc.h"

using namespace std;
extern string get_cwd();

object_proto_t * lpc_vm_t::load_object_proto(const char *name)
{
    if (!name) return nullptr;
    const string &cwd = get_cwd();
    string realName = cwd + "/bin/" + name + ".b";

    // TODO 检查文件夹啥的

    ifstream in;
    in.open(realName.c_str(), ios_base::binary);
    if (!in.good()) {
        return nullptr;
    }

    object_proto_t *proto = alloc->allocate_object_proto();

    luint32_t sz;
    in.read((char *)&sz, 4);
    function_proto_t *func_proto = new function_proto_t[sz];
    for (int i = 0; i < sz; ++i) {
        luint32_t sz1;
        in.read((char *)&sz1, 4);
        // TODO 
        char *fname = new char[sz1 + 1];
        in.read(fname, sz1);
        fname[sz1 + 1] = '\0';
        func_proto[i].name = fname;
        in.read((char *)&func_proto[i].is_static, 1);

        in.read((char *)&func_proto[i].nlocal, 2);
        in.read((char *)&func_proto[i].nargs, 2);
        in.read((char *)&func_proto[i].fromPC, 4);
        in.read((char *)&func_proto[i].toPC, 4);

        func_proto[i].offset = i;
    }

    proto->func_table = func_proto;

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
    if (sz > 0) {
        constant_proto_t *sconsts = new constant_proto_t[sz];
        luint32_t len;
        for (int i = 0; i < sz; ++i) {
            in.read((char *)&len, 4);
            char *buf = new char[len + 1];
            buf[len + 1] = '\0';
            in.read(buf, len);
            sconsts[i].item.str = buf;
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
            in.read((char *)&sproto[i].nfield, 4);
        }

        proto->class_table = sproto;
    }

    in.read((char *)&sz, 4);
    proto->var_init_codes = nullptr;
    proto->init_code_size = 0;
    if (sz > 0) {
        char *var_inits = new char[sz];
        in.read(var_inits, sz);

        proto->var_init_codes = var_inits;
        proto->init_code_size = sz;
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
    this->stack = new lpc_stack_t(20000);
    this->alloc = new lpc_allocator_t(this);
    this->gc = new lpc_gc_t;
    entry = "1";
    call_info_t *ci = new call_info_t;
    ci->savepc = "\x05\x02\x07";
    ci->base = stack->top();
    ci->cur_obj = load_object(entry);

    this->ci = ci;
    this->cur_ci = ci;
    sfun_object_name = "rc/simulate/main.c";
    this->sfun_obj = load_object(sfun_object_name);
}

lpc_vm_t * lpc_vm_t::create_vm()
{
    lpc_vm_t *vm = new lpc_vm_t();

    return vm;
}

void lpc_vm_t::bootstrap()
{

}

void lpc_vm_t::set_entry()
{

}

void lpc_vm_t::on_start()
{

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

call_info_t * lpc_vm_t::new_frame()
{
    // TODO


    return cur_ci;
}

void lpc_vm_t::pop_frame()
{

}

lpc_gc_t * lpc_vm_t::get_gc()
{
    return this->gc;
}
