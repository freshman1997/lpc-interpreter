#include <iostream>
#include <memory>
#include <cstdlib>
#include "type/lpc_array.h"
#include "gc/mark_sweep.h"
#include "runtime/vm.h"

void mark_sweep_gc::mark(lpc_gc_object_t *obj)
{
    switch ((value_type)obj->head.type)
    {
    case value_type::function_:
    case value_type::string_: {
        obj->head.marked = 1;
        break;
    }
    case value_type::array_: {
        obj->head.marked = 1;
        lpc_array_t *arr = reinterpret_cast<lpc_array_t *>(obj);
        for (int i = 0; i < arr->get_size(); ++i) {
            lpc_value_t *val = arr->get(i);
            if (val->type >= value_type::buffer_) {
                mark(arr->get(i)->gcobj);
            }
        }
        break;
    }
    case value_type::mappig_: {
        obj->head.marked = 1;
        lpc_mapping_t *map = reinterpret_cast<lpc_mapping_t *>(obj);
        for (bucket_t *it = map->get_begin(); it; it = it->next) {
            lpc_value_t *k = &it->pair[0];
            lpc_value_t *v = &it->pair[1];

            if (k->type >= value_type::buffer_) {
                mark(it->pair[0].gcobj);
            }

            if (v->type >= value_type::buffer_) {
                mark(it->pair[1].gcobj);
            }
        }
        break;
    }
    case value_type::object_: {
        obj->head.marked = 1;
        lpc_object_t *o = reinterpret_cast<lpc_object_t *>(obj);
        lpc_value_t *locs = o->get_locals();
        for (int i = 0; i < o->get_proto()->nvariable; ++i) {
            lpc_value_t *val = &locs[i];
            if (val->type >= value_type::buffer_) {
                mark(locs[i].gcobj);
            }
        }

        object_proto_t *proto = o->get_proto();
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

lpc_gc_object_t * mark_sweep_gc::mark_root()
{
    call_info_t *ci = vm->get_base_call();
    if (!ci) {
        return nullptr;
    }

    lpc_gc_object_t *st = nullptr;
    while (ci) {
        for (lpc_value_t *val = ci->base; val <= ci->top; ++val) {
            if (val->type >= value_type::buffer_ && !val->gcobj->head.marked) {
                val->gcobj->head.marked = 1;
                val->gcobj->head.gclist = st;
                st = val->gcobj;
            }
        }

        ci = ci->next;
    }

    lpc_mapping_t *map = vm->get_object_cache();
    map->header.marked = 1;
    map->header.gclist = st;
    st = reinterpret_cast<lpc_gc_object_t *>(map);

    for (bucket_t *it = map->get_begin(); it; it = it->next) {
        lpc_value_t *k = &it->pair[0];
        if (k->type >= value_type::buffer_ && !k->gcobj->head.marked) {
            k->gcobj->head.marked = 1;
        }

        lpc_value_t *val = &it->pair[1];
        if (val->type >= value_type::buffer_ && !val->gcobj->head.marked) {
            val->gcobj->head.marked = 1;
            val->gcobj->head.gclist = st;
            st = val->gcobj;
        }
    }

    return st;
}

void mark_sweep_gc::mark_all(lpc_gc_object_t *obj)
{
    if (!obj) return;

    lpc_gc_object_t *cur = obj;
    while (cur) {
        mark(cur);
        cur = cur->head.gclist;
    }
}

void mark_sweep_gc::mark_phase()
{
    // 1、找到入口对象的栈帧，遍历构建 gc root
    // 2、遍历 gc root 标记，包括对应栈帧，字符串，对象，数组，hash 表，函数
    lpc_gc_object_t *root = mark_root();
    if (!root) return;

    mark_all(root);
}

void mark_sweep_gc::free_object(lpc_gc_object_t *obj)
{
    switch ((value_type)obj->head.type)
    {
    case value_type::function_:
    case value_type::string_: {

        break;
    }
    case value_type::array_: {

        break;
    }
    case value_type::mappig_: {

        break;
    }
    case value_type::object_: {

    }
    case value_type::proto_: {

    }

    default: break;
    }
}

void mark_sweep_gc::sweep_phase()
{
    if (!root) return;

    lpc_gc_object_t *cur = root;
    lpc_gc_object_t *pre = nullptr;
    lint32_t count = 0;
    while (cur) {
        if (cur->head.marked) {
            cur->head.marked = 0;
            if (!pre) {
                pre = cur;
            } else {
                pre->head.next = cur;
                pre = cur;
            }
            cur = cur->head.next;
        } else {
            lpc_gc_object_t * t = cur;
            cur = cur->head.next;
            free_object(t);
            ++count;
        }
    }

    root = pre;

    std::cout << "free: " << count << "\n";
}

void mark_sweep_gc::collect()
{
    mark_phase();
    sweep_phase();
}

void * mark_sweep_gc::allocate(void *p, luint32_t sz)
{
    void *ptr = realloc(p, sz);
    if (!ptr) {
        vm->panic();
    }
    
    blocks += sz;

    return ptr;
}

void mark_sweep_gc::link(lpc_gc_object_t *gcobj, value_type type)
{
    gcobj->head.next = this->root;
    gcobj->head.type = (lint8_t)type;
    gcobj->head.marked = 0;
    this->root = gcobj;
    ++total_objects;
}

