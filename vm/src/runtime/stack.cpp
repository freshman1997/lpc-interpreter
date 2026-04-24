#include "lpc_value.h"
#include "runtime/stack.h"
#include "runtime/vm.h"

lpc_stack_t::lpc_stack_t(lint32_t sz, lpc_vm_t *_vm)
{
    vm = _vm;
    lpc_value_t *st = new lpc_value_t[sz];
    this->stack = st;
    this->size = sz;
    this->idx = 0;
}

bool lpc_stack_t::push(lpc_value_t *val)
{
    if (!val) return false;
    if (idx >= size) {
        if (vm) vm->stack_overflow();
        return false;
    }
    stack[idx] = *val;
    ++idx;
    return true;
}

bool lpc_stack_t::push_value(lpc_value_t val)
{
    if (idx >= size) {
        if (vm) vm->stack_overflow();
        return false;
    }
    stack[idx] = val;
    ++idx;
    return true;
}

lint32_t lpc_stack_t::frame_floor_idx() const
{
    if (!vm) {
        return 0;
    }
    call_info_t *ci = vm->get_call_info();
    if (!ci || !ci->base || !stack) {
        return 0;
    }

    lpc_value_t *begin = stack;
    lpc_value_t *end = stack + size;
    if (ci->base < begin || ci->base >= end) {
        return 0;
    }
    return static_cast<lint32_t>(ci->base - begin);
}

lpc_value_t * lpc_stack_t::get(lint32_t idx)
{
    if (idx < 0 || idx >= size) return nullptr;
    return &stack[idx];
}

lpc_value_t * lpc_stack_t::top()
{
    return this->get(idx == 0 ? idx : idx - 1);
}

lpc_value_t * lpc_stack_t::pop()
{
    const lint32_t floor = frame_floor_idx();
    if (idx <= floor) return nullptr;
    --idx;
    lpc_value_t *val = &stack[idx];
    return val;
}

bool lpc_stack_t::pop_n(lint32_t n)
{   
    if (n < 0) return false;
    const lint32_t floor = frame_floor_idx();
    if (idx - n < floor) return false;
    idx -= n;
    return true;
}
void lpc_stack_t::set_local_size(lint32_t n)
{
    if (n < 0) {
        return;
    }
    if (idx + n > size) {
        if (vm) vm->stack_overflow();
        return;
    }
    for (lint32_t i = idx; i < idx + n; ++i) {
        stack[i].set_undefined();
    }

    idx += n;
}
