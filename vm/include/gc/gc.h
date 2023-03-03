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
private:
    int large_object_threshod = 2700;
    lpc_allocator_t *allocator;
    lpc_vm_t *vm;

    mark_sweep_gc *msg;

};

#endif
