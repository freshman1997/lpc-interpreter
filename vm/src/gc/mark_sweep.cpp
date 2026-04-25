#include <memory>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <malloc.h>
#include <chrono>
#include <functional>

#include "type/lpc_array.h"
#include "gc/mark_sweep.h"
#include "runtime/vm.h"
#include "runtime/stack.h"

namespace {

static luint64_t GetAllocSize(void *p)
{
    if (!p) {
        return 0;
    }
#ifdef _WIN32
    return static_cast<luint64_t>(_msize(p));
#else
    return static_cast<luint64_t>(malloc_usable_size(p));
#endif
}

} // namespace

static bool IsAliveInRootList(lpc_gc_object_t *root, lpc_gc_object_t *obj)
{
    while (root) {
        if (root == obj) {
            return true;
        }
        root = root->head.next;
    }
    return false;
}

static void ForEachStackRoot(lpc_vm_t *vm, const std::function<void(lpc_value_t *)> &visit)
{
    if (!vm || !vm->get_stack()) {
        return;
    }
    lpc_stack_t *stack = vm->get_stack();
    call_info_t *ci = vm->get_base_call();
    if (!ci) {
        ci = vm->get_call_info();
    }
    if (!ci) {
        return;
    }

    lint32_t from = -1;
    while (ci) {
        if (ci->base_index >= 0) {
            if (from < 0 || ci->base_index < from) {
                from = ci->base_index;
            }
        } else if (ci->base) {
            lint32_t idx = stack->index_of(ci->base);
            if (idx >= 0 && (from < 0 || idx < from)) {
                from = idx;
            }
        }
        ci = ci->next;
    }

    lint32_t to = stack->get_idx() - 1;
    if (from < 0 || to < from || !stack->valid_range(from, to)) {
        return;
    }
    for (lint32_t i = from; i <= to; ++i) {
        lpc_value_t *val = stack->at_index(i);
        if (val) {
            visit(val);
        }
    }
}

void mark_sweep_gc::remove_from_remembered_set(lpc_gc_object_t *obj)
{
    if (!obj) {
        return;
    }
    for (size_t i = 0; i < remembered_set_.size();) {
        if (remembered_set_[i] == obj) {
            remembered_set_[i] = remembered_set_.back();
            remembered_set_.pop_back();
        } else {
            ++i;
        }
    }
    remembered_set_size_ = static_cast<luint64_t>(remembered_set_.size());
}

void mark_sweep_gc::cleanup_remembered_set()
{
    for (size_t i = 0; i < remembered_set_.size();) {
        lpc_gc_object_t *obj = remembered_set_[i];
        bool keep = obj && IsAliveInRootList(root, obj) && obj->head.generation == 1;
        if (!keep) {
            remembered_set_[i] = remembered_set_.back();
            remembered_set_.pop_back();
        } else {
            ++i;
        }
    }
    remembered_set_size_ = static_cast<luint64_t>(remembered_set_.size());
}

