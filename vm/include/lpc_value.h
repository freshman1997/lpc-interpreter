#ifndef __LPC_VALUE__
#define __LPC_VALUE__
#include "lpc.h"
#include "type/lpc_array.h"
#include "type/lpc_string.h"
#include "type/lpc_mapping.h"
#include "type/lpc_object.h"
#include "type/lpc_closure.h"
#include "type/lpc_function.h"
#include "type/lpc_buffer.h"
#include "type/lpc_proto.h"

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
    class_,
    return_,
};

class lpc_string_t;
class lpc_array_t;
class lpc_mapping_t;
class lpc_function_t;
class lpc_closure_t;
class lpc_object_t;
class lpc_buffer_t;
class object_proto_t;

union lpc_gc_object_t 
{
    gc_header head;    // 每个 gc obj 都有这个头部

    lpc_string_t   str;
    lpc_array_t    arr;
    lpc_mapping_t  map;
    lpc_function_t fun;
    lpc_closure_t  clo;
    lpc_object_t   obj;
    lpc_buffer_t   buf;
    object_proto_t pro;
};

struct lpc_value_t
{
    value_type type;
    value_type subtype;

    union pure_val
    {
        float real;
        int number;
        unsigned char byte;
    } pval;
    
    lpc_gc_object_t *gcobj;
};

#endif
