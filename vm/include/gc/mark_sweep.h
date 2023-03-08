#ifndef __GC_MARK_SWEEP_H__
#define __GC_MARK_SWEEP_H__
#include <list>
#include "lpc.h"

class mark_sweep_gc
{
public:
    void * allocate(luint32_t sz);
    void collect();

private:
    void mark_phase();
    void mark();
    void sweep_phase();
    void collect_leisure_sapce();

    luint32_t total_objects;

    std::list<void *> list;
};

#endif
