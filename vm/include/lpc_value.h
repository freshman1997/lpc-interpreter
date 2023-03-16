#ifndef __LPC_VALUE__
#define __LPC_VALUE__
#include "lpc.h"

enum class value_type
{
    null_,
    byte_,
    int_,
    bool_,
    float_,
    buffer_,
    string_,
    array_,
    mappig_,
    object_,
    function_,
    closure_,
    proto_,
};

class lpc_string_t;
class lpc_array_t;
class lpc_mapping_t;
class lpc_function_t;
class lpc_closure_t;
class lpc_object_t;
class lpc_buffer_t;
class object_proto_t;

union lpc_gc_object_t;

struct lpc_gc_object_header_t
{
    lpc_gc_object_t *next;
    bool marked;
    value_type type; // for gc using
};

union lpc_gc_object_t 
{
    lpc_gc_object_header_t head;    // 每个 gc obj 都有这个头部

    lpc_string_t   *str;
    lpc_array_t    *arr;
    lpc_mapping_t  *map;
    lpc_function_t *fun;
    lpc_closure_t  *clo;
    lpc_object_t   *obj;
    lpc_buffer_t   *buf;
    object_proto_t *pro;
};

struct lpc_value_t
{
    value_type type;
    union pure_val
    {
        float real;
        int number;
        unsigned char byte;
    } pval;
    
    lpc_gc_object_t *gcobj;
};

#endif
