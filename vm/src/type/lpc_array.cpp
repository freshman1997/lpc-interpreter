#include <unordered_map>

#include "lpc_value.h"
#include "type/lpc_array.h"
#include "memory/memory.h"

lpc_array_t::lpc_array_t(luint32_t sz, lpc_value_t *m) : size(sz), members(m){}

lpc_value_t * lpc_array_t::get(luint32_t i)
{
    return &this->members[i];
}

void lpc_array_t::set(lpc_value_t *val, luint32_t i)
{
    this->members[i] = *val;
}

#define oper(val, v, op) \
    if (val->type == value_type::int_) { \
        val->pval.number op v->type == value_type::int_ ? v->pval.number : v->pval.real; \
    } else if (val->type == value_type::float_) { \
        val->pval.real op v->type == value_type::int_ ? v->pval.number : v->pval.real; \
    } else if (val->type == value_type::null_) { \
        if (v->type == value_type::int_ || v->type == value_type::float_) { \
            *val = *v; \
        } else { \
            return false; \
        } \
    } else { \
        return false; \
    }

bool lpc_array_t::upset(lpc_value_t *v, luint32_t i, OpCode op)
{
    if (i >= this->size) {
        return false;
    }
    
    if (v->type != value_type::int_ && v->type != value_type::float_) {
        return false;
    }

    lpc_value_t *val = &this->members[i];

    switch (op)
    {
    case OpCode::op_load_global: {
        *val = *v;
        break;
    }
    case OpCode::op_add: {
        oper(val, v, +=)
        break;
    }
    case OpCode::op_sub: {
        oper(val, v, -=)
        break;
    }
    case OpCode::op_mul: {
        oper(val, v, *=)
        break;
    }
    case OpCode::op_div: {
        oper(val, v, /=)
        break;
    }
    case OpCode::op_mod: {
        if (val->type == value_type::int_) {
            val->pval.number %= v->pval.number;
        } else if (val->type == value_type::null_) {
            if (v->type == value_type::int_ || v->type == value_type::float_) {
                *val = *v;
            } else {
                return false;
            }
        } else {
            return false;
        }
        break;
    }
    default:
        return false;
        break;
    }

    return true;
}

luint32_t lpc_array_t::get_size() const
{
    return size;
}

lpc_array_t * lpc_array_t::copy(lpc_allocator_t *alloc)
{
    lpc_array_t *newArray = alloc->allocate_array(size);
    for (luint32_t i = 0; i < size; ++i) {
        newArray->members[i] = members[i];
    }
    return newArray;
}

// array op
lpc_array_t * array_add(lpc_array_t *l, lpc_array_t *r, lpc_allocator_t *alloc)
{
    luint32_t newSize = l->get_size() + r->get_size();
    lpc_array_t *newArray = alloc->allocate_array(newSize);
    // copy
    luint32_t i = 0, j;
    for (; i < l->get_size(); ++i) {
        newArray->set(l->get(i), i);
    }

    for (j = 0; j < r->get_size(); ++i, ++j) {
        newArray->set(r->get(j), i);
    }
    
    return newArray;
}

lpc_array_t * array_sub(lpc_array_t *l, lpc_array_t *r, lpc_allocator_t *alloc)
{

    luint32_t i = 0, c = 0;
    std::unordered_map<luint32_t, bool> skips;

    for (; i < r->get_size(); ++i) {
        lint32_t t = 0;
        for (luint32_t j = 0; j < l->get_size(); ++j) {
            if (r->get(i)->type == value_type::int_ && l->get(j)->type == value_type::int_ && l->get(j)->pval.number == r->get(i)->pval.number) {
                skips[j] = 1;
            }
        }
    }

    // fixme size == 0
    lpc_array_t *newArray = alloc->allocate_array(l->get_size() - skips.size());
    for (i = 0; i < l->get_size(); ++i) {
        if (skips.count(i)) {
            continue;
        }
        
        newArray->set(l->get(i), c++);
    }

    return newArray;
}