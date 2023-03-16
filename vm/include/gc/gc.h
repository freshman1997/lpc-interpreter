#ifndef __GC_H__
#define __GC_H__
#include "mark_sweep.h"

struct lpc_value_t;
class lpc_allocator_t;
class lpc_vm_t;

class lpc_gc_t
{
public:
    void gc();
    void * allocate(luint32_t sz) 
    {
        return msg->allocate(sz);
    }

    lpc_gc_t() 
    {
        msg = new mark_sweep_gc();
    }

    void link(lpc_gc_object_t *gcobj, value_type type) 
    {
        msg->link(gcobj, type);
    }

private:
    mark_sweep_gc *msg;
};

#endif
