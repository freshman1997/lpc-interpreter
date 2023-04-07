#include "type/lpc_closure.h"
#include "lpc_value.h"

void lpc_closure_t::init()
{
    upvalues = nullptr;
    if (proto->nupvalue) {
        upvalues = new lpc_value_t[proto->nupvalue];
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
}

void lpc_closure_t::dtor()
{
    if (upvalues) delete [] upvalues;
}
