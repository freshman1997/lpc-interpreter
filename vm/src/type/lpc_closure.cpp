#include <cstdlib>
#include "type/lpc_closure.h"
#include "lpc_value.h"
#include "memory/memory.h"
#include "runtime/vm.h"

void lpc_closure_t::init(lpc_allocator_t *alloc)
{
    alloc_ = alloc;
    upvalues = nullptr;
    if (proto->nupvalue) {
        luint32_t sz = proto->nupvalue;
        upvalues = (lpc_value_t *)alloc->allocate(sizeof(lpc_value_t) * sz, false);
        for (luint32_t i = 0; i < sz; ++i) {
            upvalues[i].set_undefined();
        }
    }
}

lpc_value_t * lpc_closure_t::get(int i)
{
    if (proto->nupvalue <= i || i < 0) {
        return nullptr;
    }

    return upvalues + i;
}

void lpc_closure_t::set(int i, lpc_value_t *v)
{
    if (proto->nupvalue <= i || i < 0) {
        return;
    }

    *(upvalues + i) = *v;
    if (alloc_ && alloc_->get_vm() && v && v->is_gc_type() && v->get_gcobj()) {
        alloc_->get_vm()->gc_write_barrier(reinterpret_cast<lpc_gc_object_t *>(this), v);
    }
}

void lpc_closure_t::dtor(lpc_allocator_t *alloc)
{
    if (upvalues) {
        alloc->release(sizeof(lpc_value_t) * proto->nupvalue);
        free(upvalues);
        upvalues = nullptr;
    }
    alloc_ = nullptr;
}
