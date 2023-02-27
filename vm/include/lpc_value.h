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

class LpcString;
class LpcArray;
class LpcMapping;
class LpcFunction;
class LpcClosure;
class LpcObject;
class LpcBuffer;

struct lpc_value_t
{
    value_type type;

    union value 
    {
        float real;
        int number;
        unsigned char byte;

        LpcString   *str;
        LpcArray    *arr;
        LpcMapping  *map;
        LpcFunction *fun;
        LpcClosure  *clo;
        LpcObject   *obj;
        LpcBuffer   *buf;
    };

    value val;
};

#endif
