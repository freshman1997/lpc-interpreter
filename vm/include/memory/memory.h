﻿#ifndef __MEMORY_H__
#define __MEMORY_H__
#include "lpc.h"

struct lpc_value_t;
class lpc_gc_t;
class lpc_vm_t;
class lpc_array_t;
class lpc_mapping_t;
class lpc_object_t;
class lpc_string_t;
class object_proto_t;
class lpc_function_t;
struct function_proto_t;

class lpc_allocator_t
{
    friend lpc_gc_t;
public:
    lpc_array_t * allocate_array(luint32_t size);
    lpc_mapping_t * allocate_mapping();
    lpc_object_t * allocate_object();
    object_proto_t * allocate_object_proto();

    lpc_value_t * allocate_closure();
    lpc_string_t * allocate_string(const char *);
    lpc_value_t * allocate_buffer(luint32_t size);
    lpc_function_t * allocate_function(function_proto_t *, lpc_object_t *, lint16_t);

    lpc_allocator_t(lpc_vm_t * v) : vm(v) {}
    
private:
    lpc_vm_t *vm;
};

#endif
