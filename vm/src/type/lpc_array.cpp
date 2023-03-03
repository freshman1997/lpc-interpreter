#include "lpc_value.h"
#include "type/lpc_array.h"
#include "memory/memory.h"

lpc_value_t * lpc_array_t::get(int i)
{
    return nullptr;
}

void lpc_array_t::set(lpc_value_t *val, int i)
{

}

luint32_t lpc_array_t::get_size() const
{
    return size;
}

lpc_value_t * lpc_array_t::copy()
{
    return nullptr;
}

void lpc_array_iterator_t::set_array(lpc_array_t *_arr)
{
    this->arr = _arr;
}

bool lpc_array_iterator_t::has_next()
{
    return idx < arr->size;
}

lpc_value_t * lpc_array_iterator_t::next()
{
    return arr->get(idx++);
}

// array op
lpc_array_t * array_add(lpc_array_t *l, lpc_array_t *r)
{
    return nullptr;
}

lpc_array_t * array_sub(lpc_array_t *l, lpc_array_t *r)
{
    return nullptr;
}