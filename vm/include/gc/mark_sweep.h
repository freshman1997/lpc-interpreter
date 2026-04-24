#ifndef __GC_MARK_SWEEP_H__
#define __GC_MARK_SWEEP_H__
#include <vector>
#include <chrono>

#include "lpc.h"
#include "lpc_value.h"

class mark_sweep_gc
{
public:
    static constexpr luint64_t kHardMaxBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;

    void * allocate(void *, luint32_t sz, bool check = true);
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

    void set_memory_limit_bytes(luint64_t bytes)
    {
        if (bytes == 0) {
            return;
        }
        if (bytes > kHardMaxBytes) {
            bytes = kHardMaxBytes;
        }
        gc_threshold = bytes;
    }

    luint64_t memory_limit_bytes() const
    {
        return gc_threshold;
    }

    luint64_t allocated_bytes() const
    {
        return blocks;
    }

    luint64_t nursery_bytes() const
    {
        return nursery_bytes_;
    }

    luint64_t nursery_limit_bytes() const
    {
        return nursery_limit_;
    }

    void set_nursery_limit_bytes(luint64_t bytes)
    {
        nursery_limit_ = bytes > 0 ? bytes : nursery_limit_;
    }

    void write_barrier(lpc_gc_object_t *container, const lpc_value_t *value);

    luint64_t write_barrier_count() const
    {
        return write_barrier_count_;
    }

    luint64_t remembered_set_size() const
    {
        return remembered_set_size_;
    }

    luint64_t major_collect_count() const
    {
        return collect_count_;
    }

    luint64_t minor_last_freed_bytes() const
    {
        return minor_last_freed_bytes_;
    }

    luint64_t minor_total_freed_bytes() const
    {
        return minor_total_freed_bytes_;
    }

    luint64_t major_last_freed_bytes() const
    {
        return major_last_freed_bytes_;
    }

    luint64_t major_total_freed_bytes() const
    {
        return major_total_freed_bytes_;
    }

    luint64_t minor_last_collected_objects() const
    {
        return minor_last_collected_objects_;
    }

    luint64_t minor_total_collected_objects() const
    {
        return minor_total_collected_objects_;
    }

    luint64_t major_last_collected_objects() const
    {
        return major_last_collected_objects_;
    }

    luint64_t major_total_collected_objects() const
    {
        return major_total_collected_objects_;
    }

    luint64_t minor_last_elapsed_us() const
    {
        return minor_last_elapsed_us_;
    }

    luint64_t minor_total_elapsed_us() const
    {
        return minor_total_elapsed_us_;
    }

    luint64_t major_last_elapsed_us() const
    {
        return major_last_elapsed_us_;
    }

    luint64_t major_total_elapsed_us() const
    {
        return major_total_elapsed_us_;
    }

    luint64_t minor_collect_count() const
    {
        return minor_collect_count_;
    }

    void maybe_collect_minor()
    {
        if (nursery_bytes_ >= nursery_limit_) {
            collect_minor();
        }
    }

    void collect_minor()
    {
        const auto t0 = std::chrono::steady_clock::now();
        ++minor_collect_count_;
        minor_mark_phase();
        minor_sweep_phase();
        nursery_bytes_ = 0;
        remembered_set_size_ = static_cast<luint64_t>(remembered_set_.size());
        const auto t1 = std::chrono::steady_clock::now();
        const luint64_t us = static_cast<luint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
        minor_last_elapsed_us_ = us;
        minor_total_elapsed_us_ += us;
    }

    luint64_t collect_count() const
    {
        return collect_count_;
    }

    luint64_t last_freed_bytes() const
    {
        return last_freed_bytes_;
    }

    luint64_t total_freed_bytes() const
    {
        return total_freed_bytes_;
    }

    luint64_t last_collected_objects() const
    {
        return last_collected_objects_;
    }

    luint64_t total_collected_objects() const
    {
        return total_collected_objects_;
    }

    void release(luint32_t sz) 
    {
        if (static_cast<luint64_t>(sz) > blocks) {
            blocks = 0;
        } else {
            blocks -= sz;
        }
    }

private:
    void cleanup_remembered_set();
    void remove_from_remembered_set(lpc_gc_object_t *obj);
    void mark_phase();
    void mark(lpc_gc_object_t *);
    lpc_gc_object_t * mark_root();
    void mark_all(lpc_gc_object_t *);
    void mark_minor(lpc_gc_object_t *);
    void minor_mark_phase();
    void minor_sweep_phase();

    void sweep_phase();
    void free_object(lpc_gc_object_t *, lint64_t &);

    luint32_t total_objects = 0;
    luint64_t blocks = 0;
    luint64_t gc_threshold = kHardMaxBytes;
    luint64_t collect_count_ = 0;
    luint64_t last_freed_bytes_ = 0;
    luint64_t total_freed_bytes_ = 0;
    luint64_t last_collected_objects_ = 0;
    luint64_t total_collected_objects_ = 0;
    luint64_t write_barrier_count_ = 0;
    luint64_t nursery_bytes_ = 0;
    luint64_t nursery_limit_ = 64ULL * 1024ULL * 1024ULL;
    luint64_t remembered_set_size_ = 0;
    luint64_t minor_collect_count_ = 0;
    luint64_t minor_last_freed_bytes_ = 0;
    luint64_t minor_total_freed_bytes_ = 0;
    luint64_t major_last_freed_bytes_ = 0;
    luint64_t major_total_freed_bytes_ = 0;
    luint64_t minor_last_collected_objects_ = 0;
    luint64_t minor_total_collected_objects_ = 0;
    luint64_t major_last_collected_objects_ = 0;
    luint64_t major_total_collected_objects_ = 0;
    luint64_t minor_last_elapsed_us_ = 0;
    luint64_t minor_total_elapsed_us_ = 0;
    luint64_t major_last_elapsed_us_ = 0;
    luint64_t major_total_elapsed_us_ = 0;
    std::vector<lpc_gc_object_t *> remembered_set_;
    lpc_vm_t *vm = nullptr;
    lpc_gc_object_t *root = nullptr;
};

#endif