void mark_sweep_gc::mark(lpc_gc_object_t *obj)
{
    if (!obj) {
        this->vm->panic();    
    }
    if (obj->head.marked) {
        return;
    }
    obj->head.marked = 1;
    if (obj->head.generation == 0) {
        if (obj->head.age < 255) {
            obj->head.age += 1;
        }
        if (obj->head.age >= 2) {
            obj->head.generation = 1;
        }
    }
    
    switch ((value_type)obj->head.type)
    {
    case value_type::function_:
    case value_type::buffer_:
    case value_type::string_: {
        break;
    }
    case value_type::closure_: {
        lpc_closure_t *cl = reinterpret_cast<lpc_closure_t *>(obj);
        if (cl->owner) {
            mark(reinterpret_cast<lpc_gc_object_t *>(cl->owner));
        }
        for (int i = 0; i < cl->proto->nupvalue; ++i) {
            lpc_value_t *v = cl->get(i);
            if (v && v->is_gc_type() && v->get_gcobj()) {
                mark(v->get_gcobj());
            }
        }
        break;
    }
    case value_type::array_: {
        lpc_array_t *arr = reinterpret_cast<lpc_array_t *>(obj);
        for (int i = 0; i < arr->get_size(); ++i) {
            lpc_value_t *val = arr->get(i);
            if (val->is_gc_type() && val->get_gcobj()) {
                mark(arr->get(i)->get_gcobj());
            }
        }
        break;
    }
    case value_type::mapping_: {
        lpc_mapping_t *map = reinterpret_cast<lpc_mapping_t *>(obj);
        for (int i = 0; i < map->get_size(); ++i) {
            bucket_t *buck = map->iterate(i);
            lpc_value_t *k = &buck->pair[0];
            lpc_value_t *v = &buck->pair[1];

            if (k->is_gc_type() && k->get_gcobj()) {
                mark(k->get_gcobj());
            }

            if (v->is_gc_type() && v->get_gcobj()) {
                mark(v->get_gcobj());
            }
        }
        map->reset_iterator();
        break;
    }
    case value_type::object_: {
        lpc_object_t *o = reinterpret_cast<lpc_object_t *>(obj);
        lpc_value_t *locs = o->get_locals();
        object_proto_t *proto = o->get_proto();
        for (int i = 0; i < proto->nvariable; ++i) {
            lpc_value_t *val = &locs[i];
            if (val->is_gc_type() && val->get_gcobj()) {
                mark(val->get_gcobj());
            }
        }

        proto->header.marked = 1;
        for (int i = 0; i < proto->nsconst; ++i) {
            proto->sconst[i].item.str->header.marked = 1;
        }

        for (int i = 0; i < proto->ninherit; ++i) {
            lpc_string_t *str = reinterpret_cast<lpc_string_t *>(proto->inherits[i]);
            str->header.marked = 1;
        }
        break;
    }
    
    default:
        break;
    }
}

void mark_sweep_gc::mark_minor(lpc_gc_object_t *obj)
{
    if (!obj || obj->head.marked || obj->head.generation != 0) {
        return;
    }
    mark(obj);
}

lpc_gc_object_t * mark_sweep_gc::mark_root()
{
    auto enqueue_root = [](lpc_gc_object_t *obj, lpc_gc_object_t *&st) {
        if (!obj || obj->head.marked) {
            return;
        }
        obj->head.marked = 1;
        obj->head.gclist = st;
        st = obj;
    };

    lpc_gc_object_t *st = nullptr;
    ForEachStackRoot(vm, [&](lpc_value_t *val) {
        if (val->is_gc_type() && val->get_gcobj()) {
            enqueue_root(val->get_gcobj(), st);
        }
    });

    lpc_mapping_t *map = vm->get_object_cache();
    enqueue_root(reinterpret_cast<lpc_gc_object_t *>(map), st);

    for (int i = 0; i < map->get_size(); ++i) {
        bucket_t *buck = map->iterate(i);
        lpc_value_t *k = &buck->pair[0];
        if (k->is_gc_type() && k->get_gcobj()) {
            enqueue_root(k->get_gcobj(), st);
        }

        lpc_value_t *val = &buck->pair[1];
        if (val->is_gc_type() && val->get_gcobj()) {
            enqueue_root(val->get_gcobj(), st);
        }
    }

    map->reset_iterator();

    return st;
}

void mark_sweep_gc::mark_all(lpc_gc_object_t *obj)
{
    if (!obj) return;

    lpc_gc_object_t *cur = obj;
    while (cur) {
        lpc_gc_object_t *next = cur->head.gclist;
        cur->head.marked = 0;
        mark(cur);
        cur = next;
    }
}

void mark_sweep_gc::mark_phase()
{
    lpc_gc_object_t *root = mark_root();
    if (!root) return;

    mark_all(root);
}

