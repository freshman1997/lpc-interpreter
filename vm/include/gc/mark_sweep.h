#ifndef __GC_MARK_SWEEP_H__
#define __GC_MARK_SWEEP_H__
#include <list>

#include "lpc.h"
#include "lpc_value.h"

class mark_sweep_gc
{
public:
    void * allocate(void *, luint32_t sz);
    void collect();
    void link(lpc_gc_object_t *gcobj, value_type type);
    void set_vm(lpc_vm_t *vm)
    {
        this->vm = vm;
    }

    void check_threshold()
    {
        if (blocks >= gc_threshold) {
            collect();
        }
    }

private:
    void mark_phase();
    void mark(lpc_gc_object_t *);
    lpc_gc_object_t * mark_root();
    void mark_all(lpc_gc_object_t *);

    void sweep_phase();
    void free_object(lpc_gc_object_t *, lint32_t &);

    luint32_t total_objects = 0;
    luint64_t blocks = 0;
    luint64_t gc_threshold = 2048;
    lpc_vm_t *vm = nullptr;
    lpc_gc_object_t *root = nullptr;
};

#endif
