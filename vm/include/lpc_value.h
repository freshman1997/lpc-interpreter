#ifndef __LPC_VALUE__
#define __LPC_VALUE__

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
};

class lpc_string_t;
class lpc_array_t;
class lpc_mapping_t;
class lpc_function_t;
class lpc_closure_t;
class lpc_object_t;
class lpc_buffer_t;

struct lpc_value_t
{
    value_type type;

    union value 
    {
        float real;
        int number;
        unsigned char byte;

        lpc_string_t   *str;
        lpc_array_t    *arr;
        lpc_mapping_t  *map;
        lpc_function_t *fun;
        lpc_closure_t  *clo;
        lpc_object_t   *obj;
        lpc_buffer_t   *buf;
    };

    value val;
};

#endif
