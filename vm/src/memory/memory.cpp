#include "memory/memory.h"
#include "runtime/vm.h"
#include "gc/gc.h"
#include "type/lpc_array.h"
#include "type/lpc_mapping.h"

lpc_array_t * lpc_allocator_t::allocate_array(int size)
{
    lpc_array_t *arr = (lpc_array_t *)vm->get_gc()->allocate(sizeof(lpc_array_t));
    lpc_value_t *m = (lpc_value_t *)vm->get_gc()->allocate(sizeof(lpc_value_t) * size);
    new(arr)(lpc_array_t(size, m)); // call ctor
    return arr;
}

lpc_mapping_t * lpc_allocator_t::allocate_mapping()
{
    lpc_mapping_t *map = (lpc_mapping_t *)vm->get_gc()->allocate(sizeof(lpc_mapping_t));
    new(map)(lpc_mapping_t(vm->get_gc()));
    return map;
}

lpc_value_t * lpc_allocator_t::allocate_object()
{
    return nullptr;
}

lpc_value_t * lpc_allocator_t::allocate_closure()
{
    return nullptr;
}

lpc_value_t * lpc_allocator_t::allocate_string()
{
    return nullptr;
}

lpc_value_t * lpc_allocator_t::allocate_buffer(int size)
{

    return nullptr;
}

lpc_value_t * lpc_allocator_t::allocate_function()
{
    return nullptr;
}

void * lpc_allocator_t::pick_chunk(int size)
{
    return nullptr;
}

void * lpc_allocator_t::allocate(int size)
{
    void *chunk = pick_chunk(size);
    if (chunk) {
        return chunk;
    } else {
        // TODO allocate fail
    }
    return nullptr;
}

