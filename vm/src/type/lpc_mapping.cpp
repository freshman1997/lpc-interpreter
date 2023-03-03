#include "type/lpc_array.h"
#include "type/lpc_mapping.h"

int calc_hash(lpc_value_t *val)
{
    return 100 ^ 2;
}

lpc_value_t * lpc_mapping_t::copy()
{
    return nullptr;
}

lpc_value_t * lpc_mapping_t::get(lpc_value_t *k)
{
    return nullptr;
}

void set(lpc_value_t *k, lpc_value_t *v)
{

}

lpc_value_t * upset(lpc_value_t *k, lpc_value_t *v)
{
    return nullptr;
}

bucket_t * lpc_mapping_t::get_members()
{
    return this->members;
}

luint32_t lpc_mapping_t::get_size()
{
    return this->size;
}

bucket_t * lpc_mapping_t::get_bucket(int i)
{
    if (i >= size) return nullptr;
    return members + i;
}

// interor op
void lpc_mapping_iterator_t::set_mapping(lpc_mapping_t *m)
{
    this->m = m;
}

bool lpc_mapping_iterator_t::has_next() const
{
    return idx < m->size;
}
    
bucket_t * lpc_mapping_iterator_t::next()
{
    return m->get_bucket(idx++);
}


lpc_array_t * mapping_values(lpc_mapping_t *)
{
    return nullptr;
}

lpc_array_t * mapping_keys(lpc_mapping_t *)
{
    return nullptr;
}

void map_delete(lpc_mapping_t *map, lpc_value_t *k)
{
    if (map->get(k) == nullptr) return;
    
}

