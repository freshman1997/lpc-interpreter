#ifndef __LPC_PROTO__
#define __LPC_PROTO__
#include "lpc_value.h"

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
    const char *name;
    bool is_static;
    int nargs = 0;
    int nlocal = 0;
    int nupvalue = 0;
    int offset;
    int fromPC = 0;                // 在当前对象指令的位置
    int toPC = 0;
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
    int nfield;
    bool is_static;
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
    lpc_gc_object_header_t header; // if reload

public:
    const char *name;
    const char *instructions;
    int instruction_size;

    const char *var_init_codes;
    int init_code_size;

    variable_proto_t *variable_table;

    constant_proto_t *iconst;
    constant_proto_t *fconst;
    constant_proto_t *sconst;

    class_proto_t *class_table;
    function_proto_t *func_table;
    int nvariable = 0;
    bool *loc_tags;
    int nfunction = 0;
    int nclass = 0;
    int nconst = 0;
};

#endif