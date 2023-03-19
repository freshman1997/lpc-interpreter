#include "lpc_value.h"
#include "gc/gc.h"
#include "type/lpc_string.h"
#include "type/lpc_array.h"
#include "type/lpc_mapping.h"

extern int hash_(const char *str);
extern int hash_pointer(int x);

int lpc_mapping_t::calc_hash(lpc_value_t *val)
{
    if (val->type == value_type::string_) return hash_(val->gcobj->str.get_str());
    else if (val->type == value_type::int_ || val->type == value_type::float_) {
        return hash_pointer(val->pval.number);
    } else {
        return 0;
    }
}

lpc_value_t * lpc_mapping_t::copy()
{
    return nullptr;
}

lpc_mapping_t::lpc_mapping_t(lpc_gc_t *gc)
{
    this->size = 200;
    this->members = (bucket_t *)gc->allocate(sizeof(bucket_t) * 200);
    fill = 0;
    this->gc = gc;
}

bucket_t * lpc_mapping_t::get(lpc_value_t *k)
{
    int hash = calc_hash(k) % this->size;
    bucket_t *b = &members[hash];
    bucket_t *target = nullptr;
    while (b->next) {
        if (k->type == value_type::int_ || k->type == value_type::float_) {
            if (b->pair.key->type == value_type::int_ || b->pair.key->type == value_type::float_) {
                if (b->pair.key->pval.number != k->pval.number) {
                    b = b->next;
                } else {
                    target = b;
                    break;
                }
            }
        } else if (k->type == value_type::string_ && b->pair.key->type == value_type::string_)
        {
            if (k->gcobj->str.get_hash() != b->pair.key->gcobj->str.get_hash()) {
                b = b->next;
            } else {
                target = b;
                break;
            }
        }

        b = b->next;
    }
    
    return target ? target : nullptr;
}

void lpc_mapping_t::set(lpc_value_t *k, lpc_value_t *v)
{
    bucket_t *found = get(k);
    if (found->pair.key) {
        bucket_t *b = new bucket_t;
        b->pair.key = k;
        b->pair.val = v;
        found->next = b;
    } else {
        found->pair.key = k;
        found->pair.val = v;
    }
}

void lpc_mapping_t::upset(lpc_value_t *k, lpc_value_t *v)
{
    bucket_t *found = get(k);
    if (found) {
        // TODO free or not
        found->pair.key = k;
        found->pair.val = v;
    } else {
        int hash = calc_hash(k) % this->size;
        bucket_t *b = &members[hash];
        while (b->next) {
            b = b->next;
        }

        if (b->pair.key) {
            bucket_t *node = new bucket_t;
            node->pair.key = k;
            node->pair.val = v;
            b->next = node;
        } else {
            b->pair.key = k;
            b->pair.val = v;
        }
    }
}

void lpc_mapping_t::remove(lpc_value_t *k)
{
    bucket_t *found = get(k);
    if (!found) return;

    int hash = calc_hash(k) % this->size;
    bucket_t *b = &members[hash];
    bucket_t *pre = nullptr;
    while (b->next) {
        if (k->type == value_type::int_ || k->type == value_type::float_) {
            if (b->pair.key->type == value_type::int_ || b->pair.key->type == value_type::float_) {
                if (b->pair.key->pval.number != k->pval.number) {
                    b = b->next;
                } else {
                    if (pre) {
                        // TODO
                        pre->next = nullptr;
                    } else {
                        b->pair.key = nullptr;
                        b->pair.val = nullptr;
                    }
                    break;
                }
            }
        } else if (k->type == value_type::string_ && b->pair.key->type == value_type::string_)
        {
            if (k->gcobj->str.get_hash() != b->pair.key->gcobj->str.get_hash()) {
                b = b->next;
            } else {
                if (pre) {
                    pre->next = nullptr;
                } else {
                    // TODO
                    b->pair.key = nullptr;
                    b->pair.val = nullptr;
                }
                break;
            }
        }
        pre = b;
        b = b->next;
    }
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


lpc_array_t * mapping_values(lpc_mapping_t *m, lpc_gc_t *gc)
{
    // TODO
    lpc_array_t *arr = (lpc_array_t *)gc->allocate(sizeof(lpc_array_t));
    
    return nullptr;
}

lpc_array_t * mapping_keys(lpc_mapping_t *m)
{
    return nullptr;
}

void map_delete(lpc_mapping_t *map, lpc_value_t *k)
{
    map->remove(k);
}

