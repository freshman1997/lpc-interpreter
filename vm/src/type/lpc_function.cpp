#include "memory/memory.h"
#include "type/lpc_function.h"

lpc_function_t * lpc_function_t::copy(lpc_allocator_t *alloc)
{
    return alloc->allocate_function(proto, owner, idx);
}
