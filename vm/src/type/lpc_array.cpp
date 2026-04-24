#include <unordered_map>

#include "lpc_value.h"
#include "type/lpc_array.h"
#include "memory/memory.h"
#include "runtime/vm.h"

lpc_array_t::lpc_array_t(luint32_t sz, lpc_value_t *m) : size(sz), members(m){}

lpc_value_t * lpc_array_t::get(luint32_t i)
{
    return &this->members[i];
}

void lpc_array_t::set(lpc_value_t *val, luint32_t i)
{
    this->members[i] = *val;
    if (val && val->is_gc_type() && val->get_gcobj()) {
        if (alloc && alloc->get_vm()) {
            alloc->get_vm()->gc_write_barrier(reinterpret_cast<lpc_gc_object_t *>(this), val);
        }
    }
}

bool lpc_array_t::upset(lpc_value_t *v, luint32_t i, OpCode op)
{
    if (i >= this->size) {
        return false;
    }
    
    if (v && !v->is_number()) {
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
            val->set_float(val->get_float() + 1.0f);
        }
        break;
    }
    case OpCode::op_dec: {
         if (val->is_int()) {
            val->set_int(val->get_int() - 1);
        } else if (val->is_float()) {
            val->set_float(val->get_float() - 1.0f);
        }
        break;
    }
    case OpCode::op_minus: {
        if (val->is_int()) {
            val->set_int(-val->get_int());
        } else if (val->is_float()) {
            val->set_float(-val->get_float());
        }
        break;
    }
    default:
        return false;
        break;
    }

    if (val && val->is_gc_type() && val->get_gcobj()) {
        if (alloc && alloc->get_vm()) {
            alloc->get_vm()->gc_write_barrier(reinterpret_cast<lpc_gc_object_t *>(this), val);
        }
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

lpc_array_t * array_add(lpc_array_t *l, lpc_array_t *r, lpc_allocator_t *alloc)
{
    luint32_t newSize = l->get_size() + r->get_size();
    lpc_array_t *newArray = alloc->allocate_array(newSize);
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
        for (luint32_t j = 0; j < l->get_size(); ++j) {
            if (r->get(i)->is_int() && l->get(j)->is_int() && l->get(j)->get_int() == r->get(i)->get_int()) {
                skips[j] = 1;
            }
        }
    }

    lpc_array_t *newArray = alloc->allocate_array(l->get_size() - skips.size());
    for (i = 0; i < l->get_size(); ++i) {
        if (skips.count(i)) {
            continue;
        }
        
        newArray->set(l->get(i), c++);
    }

    return newArray;
}
