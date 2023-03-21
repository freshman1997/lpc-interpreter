#include "lpc_value.h"
#include "gc/gc.h"
#include "type/lpc_string.h"
#include "type/lpc_array.h"
#include "type/lpc_mapping.h"

extern int hash_(const char *str);
extern int hash_pointer(int x);

int lpc_mapping_t::calc_hash(lpc_value_t *val)
{
    if (val->type == value_type::string_) {
        lpc_string_t *str = reinterpret_cast<lpc_string_t *>(val->gcobj);
        return str->get_hash();
    } else {
        return hash_pointer(val->pval.number);
    }
}

lpc_value_t * lpc_mapping_t::copy()
{
    return nullptr;
}

lpc_mapping_t::lpc_mapping_t(lpc_gc_t *gc)
{
    this->size = 200;
    this->members = new bucket_t[size];
    fill = 0;
    this->gc = gc;
}

bucket_t * lpc_mapping_t::get(lpc_value_t *k)
{
    int hash = calc_hash(k) % size;
    bucket_t *b = &members[hash];
    bucket_t *target = nullptr;
    while (b->next) {
        lpc_value_t &key = b->pair[0];
        lpc_value_t &val = b->pair[1];
        if (k->type == value_type::int_ || k->type == value_type::float_) {
            if (key.type == value_type::int_ || key.type == value_type::float_) {
                if (key.pval.number != k->pval.number) {
                    b = b->next;
                } else {
                    target = b;
                    break;
                }
            }
        } else if (k->type == value_type::string_ && key.type == value_type::string_) {
            lpc_string_t *str1 = reinterpret_cast<lpc_string_t *>(k->gcobj);
            lpc_string_t *str2 = reinterpret_cast<lpc_string_t *>(key.gcobj);
            if (str1->get_hash() != str2->get_hash()) {
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

lpc_value_t * lpc_mapping_t::get_value(lpc_value_t *k)
{
    bucket_t *b = get(k);
    return b ? &b->pair[1] : nullptr;
}

void lpc_mapping_t::set(lpc_value_t *k, lpc_value_t *v)
{
    int hash = calc_hash(k) % size;
    bucket_t *b = &members[hash];
    bucket_t *t = b;
    while (b->next) {
        b = b->next;
    }

    if (t == b) {
        b->pair = new lpc_value_t[2];
        b->pair[0] = *k;
        b->pair[1] = *v;
    } else {
        bucket_t *b1 = new bucket_t;
        b1->pair = new lpc_value_t[2];
        b1->pair[0] = *k;
        b1->pair[1] = *v;
        b->next = b1;
    }
}

void lpc_mapping_t::upset(lpc_value_t *k, lpc_value_t *v)
{
    bucket_t *found = get(k);
    if (found) {
        // TODO free or not
        if (!found->pair) {
            found->pair = new lpc_value_t[2];
        }
        
        found->pair[0] = *k;
        found->pair[1] = *v;
    } else {
        int hash = calc_hash(k) % this->size;
        bucket_t *b = &members[hash];
        bucket_t *t = b;
        while (b->next) {
            b = b->next;
        }

        if (t != b) {
            bucket_t *node = new bucket_t;
            node->pair[0] = *k;
            node->pair[1] = *v;
            b->next = node;
        } else {
            b->pair[0] = *k;
            b->pair[1] = *v;
        }
    }
}

void lpc_mapping_t::remove(lpc_value_t *k)
{
    bucket_t *found = get(k);
    if (!found) return;

    int hash = calc_hash(k) % this->size;
    bucket_t *b = &members[hash];
    bucket_t *slot = b;

    b = b->next;
    while (b) {
        bucket_t *t = b;
        b = b->next;
        delete t;
    }

    slot->next = nullptr;
    slot->pair[0].gcobj = nullptr;
    slot->pair[1].gcobj = nullptr;
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

