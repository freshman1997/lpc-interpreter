#include <memory>
#include <cstdlib>
#include "gc/mark_sweep.h"

void mark_sweep_gc::mark()
{

}

void mark_sweep_gc::mark_phase()
{

}

void mark_sweep_gc::sweep_phase()
{

}

void mark_sweep_gc::collect_leisure_sapce()
{

}

void mark_sweep_gc::collect()
{

}

void * mark_sweep_gc::allocate(luint32_t sz)
{
    // TODO
    
    void *p = malloc(sz);
    if (!p) {
        // TODO
    }

    return p;
}