void mark_sweep_gc::minor_mark_phase()
{
    ForEachStackRoot(vm, [&](lpc_value_t *val) {
        if (val->is_gc_type() && val->get_gcobj()) {
            mark_minor(val->get_gcobj());
        }
    });

    lpc_mapping_t *map = vm->get_object_cache();
    if (map) {
        mark_minor(reinterpret_cast<lpc_gc_object_t *>(map));
    }

    for (lpc_gc_object_t *obj : remembered_set_) {
        if (!obj) {
            continue;
        }
        switch ((value_type)obj->head.type)
        {
        case value_type::array_: {
            lpc_array_t *arr = reinterpret_cast<lpc_array_t *>(obj);
            for (int i = 0; i < arr->get_size(); ++i) {
                lpc_value_t *v = arr->get(i);
                if (v->is_gc_type() && v->get_gcobj()) {
                    mark_minor(v->get_gcobj());
                }
            }
            break;
        }
        case value_type::mapping_: {
            lpc_mapping_t *m = reinterpret_cast<lpc_mapping_t *>(obj);
            for (int i = 0; i < m->get_size(); ++i) {
                bucket_t *buck = m->iterate(i);
                lpc_value_t *k = &buck->pair[0];
                lpc_value_t *v = &buck->pair[1];
                if (k->is_gc_type() && k->get_gcobj()) {
                    mark_minor(k->get_gcobj());
                }
                if (v->is_gc_type() && v->get_gcobj()) {
                    mark_minor(v->get_gcobj());
                }
            }
            m->reset_iterator();
            break;
        }
        case value_type::object_: {
            lpc_object_t *o = reinterpret_cast<lpc_object_t *>(obj);
            object_proto_t *proto = o->get_proto();
            lpc_value_t *locs = o->get_locals();
            for (int i = 0; i < proto->nvariable; ++i) {
                lpc_value_t *v = &locs[i];
                if (v->is_gc_type() && v->get_gcobj()) {
                    mark_minor(v->get_gcobj());
                }
            }
            break;
        }
        case value_type::closure_: {
            lpc_closure_t *cl = reinterpret_cast<lpc_closure_t *>(obj);
            if (!cl->proto) {
                break;
            }
            for (int i = 0; i < cl->proto->nupvalue; ++i) {
                lpc_value_t *v = cl->get(i);
                if (v && v->is_gc_type() && v->get_gcobj()) {
                    mark_minor(v->get_gcobj());
                }
            }
            break;
        }
        default:
            break;
        }
    }
}

void mark_sweep_gc::minor_sweep_phase()
{
    if (!root) {
        nursery_bytes_ = 0;
        return;
    }

    lpc_gc_object_t *cur = root;
    lpc_gc_object_t *pre = nullptr;
    lpc_gc_object_t *start = nullptr;
    lint64_t freed = 0;
    lint32_t count = 0;
    while (cur) {
        lpc_gc_object_t *next = cur->head.next;
        bool is_young = (cur->head.generation == 0);
        if (is_young && !cur->head.marked) {
            free_object(cur, freed);
            ++count;
        } else {
            if (!pre) {
                start = cur;
            } else {
                pre->head.next = cur;
            }
            pre = cur;
            cur->head.marked = 0;
        }
        cur = next;
    }

    if (pre) {
        pre->head.next = nullptr;
    }
    root = start;

    if (freed < 0) {
        freed = 0;
    }
    if (static_cast<luint64_t>(freed) > blocks) {
        blocks = 0;
    } else {
        blocks -= static_cast<luint64_t>(freed);
    }
    if (count > total_objects) {
        total_objects = 0;
    } else {
        total_objects -= count;
    }

    cleanup_remembered_set();

    last_freed_bytes_ = static_cast<luint64_t>(freed);
    total_freed_bytes_ += static_cast<luint64_t>(freed);
    last_collected_objects_ = static_cast<luint64_t>(count);
    total_collected_objects_ += static_cast<luint64_t>(count);

    cleanup_remembered_set();

    minor_last_freed_bytes_ = static_cast<luint64_t>(freed);
    minor_total_freed_bytes_ += static_cast<luint64_t>(freed);
    minor_last_collected_objects_ = static_cast<luint64_t>(count);
    minor_total_collected_objects_ += static_cast<luint64_t>(count);

    nursery_bytes_ = 0;
}

