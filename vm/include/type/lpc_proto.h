#ifndef __LPC_PROTO__
#define __LPC_PROTO__
enum class variasble_type
{
    none_,
    int_,
    float_,
    string_,
    mapping_,
    array_,
    object_,
};

class function_proto_t
{
public:
    const char *name = nullptr;
    char *pc = nullptr;
    int param_num = 0;
    bool varargs = false;
    variasble_type returnType = variasble_type::none_;
};

class variable_proto_t
{
public:
    const char *name = nullptr;
    variasble_type type = variasble_type::none_;
};

class class_proto_t
{
public:
    int field_num;
    int size = 0;
    variable_proto_t *field_table;
};

union const_t
{
    const char *str;
    int number;
    float real;
};

class constant_t
{
public:
    variasble_type type = variasble_type::none_;
    const_t item;
};

struct op_line_map_t
{

};

class line_info_t
{
public:

};

class object_proto_t
{

private:
    variable_proto_t *variable_table;
    constant_t *constant_table;
    class_proto_t *class_table;
    function_proto_t *func_table;
};

#endif