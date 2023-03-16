#include "memory/memory.h"
#include "runtime/vm.h"
#include "gc/gc.h"
#include "type/lpc_array.h"
#include "type/lpc_mapping.h"
#include "type/lpc_proto.h"
#include "type/lpc_object.h"

lpc_array_t * lpc_allocator_t::allocate_array(luint32_t size)
{
    lpc_array_t *arr = (lpc_array_t *)vm->get_gc()->allocate(sizeof(lpc_array_t));
    lpc_value_t *m = (lpc_value_t *)vm->get_gc()->allocate(sizeof(lpc_value_t) * size);
    new(arr)lpc_array_t(size, m); // call ctor
    return arr;
}

lpc_mapping_t * lpc_allocator_t::allocate_mapping()
{
    lpc_mapping_t *map = (lpc_mapping_t *)vm->get_gc()->allocate(sizeof(lpc_mapping_t));
    new(map)lpc_mapping_t(vm->get_gc());
    vm->get_gc()->link(reinterpret_cast<lpc_gc_object_t *>(map), value_type::mappig_);
    return map;
}

lpc_object_t * lpc_allocator_t::allocate_object()
{
    lpc_object_t *obj = (lpc_object_t *)vm->get_gc()->allocate(sizeof(lpc_object_t));
    new(obj)lpc_object_t();
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

lpc_value_t * lpc_allocator_t::allocate_closure()
{
    return nullptr;
}

lpc_value_t * lpc_allocator_t::allocate_string()
{
    return nullptr;
}

lpc_value_t * lpc_allocator_t::allocate_buffer(luint32_t size)
{

    return nullptr;
}

lpc_value_t * lpc_allocator_t::allocate_function()
{
    return nullptr;
}

void * lpc_allocator_t::pick_chunk(luint32_t size)
{
    return nullptr;
}

void * lpc_allocator_t::allocate(luint32_t size)
{
    void *chunk = pick_chunk(size);
    if (chunk) {
        return chunk;
    } else {
        // TODO allocate fail
    }
    return nullptr;
}

