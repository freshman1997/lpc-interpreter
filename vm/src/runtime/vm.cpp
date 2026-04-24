#include <stdlib.h>
#include <string>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <cstring>
#include <cstdlib>

#include "lpc_value.h"
#include "runtime/vm.h"
#include "runtime/stack.h"
#include "runtime/verifier.h"
#include "type/lpc_proto.h"
#include "type/lpc_object.h"
#include "memory/memory.h"
#include "gc/gc.h"
#include "type/lpc_string.h"
#include "debug.h"

using namespace std;
extern string get_cwd();
extern void init_efuns(lpc_vm_t *);


object_proto_t * lpc_vm_t::load_object_proto(const char *name)
{
    if (!name) return nullptr;
    const string &cwd = get_cwd();
    string realName = cwd + "/bin/" + string(name) + ".b";
    string fallbackName = cwd + "/build/compiler/" + string(name) + ".b";

    ifstream in;
    in.open(realName.c_str(), ios_base::binary);
    if (!in.good()) {
        in.clear();
        in.open(fallbackName.c_str(), ios_base::binary);
        if (!in.good()) {
            in.clear();
            string fallback2 = cwd + "/../build/compiler/" + string(name) + ".b";
            in.open(fallback2.c_str(), ios_base::binary);
            if (!in.good()) {
            if (non_fatal_mode_) {
                set_last_error(std::string("can not open object bytecode: ") + name);
            }
            return nullptr;
            }
        }
    }

    object_proto_t *proto = alloc->allocate_object_proto();
    luint32_t sz = 0;
    in.read((char *)&sz, 4);
    char *objName = new char[sz + 1];
    in.read(objName, sz);
    objName[sz] = '\0';
    proto->name = objName;

    in.read((char *)&sz, 4);
    proto->inherits = nullptr;
    proto->inherit_offsets = nullptr;
    proto->ninherit = 0;
    proto->class_table = nullptr;
    proto->nclass = sz;
    if (sz > 0) {
        class_proto_t *klass = new class_proto_t[sz];
        for (int i = 0; i < static_cast<int>(sz); ++i) {
            luint32_t nlen = 0;
            in.read((char *)&nlen, 4);
            if (nlen > 0) {
                std::string tmp(nlen, '\0');
                in.read(&tmp[0], nlen);
            }
            in.read((char *)&klass[i].is_static, 1);
            in.read((char *)&klass[i].nfield, 2);
            klass[i].field_table = nullptr;
        }
        proto->class_table = klass;
    }

    in.read((char *)&proto->create_idx, 2);
    in.read((char *)&proto->on_load_in_idx, 2);
    in.read((char *)&proto->on_destruct_idx, 2);
    
    in.read((char *)&sz, 4);
    proto->nswitch = 0;
    proto->lookup_table = nullptr;
    proto->defaults = nullptr;
    proto->initLineMap = nullptr;
    proto->lineMap = nullptr;
    if (sz > 0) {
        proto->lineMap = new std::vector<std::pair<luint32_t, luint32_t>>();
        lint32_t lineno = 0, opoff = 0;
        for (lint32_t i = 0; i < sz; ++i) {
            in.read((char *)&lineno, 4);
            in.read((char *)&opoff, 4);
            proto->lineMap->push_back({lineno + 1, opoff});
        }
    }

    in.read((char *)&sz, 4);
    function_proto_t *func_proto = new function_proto_t[sz];
    for (int i = 0; i < sz; ++i) {
        luint32_t sz1;
        in.read((char *)&sz1, 4);
        char *fname = nullptr;
        if (sz1 > 0) {
            fname = new char[sz1 + 1];
            in.read(fname, sz1);
            fname[sz1] = '\0';
        }

        func_proto[i].name = fname;
        in.read((char *)&func_proto[i].retType, 1);
        in.read((char *)&func_proto[i].is_static, 1);
        in.read((char *)&func_proto[i].nargs, 2);
        in.read((char *)&func_proto[i].nlocal, 2);
        in.read((char *)&func_proto[i].nupvalue, 2);
        func_proto[i].upvalue_source_kind = nullptr;
        func_proto[i].upvalue_source_index = nullptr;
        if (func_proto[i].nupvalue > 0) {
            func_proto[i].upvalue_source_kind = new lint16_t[func_proto[i].nupvalue];
            func_proto[i].upvalue_source_index = new lint16_t[func_proto[i].nupvalue];
            for (int ui = 0; ui < func_proto[i].nupvalue; ++ui) {
                in.read((char *)&func_proto[i].upvalue_source_kind[ui], 2);
                in.read((char *)&func_proto[i].upvalue_source_index[ui], 2);
            }
        }
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
            sconsts[i].item.str->header.marked = 1;
        }
        proto->sconst = sconsts;
    }

    bool hasClazz = false;
    in.read((char *)&hasClazz, 1);
    if (!hasClazz) {
        in.read((char *)&sz, 4);
        if (proto->class_table) {
            delete [] proto->class_table;
            proto->class_table = nullptr;
        }
        proto->nclass = sz;
        class_proto_t *sproto = new class_proto_t[sz];
        for (int i = 0; i < static_cast<int>(sz); ++i) {
            in.read((char *)&sproto[i].is_static, 1);
            in.read((char *)&sproto[i].nfield, 2);
            sproto[i].field_table = nullptr;
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
        var_inits[sz] = (char)OpCode::op_return;
        proto->init_codes = var_inits;
        proto->ninit = sz;
        init_fun->fromPC = 0;
        init_fun->toPC = sz + 1;
        proto->init_fun = init_fun;
    }

    in.read((char *)&sz, 4);
    if (sz > 0) {
        if (!proto->initLineMap) {
            proto->initLineMap = new std::vector<std::pair<luint32_t, luint32_t>>();
        }
        lint32_t lineno = 0, opoff = 0;
        for (lint32_t i = 0; i < sz; ++i) {
            in.read((char *)&lineno, 4);
            in.read((char *)&opoff, 4);
            proto->initLineMap->push_back({lineno + 1, opoff});
        }
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

    in.read((char *)&sz, 4);
    if (sz > 0) {
        if (!proto->lineMap) {
            proto->lineMap = new std::vector<std::pair<luint32_t, luint32_t>>();
        }
        lint32_t lineno = 0, opoff = 0;
        for (lint32_t i = 0; i < sz; ++i) {
            in.read((char *)&lineno, 4);
            in.read((char *)&opoff, 4);
            proto->lineMap->push_back({lineno + 1, opoff});
        }
    }

    in.close();

    vm::VerifyResult vr = vm::VerifyV1Bytecode(*proto);
    if (!vr.ok) {
        std::cout << "bytecode verify failed for object: " << proto->name
                  << ", offset=" << vr.offset
                  << ", reason=" << vr.message << std::endl;
        if (non_fatal_mode_) {
            set_last_error(std::string("bytecode verify failed: ") + vr.message);
            return nullptr;
        }
        exit(-1);
    }

    return proto;
}

lpc_object_t * lpc_vm_t::load_object(const char *name, bool newOne)
{
    lpc_object_t *obj = alloc->allocate_object();
    object_proto_t *proto = load_object_proto(name);
    if (!proto) {
        cout << "can not load object: " << name << endl;
        if (non_fatal_mode_) {
            if (!has_error()) {
                set_last_error(std::string("can not load object: ") + name);
            }
            return nullptr;
        }
        exit(-1);
    }

    if (non_fatal_mode_) {
        try {
            obj->set_proto(proto);
            on_loaded_object(obj, name, newOne);
        } catch (const std::bad_alloc &) {
            set_last_error(std::string("load object bad_alloc: ") + name);
            return nullptr;
        } catch (const vm_abort_signal &) {
            return nullptr;
        } catch (...) {
            set_last_error(std::string("load object unknown exception: ") + name);
            return nullptr;
        }
        return obj;
    }

    obj->set_proto(proto);
    on_loaded_object(obj, name, newOne);

    return obj;
}

void lpc_vm_t::on_loaded_object(lpc_object_t *obj, const char *name, bool newOne)
{
    if (obj->get_proto()->init_codes) {
        this->eval_init_codes(obj);
    }
    
    lpc_string_t *k = alloc->allocate_string(name, newOne);
    lpc_value_t key;
    key.set_string(reinterpret_cast<lpc_gc_object_t *>(k));

    lpc_value_t val;
    val.set_object(reinterpret_cast<lpc_gc_object_t *>(obj));
    loaded_protos->set(&key, &val);

    this->on_create_object(obj);
    this->on_load_in_object(obj);
}

lpc_object_t * lpc_vm_t::find_oject(lpc_value_t *name)
{
    if (!name->is_string()) {
        return nullptr;
    }

    lpc_value_t *val = loaded_protos->get_value(name);
    if (val) {
        return reinterpret_cast<lpc_object_t *>(val->get_gcobj());
    } else {
        lpc_string_t *str = reinterpret_cast<lpc_string_t *>(name->get_gcobj());
        return load_object(str->get_str());
    }
}

lpc_vm_t::lpc_vm_t()
{
    init_efuns(this);
    this->stack = new lpc_stack_t(20000, this);
    this->alloc = new lpc_allocator_t(this);
    this->gc = new lpc_gc_t(this);

    loaded_protos = alloc->allocate_mapping();
    loaded_protos->header.marked = 1;
    base_ci = nullptr;
    cur_ci = nullptr;
    entry_storage = "1";
    entry = entry_storage.c_str();
    sfun_object_name = "rc/simulate_efun";
    dbg = nullptr;
}

lpc_vm_t::~lpc_vm_t()
{
    if (profiler) {
        delete profiler;
        profiler = nullptr;
    }
    if (dbg) {
        delete dbg;
        dbg = nullptr;
    }
    if (stack) {
        delete stack;
        stack = nullptr;
    }
    if (efuns) {
        delete [] efuns;
        efuns = nullptr;
    }
    if (gc) {
        delete gc;
        gc = nullptr;
    }
    if (alloc) {
        delete alloc;
        alloc = nullptr;
    }
}

lpc_vm_t * lpc_vm_t::create_vm()
{
    lpc_vm_t *vm = new lpc_vm_t();
    return vm;
}

void lpc_vm_t::bootstrap()
{
    clear_last_error();
    set_memory_limit_bytes(2ULL * 1024ULL * 1024ULL * 1024ULL);
    const char *mem_limit = std::getenv("LPC_VM_MAX_MEM");
    if (mem_limit && *mem_limit) {
        char *end = nullptr;
        unsigned long long bytes = std::strtoull(mem_limit, &end, 10);
        if (end != mem_limit && bytes > 0) {
            set_memory_limit_bytes(static_cast<luint64_t>(bytes));
        }
    }

    const char *nursery_limit = std::getenv("LPC_VM_NURSERY_MEM");
    if (nursery_limit && *nursery_limit) {
        char *end = nullptr;
        unsigned long long bytes = std::strtoull(nursery_limit, &end, 10);
        if (end != nursery_limit && bytes > 0) {
            set_nursery_limit_bytes(static_cast<luint64_t>(bytes));
        }
    }

    lpc_object_t *eobj = load_object(sfun_object_name, true);
    if (!eobj) {
        if (!has_error()) {
            set_last_error("failed to load simulate_efun object");
        }
        return;
    }
    this->sfun_obj = eobj;

    const char *env_entry = std::getenv("LPC_ENTRY");
    if (env_entry && *env_entry) {
        set_entry_owned(env_entry);
    }

    if (strcmp(entry, "1") == 0) {
        namespace fs = std::filesystem;
        std::string p = get_cwd() + "/bin/entry.txt";
        if (fs::exists(fs::path(p))) {
            std::ifstream in(p.c_str(), std::ios::binary);
            std::string m;
            if (in.is_open()) {
                std::getline(in, m);
                in.close();
                if (!m.empty()) {
                    set_entry_owned(m);
                }
            }
        }
    }
    
    lpc_object_t *obj = load_object(entry);
    if (!obj) {
        if (!has_error()) {
            set_last_error(std::string("failed to load entry object: ") + entry);
        }
        return;
    }
    this->entry_obj = obj;

    if (gc_nursery_limit_bytes() <= 1024ULL * 1024ULL) {
        set_nursery_limit_bytes(8ULL * 1024ULL * 1024ULL);
    }
}

void lpc_vm_t::set_entry(const char *entry)
{
    if (entry) {
        set_entry_owned(entry);
    }
}

void lpc_vm_t::on_start()
{
}

void lpc_vm_t::on_exit()
{

}

void lpc_vm_t::load_config()
{
    
}

void lpc_vm_t::run_main()
{
    clear_last_error();
    has_last_int_result_ = false;
    last_int_result_ = 0;
    if (!entry_obj) {
        set_last_error("entry object is null");
        return;
    }

    string main = "main";
    object_proto_t *proto = entry_obj->get_proto();
    if (proto->func_table) {
        lint16_t funIdx = -1;
        for (int i = 0; i < proto->nfunction; ++i) {
            auto *fun = &proto->func_table[i];
            if (0 == strcmp(fun->name, main.c_str()) && !fun->is_static) {
                funIdx = i;
                break;
            }
        }

        if (funIdx >= 0) {
            function_proto_t *f = &proto->func_table[funIdx];
            lpc_value_t null_val;
            null_val.set_null();
            for (int i = 0; i < f->nargs; ++i) {
                stack->push(&null_val);
            }
            new_frame(entry_obj, funIdx);
            run();
            lpc_value_t *topv = stack->top();
            if (topv && topv->is_int()) {
                has_last_int_result_ = true;
                last_int_result_ = topv->get_int();
            }
        } else {
            set_last_error("main function not found");
        }
    } else {
        set_last_error("entry object has no function table");
    }
}

lpc_stack_t * lpc_vm_t::get_stack()
{
    return this->stack;
}

call_info_t * lpc_vm_t::get_call_info()
{
    return this->cur_ci;
}

const object_proto_t * lpc_vm_t::get_frame_proto(const call_info_t *ci) const
{
    if (!ci) {
        return nullptr;
    }
    if (ci->father) {
        return ci->father;
    }
    if (ci->cur_obj) {
        return ci->cur_obj->get_proto();
    }
    return nullptr;
}

const function_proto_t * lpc_vm_t::get_frame_function(const call_info_t *ci) const
{
    if (!ci) {
        return nullptr;
    }
    if (ci->call_init) {
        const object_proto_t *proto = get_frame_proto(ci);
        return proto ? proto->init_fun : nullptr;
    }

    const object_proto_t *proto = get_frame_proto(ci);
    if (!proto || !proto->func_table || ci->funcIdx < 0 || ci->funcIdx >= proto->nfunction) {
        return nullptr;
    }
    return &proto->func_table[ci->funcIdx];
}

const char * lpc_vm_t::get_frame_code_base(const call_info_t *ci) const
{
    if (!ci) {
        return nullptr;
    }
    const object_proto_t *proto = get_frame_proto(ci);
    if (!proto) {
        return nullptr;
    }
    return ci->call_init ? proto->init_codes : proto->instructions;
}

const std::vector<std::pair<luint32_t, luint32_t>> * lpc_vm_t::get_frame_line_map(const call_info_t *ci) const
{
    const object_proto_t *proto = get_frame_proto(ci);
    if (!proto) {
        return nullptr;
    }
    return ci && ci->call_init ? proto->initLineMap : proto->lineMap;
}

bool lpc_vm_t::get_frame_info(const call_info_t *ci, vm_frame_info_t *out) const
{
    if (!ci || !out) {
        return false;
    }

    const object_proto_t *proto = get_frame_proto(ci);
    const function_proto_t *func = get_frame_function(ci);
    const char *base = get_frame_code_base(ci);
    if (!proto || !base || !ci->savepc) {
        return false;
    }

    out->object_name = proto->name;
    out->function_name = ci->call_init ? "<init>" : (func ? func->name : "<unknown>");
    out->pc_offset = static_cast<luint32_t>(ci->savepc - base);
    out->line = 0;
    out->has_line = false;

    const auto *line_map = get_frame_line_map(ci);
    if (!line_map || line_map->empty()) {
        return true;
    }

    for (const auto &it : *line_map) {
        if (it.second <= out->pc_offset) {
            out->line = it.first;
            out->has_line = true;
            continue;
        }
        break;
    }
    return true;
}

call_info_t * lpc_vm_t::new_frame(lpc_object_t *obj, lint16_t idx, bool init, lpc_function_t *callee)
{
    call_info_t *nci = ci_pool.alloc();
    if (idx < 0 && !init) {
        idx = -idx;
        nci->call_other = true;
    }
    
    object_proto_t *proto = obj->get_proto();
    const function_proto_t *f = nullptr;
    if (init) {
        f = proto->init_fun;
        nci->call_init = true;
        nci->savepc = proto->init_codes;
    } else {
        if (cur_ci && cur_ci->father) {
            f = &cur_ci->father->func_table[idx];
            nci->savepc = cur_ci->father->instructions + f->fromPC;
            nci->father = cur_ci->father;
        } else {
            f = &proto->func_table[idx];
            nci->savepc = proto->instructions + f->fromPC;
        }
    }
    
    nci->funcIdx = idx;
    nci->cur_obj = obj;
    nci->callee = callee;
    nci->pre = cur_ci;
    nci->base = stack->top() - (f->nargs > 0 ? f->nargs - 1 : 0);
    nci->top = f->retType > 1 ? stack->top() + f->nlocal + 1 : stack->top() + f->nlocal;

    if (cur_ci) {
        cur_ci->next = nci;
        cur_ci = nci;
    } else {
        cur_ci = nci;
    }

    if (is_profiling() && !init) {
        object_proto_t *p = nci->father ? nci->father : obj->get_proto();
        if (p && idx >= 0 && idx < p->nfunction && p->func_table[idx].name) {
            get_profiler()->CountFunction(p->func_table[idx].name);
        }
    }

    if (!base_ci && !init) {
        base_ci = nci;
    }

    stack->set_local_size(f->nlocal - f->nargs);

    ++ncall;

    return nci;
}

void lpc_vm_t::pop_frame()
{
    call_info_t *pre = cur_ci;
    lpc_value_t *base = pre->base;
    if (pre->call_init) {
        cur_ci = pre->pre;
        ci_pool.release(pre);
        return;
    }
    
    cur_ci = pre->pre;
    if (!cur_ci) {
        if (pre->call_other) {
            cur_ci = nullptr;
            ci_pool.release(pre);
        }
        return;
    }

    cur_ci->next = nullptr;
    object_proto_t *proto = pre->father ? pre->father : pre->cur_obj->get_proto();
    const function_proto_t &f = proto->func_table[pre->funcIdx];

    int n = f.nlocal;
    int n1 = stack->top() - base;
    n = n1 > n ? n1 : n;
    
    if (f.retType > 1) {
        lpc_value_t *ret = stack->pop();
        stack->pop_n(n);
        stack->push(ret);
    } else {
        stack->pop_n(n);
    }
    ci_pool.release(pre);

    if (!cur_ci) {
        base_ci = nullptr;
    }

    --ncall;

    if (ncall % 64 == 0) {
        gc->gc();
    }
}

lpc_gc_t * lpc_vm_t::get_gc()
{
    return this->gc;
}

void lpc_vm_t::set_memory_limit_bytes(luint64_t bytes)
{
    if (gc) {
        gc->set_memory_limit_bytes(bytes);
    }
}

luint64_t lpc_vm_t::memory_limit_bytes() const
{
    return gc ? gc->memory_limit_bytes() : 0;
}

luint64_t lpc_vm_t::allocated_bytes() const
{
    return gc ? gc->allocated_bytes() : 0;
}

luint64_t lpc_vm_t::gc_collect_count() const
{
    return gc ? gc->gc_collect_count() : 0;
}

luint64_t lpc_vm_t::gc_last_freed_bytes() const
{
    return gc ? gc->gc_last_freed_bytes() : 0;
}

luint64_t lpc_vm_t::gc_total_freed_bytes() const
{
    return gc ? gc->gc_total_freed_bytes() : 0;
}

luint64_t lpc_vm_t::gc_last_collected_objects() const
{
    return gc ? gc->gc_last_collected_objects() : 0;
}

luint64_t lpc_vm_t::gc_total_collected_objects() const
{
    return gc ? gc->gc_total_collected_objects() : 0;
}

luint64_t lpc_vm_t::gc_write_barrier_count() const
{
    return gc ? gc->gc_write_barrier_count() : 0;
}

luint64_t lpc_vm_t::gc_minor_collect_count() const
{
    return gc ? gc->gc_minor_collect_count() : 0;
}

luint64_t lpc_vm_t::gc_major_collect_count() const
{
    return gc ? gc->gc_major_collect_count() : 0;
}

luint64_t lpc_vm_t::gc_nursery_bytes() const
{
    return gc ? gc->gc_nursery_bytes() : 0;
}

luint64_t lpc_vm_t::gc_nursery_limit_bytes() const
{
    return gc ? gc->gc_nursery_limit_bytes() : 0;
}

luint64_t lpc_vm_t::gc_remembered_set_size() const
{
    return gc ? gc->gc_remembered_set_size() : 0;
}

luint64_t lpc_vm_t::gc_minor_last_freed_bytes() const
{
    return gc ? gc->gc_minor_last_freed_bytes() : 0;
}

luint64_t lpc_vm_t::gc_minor_total_freed_bytes() const
{
    return gc ? gc->gc_minor_total_freed_bytes() : 0;
}

luint64_t lpc_vm_t::gc_major_last_freed_bytes() const
{
    return gc ? gc->gc_major_last_freed_bytes() : 0;
}

luint64_t lpc_vm_t::gc_major_total_freed_bytes() const
{
    return gc ? gc->gc_major_total_freed_bytes() : 0;
}

luint64_t lpc_vm_t::gc_minor_last_elapsed_us() const
{
    return gc ? gc->gc_minor_last_elapsed_us() : 0;
}

luint64_t lpc_vm_t::gc_minor_total_elapsed_us() const
{
    return gc ? gc->gc_minor_total_elapsed_us() : 0;
}

luint64_t lpc_vm_t::gc_major_last_elapsed_us() const
{
    return gc ? gc->gc_major_last_elapsed_us() : 0;
}

luint64_t lpc_vm_t::gc_major_total_elapsed_us() const
{
    return gc ? gc->gc_major_total_elapsed_us() : 0;
}

void lpc_vm_t::set_nursery_limit_bytes(luint64_t bytes)
{
    if (gc) {
        gc->set_nursery_limit_bytes(bytes);
    }
}

void lpc_vm_t::gc_write_barrier(lpc_gc_object_t *container, const lpc_value_t *value)
{
    if (gc) {
        gc->write_barrier(container, value);
    }
}

void lpc_vm_t::eval_init_codes(lpc_object_t *obj)
{
    object_proto_t *proto = obj->get_proto();
    if (!proto->init_codes) {
        return;
    }

    new_frame(obj, -1, true);
    vm::eval(this);
}

void lpc_vm_t::on_create_object(lpc_object_t *obj)
{
    object_proto_t *proto = obj->get_proto();
    if (proto->create_idx < 0) {
        return;
    }

    new_frame(obj, -proto->create_idx);
    vm::eval(this);
}

void lpc_vm_t::on_load_in_object(lpc_object_t *obj)
{
    object_proto_t *proto = obj->get_proto();
    if (proto->on_load_in_idx < 0) {
        return;
    }

    new_frame(obj, -proto->on_load_in_idx);
    vm::eval(this);
}

void lpc_vm_t::on_destruct_object(lpc_object_t *obj)
{
    object_proto_t *proto = obj->get_proto();
    if (proto->on_destruct_idx < 0) {
        return;
    }

    new_frame(obj, -proto->on_destruct_idx);
    vm::eval(this);
}

void lpc_vm_t::traceback()
{
    cout << traceback_string();
}

std::string lpc_vm_t::current_frame_string() const
{
    std::stringstream buf;
    call_info_t *ci = cur_ci;
    vm_frame_info_t frame;
    if (ci && get_frame_info(ci, &frame)) {
        buf << (frame.object_name ? frame.object_name : "<unknown>")
            << "::"
            << (frame.function_name ? frame.function_name : "<unknown>");
        if (frame.has_line) {
            buf << ":" << (frame.line + 1);
        }
    } else {
        buf << "<no-frame>";
    }
    return buf.str();
}

std::string lpc_vm_t::traceback_string() const
{
    call_info_t *tmp = base_ci;
    std::stringstream buf;
    buf << "stack:\n";
    while (tmp) {
        vm_frame_info_t frame;
        if (get_frame_info(tmp, &frame)) {
            buf << "in file: "
                << (frame.object_name ? frame.object_name : "<unknown>")
                << ", func: "
                << (frame.function_name ? frame.function_name : "<unknown>");
            if (frame.has_line) {
                buf << ", line: " << (frame.line + 1);
            }
            buf << "\n";
        } else {
            buf << "in file: <unknown>, func: <unknown>\n";
        }
        tmp = tmp->next;
    }
    return buf.str();
}

void lpc_vm_t::panic()
{
    if (non_fatal_mode_) {
        if (!has_error()) {
            set_last_error("runtime panic");
        }
        throw vm_abort_signal();
    }

    traceback();
    exit(-1);
}

void lpc_vm_t::stack_overflow()
{
    const char *msg = "runtime error: stack overflow";
    if (non_fatal_mode_) {
        set_last_error(msg);
        throw vm_abort_signal();
    }
    std::cout << msg << std::endl;
    traceback();
    exit(-1);
}

void lpc_vm_t::on_debug_mode()
{
    if (!this->dbg) {
        this->dbg = new lpc_debugger_t(this);
    }
}

bool lpc_vm_t::check_run()
{
    if (dbg) {
        return dbg->can_run();
    } 

    return true;
}

void lpc_vm_t::start_debug()
{
    dbg->start();
}
