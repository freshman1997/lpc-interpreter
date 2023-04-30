#include <iostream>
#include <memory>
#include <cstdlib>
#include <cstring>
#include <cassert>

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
        for (int i = 0; i < map->get_size(); ++i) {
            bucket_t *buck = map->iterate(i);
            lpc_value_t *k = &buck->pair[0];
            lpc_value_t *v = &buck->pair[1];

            if (k->type >= value_type::buffer_) {
                mark(k->gcobj);
            }

            if (v->type >= value_type::buffer_) {
                mark(v->gcobj);
            }
        }
        map->reset_iterator();
        break;
    }
    case value_type::object_: {
        obj->head.marked = 1;
        lpc_object_t *o = reinterpret_cast<lpc_object_t *>(obj);
        lpc_value_t *locs = o->get_locals();
        object_proto_t *proto = o->get_proto();
        for (int i = 0; i < proto->nvariable; ++i) {
            lpc_value_t *val = &locs[i];
            if (val->type >= value_type::buffer_) {
                mark(locs[i].gcobj);
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

    for (int i = 0; i < map->get_size(); ++i) {
        bucket_t *buck = map->iterate(i);
        lpc_value_t *k = &buck->pair[0];
        if (k->type >= value_type::buffer_ && !k->gcobj->head.marked) {
            k->gcobj->head.marked = 1;
        }

        lpc_value_t *val = &buck->pair[1];
        if (val->type >= value_type::buffer_ && !val->gcobj->head.marked) {
            val->gcobj->head.marked = 1;
            val->gcobj->head.gclist = st;
            st = val->gcobj;
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

void mark_sweep_gc::free_object(lpc_gc_object_t *obj, lint32_t & freeBytes)
{
    switch ((value_type)obj->head.type)
    {
    case value_type::function_: {
        free(obj);
        freeBytes += sizeof(lpc_function_t);
        break;
    }
    case value_type::string_: {
        lpc_string_t *str = reinterpret_cast<lpc_string_t *>(obj);
        delete [] str->get_str();
        free(str);
        freeBytes += sizeof(lpc_string_t);
        break;
    }
    case value_type::array_: {
        lpc_array_t *arr = reinterpret_cast<lpc_array_t *>(obj);
        if (arr->get_size() > 0) {
            for (int i = 0; i < arr->get_size(); ++i) {
                lpc_value_t *val = arr->get(i);
                if (val->type >= value_type::buffer_) {
                    free_object(val->gcobj, freeBytes);
                }
            }

            free(arr->get_members());
        }

        free(arr);
        freeBytes += sizeof(lpc_array_t);
        break;
    }
    case value_type::mappig_: {
        lpc_mapping_t *map = reinterpret_cast<lpc_mapping_t *>(obj);
        map->dtor();
        free(map);
        freeBytes += sizeof(lpc_mapping_t);
        break;
    }
    case value_type::closure_: {
        lpc_closure_t *clo = reinterpret_cast<lpc_closure_t *>(obj);
        clo->dtor();
        free(clo);
        freeBytes += sizeof(lpc_closure_t);
        break;
    }
    case value_type::buffer_: {
        lpc_buffer_t *buff = reinterpret_cast<lpc_buffer_t *>(obj);
        if (buff->buff) {
            delete [] buff->buff;
        }
        free(buff);
        freeBytes += sizeof(lpc_buffer_t);
        break;
    }
    case value_type::object_: {
        lpc_object_t *o = reinterpret_cast<lpc_object_t *>(obj);
        object_proto_t *proto = o->get_proto();
        if (o->get_name() != proto->name) {
            delete [] o->get_name();
        }

        if (proto->nvariable > 0) {
            delete [] o->get_locals();
        }
        free(o);
        freeBytes += sizeof(lpc_object_t);
        break;
    }
    case value_type::proto_: {
        object_proto_t *proto = reinterpret_cast<object_proto_t *>(obj);
        delete [] proto->name;

        if (proto->inherits) {
            delete [] proto->inherits;
        }

        delete [] proto->instructions;
        delete [] proto->inherit_offsets;

        if (proto->init_codes) {
            delete [] proto->init_codes;
        }

        if (proto->init_fun) {
            delete [] proto->init_codes;
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
            for (int i = 0; i < proto->nsconst; ++i) {
                free_object(reinterpret_cast<lpc_gc_object_t *>(proto->sconst[i].item.str), freeBytes);
            }
        }

        if (proto->class_table) {
            if (proto->class_table->nfield > 0) {
                delete [] proto->class_table->field_table;
            }
            delete [] proto->class_table;
        }

        if (proto->func_table) {
            delete [] proto->func_table;
        }

        if (proto->loc_tags) {
            delete [] proto->loc_tags;
        }

        if (proto->lookup_table) {
            delete proto->lookup_table;
            delete proto->defaults;
        }
        
        free(proto);
        freeBytes += sizeof(object_proto_t);
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
    lint32_t count = 0, freeBytes = 0;
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

    root = start;
    blocks -= freeBytes;
    total_objects -= count;

    assert(total_objects > 0 && blocks > 0 && count > 0);

    std::cout << "free object: " << count << ", total bytes: " << freeBytes << std::endl;
    std::cout.flush();
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
    
    check_threshold();
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

