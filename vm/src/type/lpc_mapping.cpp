#include <cstring>
#include "lpc_value.h"
#include "memory/memory.h"
#include "type/lpc_string.h"
#include "type/lpc_array.h"
#include "type/lpc_mapping.h"
#include "runtime/vm.h"

extern int hash_(const char *str);
extern int hash_pointer(int x);

int lpc_mapping_t::calc_hash(lpc_value_t *val)
{
    if (val->is_string()) {
        lpc_string_t *str = reinterpret_cast<lpc_string_t *>(val->get_gcobj());
        return str->get_hash();
    } else if (val->is_float()) {
        return hash_pointer((int)val->get_float());
    } else {
        return hash_pointer(val->get_int());
    }
}

lpc_mapping_t * lpc_mapping_t::copy()
{
    lpc_mapping_t *newMap = alloc->allocate_mapping();

    for (luint32_t i = 0; i < used; ++i) {
        bucket_t *buck = iterate(i);
        newMap->upset(&buck->pair[0], &buck->pair[1], OpCode::op_load_global);
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
        if (k->is_int() && key.is_int()) {
            if (key.get_int() != k->get_int()) {
                b = b->next;
            } else {
                target = b;
                break;
            }
        } else if (k->is_number() && key.is_number()) {
            float kv = k->is_float() ? k->get_float() : (float)k->get_int();
            float kk = key.is_float() ? key.get_float() : (float)key.get_int();
            if (kk != kv) {
                b = b->next;
            } else {
                target = b;
                break;
            }
        } else if (k->is_string() && key.is_string()) {
            lpc_string_t *str1 = reinterpret_cast<lpc_string_t *>(k->get_gcobj());
            lpc_string_t *str2 = reinterpret_cast<lpc_string_t *>(key.get_gcobj());
            if (str1->get_hash() != str2->get_hash() || strcmp(str1->get_str(), str2->get_str()) != 0) {
                b = b->next;
            } else {
                target = b;
                break;
            }
        } else {
            b = b->next;
        }
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

    if (alloc && alloc->get_vm()) {
        if ((k && k->is_gc_type() && k->get_gcobj()) || (v && v->is_gc_type() && v->get_gcobj())) {
            lpc_value_t vv = v ? *v : lpc_value_t::make_null();
            alloc->get_vm()->gc_write_barrier(reinterpret_cast<lpc_gc_object_t *>(this), &vv);
        }
    }
}

bool lpc_mapping_t::upset(lpc_value_t *k, lpc_value_t *v, OpCode op)
{
    bucket_t *found = get(k);
    lpc_value_t *val = nullptr;
    if (found) {
        found->pair[0] = *k;
        val = &found->pair[1];
    } else {
        int hash = calc_hash(k) % this->size;
        bucket_t *b = &members[hash];
        bucket_t *t = b;
        while (b->next) {
            b = b->next;
        }

        if (t != b) {
            bucket_t *node = this->alloc->allocate<bucket_t, true>(1);
            node->pair = this->alloc->allocate<lpc_value_t, true>(2);
            node->pair[0].set_null();
            node->pair[1].set_null();
            node->pair[0] = *k;
            val = &node->pair[1];
            b->next = node;
            ++used;
        } else {
            if (!b->pair) {
                b->pair = this->alloc->allocate<lpc_value_t, true>(2);
                b->pair[0].set_null();
                b->pair[1].set_null();
                ++fill;
                ++used;
            }
            b->pair[0] = *k;
            val = &b->pair[1];
        }
    }

    switch (op)
    {
    case OpCode::op_load_global: {
        *val = *v;
        break;
    }
    case OpCode::op_add: {
        if (!arith_binop(val, v, ArithBinOp::Add)) return false;
        break;
    }
    case OpCode::op_sub: {
        if (!arith_binop(val, v, ArithBinOp::Sub)) return false;
        break;
    }
    case OpCode::op_mul: {
        if (!arith_binop(val, v, ArithBinOp::Mul)) return false;
        break;
    }
    case OpCode::op_div: {
        if (!arith_binop(val, v, ArithBinOp::Div)) return false;
        break;
    }
    case OpCode::op_mod: {
        if (val->is_int()) {
            val->set_int(val->get_int() % v->get_int());
        } else if (val->is_null()) {
            if (v->is_int() || v->is_float()) {
                *val = *v;
            } else {
                return false;
            }
        } else {
            return false;
        }
        break;
    }
    case OpCode::op_inc: {
        if (val->is_int()) {
            val->set_int(val->get_int() + 1);
        } else if (val->is_float()) {
            val->set_float(val->get_float() + 1);
        } else if (val->is_null()) {
            val->set_int(1);
        }
        break;
    }
    case OpCode::op_dec: {
        if (val->is_int()) {
            val->set_int(val->get_int() - 1);
        } else if (val->is_float()) {
            val->set_float(val->get_float() - 1);
        } else if (val->is_null()) {
            val->set_int(-1);
        }
        break;
    }
    case OpCode::op_minus: {
        if (val->is_int()) {
            val->set_int(-val->get_int());
        } else if (val->is_float()) {
            val->set_float(-val->get_float());
        } else if (val->is_null()) {
            val->set_int(0);
        }
        break;
    }
    default:
        return false;
        break;
    }
        
    if (fill * 1.0 / size >= 0.70) {
        grow();
    }

    if (alloc && alloc->get_vm() && val && val->is_gc_type() && val->get_gcobj()) {
        alloc->get_vm()->gc_write_barrier(reinterpret_cast<lpc_gc_object_t *>(this), val);
    }

    return true;
}

void lpc_mapping_t::remove(lpc_value_t *k)
{
    int hash = calc_hash(k) % this->size;
    bucket_t *b = &members[hash];

    if (!b->pair) return;

    bucket_t *prev = nullptr;
    bucket_t *cur = b;

    while (cur) {
        lpc_value_t &key = cur->pair[0];
        bool match = false;
        if (k->is_number() && key.is_number()) {
            if (k->is_int() && key.is_int()) {
                match = (key.get_int() == k->get_int());
            } else {
                float kv = k->is_float() ? k->get_float() : (float)k->get_int();
                float kk = key.is_float() ? key.get_float() : (float)key.get_int();
                match = (kk == kv);
            }
        } else if (k->is_string() && key.is_string()) {
            lpc_string_t *str1 = reinterpret_cast<lpc_string_t *>(k->get_gcobj());
            lpc_string_t *str2 = reinterpret_cast<lpc_string_t *>(key.get_gcobj());
            match = (str1->get_hash() == str2->get_hash() && strcmp(str1->get_str(), str2->get_str()) == 0);
        }

        if (match) {
            if (prev) {
                prev->next = cur->next;
                if (cur->pair) {
                    alloc->release(sizeof(lpc_value_t) * 2);
                    free(cur->pair);
                }
                alloc->release(sizeof(bucket_t));
                free(cur);
                --used;
            } else {
                if (cur->next) {
                    bucket_t *t = cur->next;
                    cur->pair[0] = t->pair[0];
                    cur->pair[1] = t->pair[1];
                    cur->next = t->next;
                    if (t->pair) {
                        alloc->release(sizeof(lpc_value_t) * 2);
                        free(t->pair);
                    }
                    alloc->release(sizeof(bucket_t));
                    free(t);
                    --used;
                } else {
                    cur->pair[0].set_null();
                    cur->pair[1].set_null();
                    --fill;
                    --used;
                }
            }
            return;
        }

        prev = cur;
        cur = cur->next;
    }
}

void lpc_mapping_t::place(bucket_t *newBuckets, int newSize, bucket_t *buck, bool reuse)
{
    lpc_value_t &k = buck->pair[0];
    lpc_value_t &v = buck->pair[1];
    int hash = calc_hash(&k) % newSize;
    
    if (newBuckets[hash].pair) {
        bucket_t *cur = &newBuckets[hash];
        while (cur->next) {
            cur = cur->next;
        }

        buck->next = nullptr;
        if (reuse) {
            cur->next = buck;
        } else {
            bucket_t *b = this->alloc->allocate<bucket_t, true>(1);
            b->pair = buck->pair;
            cur->next = b;
        }
    } else {
        buck->next = nullptr;
        newBuckets[hash] = *buck;
        ++fill;
    }

    ++used;
}

void lpc_mapping_t::grow()
{
    int newSize = size << 1;
    bucket_t *newBuckets = this->alloc->allocate<bucket_t, true>(newSize);

    this->fill = 0;
    this->used = 0;

    for (int i = 0; i < size; ++i) {
        if (members[i].pair) {
            bucket_t *cur = members[i].next;
            place(newBuckets, newSize, &members[i]);

            while (cur) {
                bucket_t *t = cur->next;
                place(newBuckets, newSize, cur, true);
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

    ++idx;
    for (; idx < size; ++idx) {
        if (members[idx].pair) {
            cur = &members[idx];
            break;
        }
    }

    return cur;
}

void lpc_mapping_t::dtor(lint64_t &freeBytes)
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
