#include "gc/gc.h"
#include "gc/mark_sweep.h"

void lpc_gc_t::gc()
{
    msg->check_threshold();
}


