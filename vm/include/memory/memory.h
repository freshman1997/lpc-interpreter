#ifndef __MEMORY_H__
#define __MEMORY_H__

struct lpc_value_t;
class lpc_gc_t;
class lpc_vm_t;
class lpc_array_t;
class lpc_mapping_t;

class lpc_allocator_t
{
    friend lpc_gc_t;
public:
    lpc_array_t * allocate_array(int size);
    lpc_mapping_t * allocate_mapping();
    lpc_value_t * allocate_object();
    lpc_value_t * allocate_closure();
    lpc_value_t * allocate_string();
    lpc_value_t * allocate_buffer(int size);
    lpc_value_t * allocate_function();

private:
    void * allocate(int size);
    void * pick_chunk(int size);
    lpc_vm_t *vm;
};

#endif
