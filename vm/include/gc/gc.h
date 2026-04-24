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
    void * allocate(luint32_t sz, bool check = true) 
    {
        return msg->allocate(NULL, sz, check);
    }

    void * allocate(void *p, luint32_t sz, bool check) 
    {
        return msg->allocate(p, sz, check);
    }

    lpc_gc_t(lpc_vm_t *vm) 
    {
        msg = new mark_sweep_gc();
        msg->set_vm(vm);
    }

    void link(lpc_gc_object_t *gcobj, value_type type) 
    {
        msg->link(gcobj, type);
    }

    void release(luint32_t sz) 
    {
        msg->release(sz);
    }

    void set_memory_limit_bytes(luint64_t bytes)
    {
        msg->set_memory_limit_bytes(bytes);
    }

    luint64_t memory_limit_bytes() const
    {
        return msg->memory_limit_bytes();
    }

    luint64_t allocated_bytes() const
    {
        return msg->allocated_bytes();
    }

    luint64_t gc_collect_count() const
    {
        return msg->collect_count();
    }

    luint64_t gc_last_freed_bytes() const
    {
        return msg->last_freed_bytes();
    }

    luint64_t gc_total_freed_bytes() const
    {
        return msg->total_freed_bytes();
    }

    luint64_t gc_last_collected_objects() const
    {
        return msg->last_collected_objects();
    }

    luint64_t gc_total_collected_objects() const
    {
        return msg->total_collected_objects();
    }

    void write_barrier(lpc_gc_object_t *container, const lpc_value_t *value)
    {
        msg->write_barrier(container, value);
    }

    luint64_t gc_write_barrier_count() const
    {
        return msg->write_barrier_count();
    }

    luint64_t gc_minor_collect_count() const
    {
        return msg->minor_collect_count();
    }

    luint64_t gc_nursery_bytes() const
    {
        return msg->nursery_bytes();
    }

    luint64_t gc_nursery_limit_bytes() const
    {
        return msg->nursery_limit_bytes();
    }

    luint64_t gc_remembered_set_size() const
    {
        return msg->remembered_set_size();
    }

    void set_nursery_limit_bytes(luint64_t bytes)
    {
        msg->set_nursery_limit_bytes(bytes);
    }

    luint64_t gc_major_collect_count() const
    {
        return msg->major_collect_count();
    }

    luint64_t gc_minor_last_freed_bytes() const
    {
        return msg->minor_last_freed_bytes();
    }

    luint64_t gc_minor_total_freed_bytes() const
    {
        return msg->minor_total_freed_bytes();
    }

    luint64_t gc_major_last_freed_bytes() const
    {
        return msg->major_last_freed_bytes();
    }

    luint64_t gc_major_total_freed_bytes() const
    {
        return msg->major_total_freed_bytes();
    }

    luint64_t gc_minor_last_elapsed_us() const
    {
        return msg->minor_last_elapsed_us();
    }

    luint64_t gc_minor_total_elapsed_us() const
    {
        return msg->minor_total_elapsed_us();
    }

    luint64_t gc_major_last_elapsed_us() const
    {
        return msg->major_last_elapsed_us();
    }

    luint64_t gc_major_total_elapsed_us() const
    {
        return msg->major_total_elapsed_us();
    }

private:
    mark_sweep_gc *msg;
};

#endif
