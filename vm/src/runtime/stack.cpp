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
    if (idx >= size || !val) return false;
    stack[idx] = *val;
    ++idx;
    return true;
}

lpc_value_t * lpc_stack_t::get(lint32_t idx)
{
    if (idx >= size) return nullptr;
    return &stack[idx];
}

lpc_value_t * lpc_stack_t::top()
{
    return this->get(idx == 0 ? idx : idx - 1);
}

void lpc_stack_t::check_stack(lint32_t n)
{
    call_info_t *cur_ci = vm->get_call_info();
    lint32_t nparam = 0;
    if (cur_ci->father) {
        nparam = cur_ci->father->func_table[cur_ci->funcIdx].nlocal;
    } else {
        nparam = cur_ci->cur_obj->get_proto()->func_table[cur_ci->funcIdx].nlocal;
    }

    if (cur_ci->base + nparam > stack + idx - n) {
        vm->panic();
    }
}

lpc_value_t * lpc_stack_t::pop()
{
    check_stack(1);

    if (idx < 0) return nullptr;
    --idx;
    lpc_value_t *val = &stack[idx];
    return val;
}

bool lpc_stack_t::pop_n(lint32_t n)
{   
    if (idx - n < 0) return false;
    idx -= n;
    return true;
}
void lpc_stack_t::set_local_size(lint32_t n)
{
    for (lint32_t i = idx; i < idx + n; ++i) {
        stack[i].type = value_type::int_;
        stack[i].subtype = value_type::null_;
        stack[i].pval.number = 0;
    }

    idx += n;
}