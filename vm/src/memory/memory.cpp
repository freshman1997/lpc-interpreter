#include <cstring>

#include "memory/memory.h"
#include "runtime/vm.h"
#include "gc/gc.h"
#include "type/lpc_array.h"
#include "type/lpc_buffer.h"
#include "type/lpc_mapping.h"
#include "type/lpc_proto.h"
#include "type/lpc_object.h"
#include "type/lpc_string.h"

lpc_array_t * lpc_allocator_t::allocate_array(luint32_t size)
{
    lpc_array_t *arr = (lpc_array_t *)vm->get_gc()->allocate(sizeof(lpc_array_t));
    lpc_value_t *m = nullptr;
    if (size > 0) {
        m = (lpc_value_t *)vm->get_gc()->allocate(sizeof(lpc_value_t) * size);
    }
    
    new(arr)lpc_array_t(size, m); // call ctor
    vm->get_gc()->link(reinterpret_cast<lpc_gc_object_t *>(arr), value_type::array_);
    return arr;
}

lpc_mapping_t * lpc_allocator_t::allocate_mapping()
{
    lpc_mapping_t *map = (lpc_mapping_t *)vm->get_gc()->allocate(sizeof(lpc_mapping_t));
    new(map)lpc_mapping_t(this);
    vm->get_gc()->link(reinterpret_cast<lpc_gc_object_t *>(map), value_type::mappig_);
    return map;
}

lpc_object_t * lpc_allocator_t::allocate_object()
{
    lpc_object_t *obj = (lpc_object_t *)vm->get_gc()->allocate(sizeof(lpc_object_t));
    new(obj)lpc_object_t(this);
    vm->get_gc()->link(reinterpret_cast<lpc_gc_object_t *>(obj), value_type::object_);
    return obj;
}

object_proto_t * lpc_allocator_t::allocate_object_proto()
{
    object_proto_t *proto = (object_proto_t *)vm->get_gc()->allocate(sizeof(object_proto_t));
    new(proto)object_proto_t();
    vm->get_gc()->link(reinterpret_cast<lpc_gc_object_t *>(proto), value_type::proto_);
    return proto;
}

lpc_closure_t * lpc_allocator_t::allocate_closure(function_proto_t *funcProto, lpc_object_t *owner)
{
    lpc_closure_t *clo = (lpc_closure_t *)vm->get_gc()->allocate(sizeof(lpc_closure_t));
    clo->proto = funcProto;
    clo->owner = owner;
    clo->init();
    vm->get_gc()->link(reinterpret_cast<lpc_gc_object_t *>(clo), value_type::closure_);
    return clo;
}

lpc_string_t * lpc_allocator_t::allocate_string(const char *init,  bool newOne)
{
    lpc_string_t *str = (lpc_string_t *)vm->get_gc()->allocate(sizeof(lpc_string_t));
    const char *buf = init;
    if (newOne) {
        size_t len = strlen(init);
        buf = (const char *)vm->get_gc()->allocate(len, false);
    }
    new(str)lpc_string_t(buf);
    vm->get_gc()->link(reinterpret_cast<lpc_gc_object_t *>(str), value_type::string_);
    return str;
}

lpc_buffer_t * lpc_allocator_t::allocate_buffer(luint32_t size)
{
    lpc_buffer_t *buf = (lpc_buffer_t *)vm->get_gc()->allocate(sizeof(lpc_buffer_t));
    buf->buff = (const char *)vm->get_gc()->allocate(sizeof(char) * size);
    vm->get_gc()->link(reinterpret_cast<lpc_gc_object_t *>(buf), value_type::buffer_);
    return buf;
}

lpc_function_t * lpc_allocator_t::allocate_function(function_proto_t *funcProto, lpc_object_t *owner, lint16_t idx)
{
    lpc_function_t *f = (lpc_function_t *)vm->get_gc()->allocate(sizeof(lpc_function_t));
    new(f)lpc_function_t();
    f->idx = idx;
    f->proto = funcProto;
    f->owner = owner;
    vm->get_gc()->link(reinterpret_cast<lpc_gc_object_t *>(f), value_type::function_);
    return f;
}

void * lpc_allocator_t::allocate(luint32_t sz, bool check)
{
    return vm->get_gc()->allocate(sz, check);
}

void * lpc_allocator_t::allocate(void *p, luint32_t newSz)
{
    return vm->get_gc()->allocate(p, newSz, false);
}

void lpc_allocator_t::release(luint32_t sz)
{
    vm->get_gc()->release(sz);
}