void mark_sweep_gc::free_object(lpc_gc_object_t *obj, lint64_t & freeBytes)
{
    switch ((value_type)obj->head.type)
    {
    case value_type::function_: {
        remove_from_remembered_set(obj);
        free(obj);
        freeBytes += sizeof(lpc_function_t);
        break;
    }
    case value_type::string_: {
        lpc_string_t *str = reinterpret_cast<lpc_string_t *>(obj);
        remove_from_remembered_set(obj);
        if (str->owns_str() && str->get_str()) {
            free(const_cast<char *>(str->get_str()));
        }
        free(str);
        freeBytes += static_cast<lint32_t>(GetAllocSize(str));
        break;
    }
    case value_type::array_: {
        lpc_array_t *arr = reinterpret_cast<lpc_array_t *>(obj);
        remove_from_remembered_set(obj);
        luint64_t arr_obj_sz = GetAllocSize(arr);
        luint64_t members_sz = 0;
        if (arr->get_size() > 0) {
            members_sz = GetAllocSize(arr->get_members());
            free(arr->get_members());
        }

        free(arr);
        freeBytes += static_cast<lint32_t>(arr_obj_sz + members_sz);
        break;
    }
    case value_type::mapping_: {
        lpc_mapping_t *map = reinterpret_cast<lpc_mapping_t *>(obj);
        remove_from_remembered_set(obj);
        map->dtor(freeBytes);
        luint64_t map_obj_sz = GetAllocSize(map);
        free(map);
        freeBytes += static_cast<lint64_t>(map_obj_sz);
        break;
    }
    case value_type::closure_: {
        lpc_closure_t *clo = reinterpret_cast<lpc_closure_t *>(obj);
        remove_from_remembered_set(obj);
        clo->dtor(vm->get_alloc());
        luint64_t clo_sz = GetAllocSize(clo);
        free(clo);
        freeBytes += static_cast<lint64_t>(clo_sz);
        break;
    }
    case value_type::buffer_: {
        lpc_buffer_t *buff = reinterpret_cast<lpc_buffer_t *>(obj);
        remove_from_remembered_set(obj);
        luint64_t buff_obj_sz = GetAllocSize(buff);
        luint64_t buff_data_sz = 0;
        if (buff->buff) {
            buff_data_sz = GetAllocSize(const_cast<char *>(buff->buff));
            free(const_cast<char *>(buff->buff));
        }
        free(buff);
        freeBytes += static_cast<lint64_t>(buff_obj_sz + buff_data_sz);
        break;
    }
    case value_type::object_: {
        lpc_object_t *o = reinterpret_cast<lpc_object_t *>(obj);
        remove_from_remembered_set(obj);
        luint64_t obj_sz = GetAllocSize(o);

        object_proto_t *proto = o->get_proto();
        if (o->get_name() != proto->name) {
            free(const_cast<char *>(o->get_name()));
        }

        if (proto->nvariable > 0) {
            free(o->get_locals());
        }
        free(o);
        freeBytes += static_cast<lint64_t>(obj_sz);
        break;
    }
    case value_type::proto_: {
        object_proto_t *proto = reinterpret_cast<object_proto_t *>(obj);
        remove_from_remembered_set(obj);
        free(const_cast<char *>(proto->name));

        if (proto->inherits) {
            delete [] proto->inherits;
        }

        if (proto->instructions) {
            free(const_cast<char *>(proto->instructions));
        }
        delete [] proto->inherit_offsets;

        if (proto->init_codes) {
            free(const_cast<char *>(proto->init_codes));
        }

        if (proto->init_fun) {
            delete proto->init_fun;
        }

        if (proto->variable_table) {
            delete[] proto->variable_table;
        }

        if (proto->iconst) {
            delete [] proto->iconst;
        }

        if (proto->fconst) {
            delete [] proto->fconst;
        }

        if (proto->sconst) {
            delete [] proto->sconst;
        }

        if (proto->class_table) {
            if (proto->class_table->nfield > 0) {
                delete [] proto->class_table->field_table;
            }
            delete [] proto->class_table;
        }

        if (proto->func_table) {
            for (int i = 0; i < proto->nfunction; ++i) {
                if (proto->func_table[i].upvalue_source_kind) {
                    delete [] proto->func_table[i].upvalue_source_kind;
                }
                if (proto->func_table[i].upvalue_source_index) {
                    delete [] proto->func_table[i].upvalue_source_index;
                }
            }
            delete [] proto->func_table;
        }

        if (proto->loc_tags) {
            delete [] proto->loc_tags;
        }

        if (proto->lookup_table) {
            delete proto->lookup_table;
            delete proto->defaults;
        }

        if (proto->initLineMap) {
            delete proto->initLineMap;
        }
        if (proto->lineMap) {
            delete proto->lineMap;
        }
        
        luint64_t proto_sz = GetAllocSize(proto);
        free(proto);
        freeBytes += static_cast<lint64_t>(proto_sz);
        break;
    }

    default: break;
    }
}

