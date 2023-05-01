#include "lpc_value.h"
#include "memory/memory.h"
#include "type/lpc_string.h"
#include "type/lpc_array.h"
#include "type/lpc_mapping.h"
#include <stdlib.h>

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

lpc_mapping_t * lpc_mapping_t::copy()
{
    lpc_mapping_t *newMap = alloc->allocate_mapping();

    for (luint32_t i = 0; i < used; ++i) {
        bucket_t *buck = iterate(i);
        newMap->upset(&buck->pair[0], &buck->pair[1]);
    }

    reset_iterator();

    return newMap;
}

lpc_mapping_t::lpc_mapping_t(lpc_allocator_t *alloc)
{
    this->size = 10;
    this->fill = 0;
    this->used = 0;
    this->alloc = alloc;
    this->cur = nullptr;
    this->idx = 0;
    this->members = alloc->allocate<bucket_t, true>(size);
}

bucket_t * lpc_mapping_t::get(lpc_value_t *k)
{
    int hash = calc_hash(k) % size;
    bucket_t *b = &members[hash];
    if (!b->pair) {
        return nullptr;
    }

    bucket_t *target = nullptr;
    while (b) {
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

    if (t == b && !b->pair) {
        b->pair = this->alloc->allocate<lpc_value_t, true>(2);
        b->pair[0] = *k;
        b->pair[1] = *v;
        ++used;
        ++fill;
    } else {
        bucket_t *b1 = this->alloc->allocate<bucket_t, true>(1);
        b1->pair = this->alloc->allocate<lpc_value_t, true>(2);
        b1->pair[0] = *k;
        b1->pair[1] = *v;
        b->next = b1;
        ++used;
    }

    if (fill * 1.0 / size >= 0.75) {
        grow();
    }
}

void lpc_mapping_t::upset(lpc_value_t *k, lpc_value_t *v)
{
    bucket_t *found = get(k);
    if (found) {
        found->pair[0] = *k;
        found->pair[1] = *v;
        ++used;
        ++fill;
    } else {
        int hash = calc_hash(k) % this->size;
        bucket_t *b = &members[hash];
        bucket_t *t = b;
        while (b->next) {
            b = b->next;
        }

        if (t != b) {
            bucket_t *node = this->alloc->allocate<bucket_t>(1);
            node->pair[0] = *k;
            node->pair[1] = *v;
            b->next = node;
        } else {
            if (!b->pair) {
                b->pair = this->alloc->allocate<lpc_value_t, true>(2);
                ++fill;
            }
            b->pair[0] = *k;
            b->pair[1] = *v;
        }
        ++used;
    }
        
    if (fill * 1.0 / size >= 0.75) {
        grow();
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
        alloc->release(sizeof(bucket_t));
        if (t->pair) {
            alloc->release(sizeof(lpc_value_t) * 2);
            free(t->pair);
        }
        free(t);
        --used;
    }

    slot->next = nullptr;
    slot->pair[0].gcobj = nullptr;
    slot->pair[1].gcobj = nullptr;
    --fill;
    --used;
}

void lpc_mapping_t::place(bucket_t *newBuckets, int newSize, bucket_t *buck, bool reuse)
{
    lpc_value_t &k = buck->pair[0];
    lpc_value_t &v = buck->pair[1];
    int hash = calc_hash(&k) % newSize;
    if (!newBuckets[hash].pair) {
        bucket_t *cur = &newBuckets[hash];
        while (cur->next) {
            cur = cur->next;
        }

        if (reuse) {
            cur->next = buck;
        } else {
            bucket_t *b = this->alloc->allocate<bucket_t>(1);
            b->next = nullptr;
            b->pair = buck->pair;
        }
    } else {
        newBuckets[hash].pair = buck->pair;
    }
}

void lpc_mapping_t::grow()
{
    int newSize = size << 1;
    bucket_t *newBuckets = this->alloc->allocate<bucket_t, true>(newSize);

    // copy
    for (int i = 0; i < size; ++i) {
        if (members[i].pair) {
            place(newBuckets, newSize, &members[i]);

            bucket_t *cur = members[i].next;
            while (cur) {
                place(newBuckets, newSize, cur, true);
                bucket_t *t = cur->next;
                cur->next = nullptr;
                alloc->release(sizeof(bucket_t));
                free(cur);
                cur = t;
            }
        }
    }

    free(members);
    this->members = newBuckets;
    this->size = newSize;
}

bucket_t * lpc_mapping_t::get_members()
{
    return this->members;
}

luint32_t lpc_mapping_t::get_size()
{
    return this->used;
}

bucket_t * lpc_mapping_t::get_bucket(int i)
{
    if (i >= size) return nullptr;
    return members + i;
}

void lpc_mapping_t::reset_iterator()
{
    cur = nullptr;
    idx = 0;
}

bucket_t * lpc_mapping_t::iterate(int i)
{
    if (i >= used) {
        return nullptr;
    }

    if (!cur) {
        for (; idx < size; ++idx) {
            if (members[idx].pair) {
                cur = &members[idx];
                break;
            }
        }

        return cur;
    } 

    if (cur->next) {
        cur = cur->next;
        return cur;
    }

    for (; idx < size; ++idx) {
        if (cur != &members[idx] && members[idx].pair) {
            cur = &members[idx];
            break;
        }
    }

    return cur;
}

void lpc_mapping_t::dtor(lint32_t &freeBytes)
{
    for (int i = 0; i < size; ++i) {
        bucket_t *buck = &members[i];
        if (buck->pair) {
            freeBytes += sizeof(lpc_value_t) * 2;
        }

        if (buck->next) {
            bucket_t *t = buck->next;
            while (t) {
                freeBytes += sizeof(bucket_t);
                bucket_t *tmp = t;
                if (tmp->pair) {
                    freeBytes += sizeof(lpc_value_t) * 2;
                }
                t = t->next;
                free(tmp);
            }
        }
    }

    freeBytes += sizeof(bucket_t) * size;
    free(members);
}

lpc_array_t * mapping_values(lpc_mapping_t *m, lpc_allocator_t *alloc)
{
    lpc_array_t *arr = (lpc_array_t *)alloc->allocate_array(m->get_size());
    for (int i = 0; i < m->get_size(); ++i) {
        bucket_t *buck = m->iterate(i);
        *arr->get(i) = buck->pair[1];
    }
    m->reset_iterator();
    return arr;
}

lpc_array_t * mapping_keys(lpc_mapping_t *m, lpc_allocator_t *alloc)
{
    lpc_array_t *arr = (lpc_array_t *)alloc->allocate_array(m->get_size());
    for (int i = 0; i < m->get_size(); ++i) {
        bucket_t *buck = m->iterate(i);
        *arr->get(i) = buck->pair[0];
    }
    m->reset_iterator();
    return arr;
}

void map_delete(lpc_mapping_t *map, lpc_value_t *k)
{
    map->remove(k);
}

