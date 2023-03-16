#ifndef __GC_MARK_SWEEP_H__
#define __GC_MARK_SWEEP_H__
#include <list>

#include "lpc.h"
#include "lpc_value.h"

class mark_sweep_gc
{
public:
    void * allocate(luint32_t sz);
    void collect();
    void link(lpc_gc_object_t *gcobj, value_type type);

private:
    void mark_phase();
    void mark(lpc_gc_object_t *);
    void sweep_phase();
    void collect_leisure_sapce();

    luint32_t total_objects = 0;
    luint64_t blocks = 0;
    luint64_t gc_threshold = 1024 * 1024;

    lpc_gc_object_t *root = nullptr;
};

#endif