void mark_sweep_gc::sweep_phase()
{
    if (!root) return;

    lpc_gc_object_t *cur = root;
    lpc_gc_object_t *pre = nullptr, *start = nullptr;
    lint32_t count = 0;
    lint64_t freeBytes = 0;
    while (cur) {
        if (cur->head.marked) {
            if (!pre) {
                pre = cur;
                start = cur;
            } else {
                pre->head.next = cur;
                pre = cur;
            }

            cur->head.marked = 0;
            cur = cur->head.next;
        } else {
            lpc_gc_object_t * t = cur;
            cur = cur->head.next;
            free_object(t, freeBytes);
            ++count;
        }
    }

    if (pre) {
        pre->head.next = nullptr;
    }

    root = start;
    if (static_cast<luint64_t>(freeBytes) > blocks) {
        blocks = 0;
    } else {
        blocks -= static_cast<luint64_t>(freeBytes);
    }
    total_objects -= count;
    if (freeBytes < 0) {
        freeBytes = 0;
    }
    last_freed_bytes_ = static_cast<luint64_t>(freeBytes);
    total_freed_bytes_ += static_cast<luint64_t>(freeBytes);
    last_collected_objects_ = static_cast<luint64_t>(count);
    total_collected_objects_ += static_cast<luint64_t>(count);


}

void mark_sweep_gc::collect()
{
    const auto t0 = std::chrono::steady_clock::now();
    ++collect_count_;
    mark_phase();
    sweep_phase();
    const auto t1 = std::chrono::steady_clock::now();
    const luint64_t us = static_cast<luint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
    major_last_elapsed_us_ = us;
    major_total_elapsed_us_ += us;
    major_last_freed_bytes_ = last_freed_bytes_;
    major_total_freed_bytes_ += last_freed_bytes_;
    major_last_collected_objects_ = last_collected_objects_;
    major_total_collected_objects_ += last_collected_objects_;
}

void * mark_sweep_gc::allocate(void *p, luint32_t sz, bool check)
{
    luint64_t old_sz = GetAllocSize(p);
    luint64_t projected = blocks - (old_sz > blocks ? blocks : old_sz) + static_cast<luint64_t>(sz);
    if (projected > gc_threshold) {
        collect();
        projected = blocks - (old_sz > blocks ? blocks : old_sz) + static_cast<luint64_t>(sz);
        if (projected > gc_threshold) {
            vm->set_last_error(std::string("memory limit exceeded: ") + std::to_string(projected) + " > " + std::to_string(gc_threshold));
            if (vm->non_fatal_mode()) {
                throw vm_abort_signal();
            }
            vm->panic();
        }
    }

    void *ptr = malloc(sz);
    if (!ptr) {
        vm->panic();
    }
    
    if (p) {
        memcpy(ptr, p, static_cast<size_t>(old_sz < sz ? old_sz : sz));
        free(p);
    }

    if (check) {
        nursery_bytes_ += static_cast<luint64_t>(sz);
        if (p && old_sz > 0) {
            if (old_sz > nursery_bytes_) {
                nursery_bytes_ = 0;
            } else {
                nursery_bytes_ -= old_sz;
            }
        }
        maybe_collect_minor();
        check_threshold();
    }

    blocks = projected;

    return ptr;
}

void mark_sweep_gc::link(lpc_gc_object_t *gcobj, value_type type)
{
    gcobj->head.next = this->root;
    gcobj->head.type = (lint8_t)type;
    gcobj->head.marked = 0;
    gcobj->head.generation = 0;
    gcobj->head.age = 0;
    this->root = gcobj;
    ++total_objects;
}

void mark_sweep_gc::write_barrier(lpc_gc_object_t *container, const lpc_value_t *value)
{
    ++write_barrier_count_;
    if (!container) {
        return;
    }

    if (!value || !value->is_gc_type() || !value->get_gcobj()) {
        return;
    }

    lpc_gc_object_t *child = value->get_gcobj();
    if (container->head.generation != 1 || child->head.generation != 0) {
        return;
    }

    for (lpc_gc_object_t *it : remembered_set_) {
        if (it == container) {
            remembered_set_size_ = static_cast<luint64_t>(remembered_set_.size());
            return;
        }
    }
    remembered_set_.push_back(container);
    remembered_set_size_ = static_cast<luint64_t>(remembered_set_.size());
}
