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
    int nargs = 0;
    int nlocal = 0;
    int nupvalue = 0;
    bool varargs = false;
    int offset;
    int address = 0;                // 在当前对象指令的位置
};

class variable_proto_t
{
public:
    const char *name = nullptr;
    variasble_type type = variasble_type::none_;
    int offset;
};

class class_proto_t
{
public:
    int field_num;
    int size = 0;
    int offset;
    variable_proto_t *field_table;
};

union const_t
{
    const char *str;
    int number;
    float real;
};

class constant_proto_t
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
public:
    char *instructions;
    int instruction_size;
    variable_proto_t **variable_table;
    constant_proto_t **constant_table;
    class_proto_t **class_table;
    function_proto_t **func_table;
    int nvariable = 0;
    int nfunction = 0;
    int nclass = 0;
    int nconst = 0;
};

#endif