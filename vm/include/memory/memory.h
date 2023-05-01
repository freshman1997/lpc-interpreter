#ifndef __MEMORY_H__
#define __MEMORY_H__
#include <memory>
#include "lpc.h"

struct lpc_value_t;
class lpc_gc_t;
class lpc_vm_t;
class lpc_array_t;
class lpc_mapping_t;
class lpc_object_t;
class lpc_string_t;
struct object_proto_t;
class lpc_function_t;
struct function_proto_t;
class lpc_buffer_t;
class lpc_closure_t;

class lpc_allocator_t
{
    friend lpc_gc_t;
public:
    lpc_array_t * allocate_array(luint32_t size);
    lpc_mapping_t * allocate_mapping();
    lpc_object_t * allocate_object();
    object_proto_t * allocate_object_proto();

    lpc_closure_t * allocate_closure(function_proto_t *funcProto, lpc_object_t *owner);
    lpc_string_t * allocate_string(const char *, bool newOne = false);
    lpc_buffer_t * allocate_buffer(luint32_t size);
    lpc_function_t * allocate_function(function_proto_t *, lpc_object_t *, lint16_t);

    void * allocate(luint32_t sz, bool check = true);
    void * allocate(void *p, luint32_t newSz);
    lpc_allocator_t(lpc_vm_t * v) : vm(v) {}

    void release(luint32_t sz);

    template<typename T, bool call = false>
    T * allocate(luint32_t sz) 
    {
        T *p = (T *)allocate(sz * sizeof(T), false);
        if (call) {
            for (int i = 0; i < sz; ++i) {
                new(p + i)(T);
            }
        }

        return p;
    }

    template<typename T>
    T * allocate(void *origin, luint32_t newSize) 
    {
        void *p = allocate(origin, newSize);
        return (T *)p;
    }

private:
    lpc_vm_t *vm;
};

#endif
