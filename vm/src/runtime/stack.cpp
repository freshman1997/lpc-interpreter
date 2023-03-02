#include "lpc_value.h"
#include "runtime/stack.h"

lpc_stack_t::lpc_stack_t(int sz)
{
    lpc_value_t *st = new lpc_value_t[sz];
    this->stack = st;
    this->size = sz;
    this->idx = 0;
}

bool lpc_stack_t::push(lpc_value_t *val)
{
    if (idx >= size) return false;
    stack[idx] = *val;
    ++idx;
    return true;
}

lpc_value_t * lpc_stack_t::get(int idx)
{
    return &stack[idx];
}

lpc_value_t * lpc_stack_t::top()
{
    return this->get(0);
}

lpc_value_t * lpc_stack_t::pop()
{
    if (idx < 0) return nullptr;
    --idx;
    lpc_value_t *val = &stack[idx];
    return val;
}

bool lpc_stack_t::pop_n(int n)
{   
    if (idx - n < 0) return false;
    idx -= n;
    return true;
}

