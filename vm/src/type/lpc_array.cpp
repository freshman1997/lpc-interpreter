#include "lpc_value.h"
#include "type/lpc_array.h"
#include "memory/memory.h"

lpc_array_t::lpc_array_t(luint32_t sz, lpc_value_t *m) : size(sz), members(m){}

lpc_value_t * lpc_array_t::get(int i)
{
    return &this->members[i];
}

void lpc_array_t::set(lpc_value_t *val, lint32_t i)
{
    this->members[i] = *val;
}

luint32_t lpc_array_t::get_size() const
{
    return size;
}

lpc_value_t * lpc_array_t::copy()
{
    return nullptr;
}

// array op
lpc_array_t * array_add(lpc_array_t *l, lpc_array_t *r, lpc_gc_t *gc)
{
    return nullptr;
}

lpc_array_t * array_sub(lpc_array_t *l, lpc_array_t *r, lpc_gc_t *gc)
{
    return nullptr;
}