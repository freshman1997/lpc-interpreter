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

lint32_t lpc_stack_t::index_of(const lpc_value_t *ptr) const
{
    if (!ptr || !stack) {
        return -1;
    }
    const lpc_value_t *begin = stack;
    const lpc_value_t *end = stack + size;
    if (ptr < begin || ptr >= end) {
        return -1;
    }
    return static_cast<lint32_t>(ptr - begin);
}

lpc_value_t * lpc_stack_t::at_index(lint32_t index)
{
    return get(index);
}

bool lpc_stack_t::contains(const lpc_value_t *ptr) const
{
    return index_of(ptr) >= 0;
}

bool lpc_stack_t::valid_range(lint32_t from, lint32_t to) const
{
    if (from < 0 || to < from) {
        return false;
    }
    return from < size && to < size;
}

lint32_t lpc_stack_t::frame_floor_idx() const
{
    if (!vm) {
        return 0;
    }
    call_info_t *ci = vm->get_call_info();
    if (!ci || !stack) {
        return 0;
    }
    if (ci->base_index >= 0 && ci->base_index < size) {
        return ci->base_index;
    }

    lint32_t base_index = index_of(ci->base);
    if (base_index < 0) {
        return 0;
    }
    return base_index;
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
