#include "memory/memory.h"

lpc_value_t * lpc_allocator_t::allocate_array(int size)
{
    return nullptr;
}

lpc_value_t * lpc_allocator_t::allocate_mapping()
{
    return nullptr;
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

