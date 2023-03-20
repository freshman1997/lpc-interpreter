#ifndef __LPC_PROTO__
#define __LPC_PROTO__
#include <unordered_map>
#include <vector>
#include "lpc.h"
#include "type/lpc_string.h"

enum class variasble_type
{
    none_,
    lint32_t_,
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
    lint32_t nargs = 0;
    lint32_t nlocal = 0;
    lint32_t nupvalue = 0;
    lint32_t offset;
    lint32_t fromPC = 0;                // 在当前对象指令的位置
    lint32_t toPC = 0;
};

class variable_proto_t
{
public:
    const char *name = nullptr;
    variasble_type type = variasble_type::none_;
    lint32_t offset;
};

class class_proto_t
{
public:
    lint32_t nfield;
    bool is_static;
    variable_proto_t *field_table;
};

union const_t
{
    lpc_string_t *str;
    lint32_t number;
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
    gc_header header;

public:
    const char *name;
    const char *instructions;
    lint32_t instruction_size;

    lint16_t create_idx;
    lint16_t on_load_in_idx;
    lint16_t on_destruct_idx;

    const char *init_codes;
    lint32_t ninit;
    function_proto_t *init_fun;

    variable_proto_t *variable_table;

    constant_proto_t *iconst;
    constant_proto_t *fconst;
    constant_proto_t *sconst;

    class_proto_t *class_table;
    function_proto_t *func_table;
    lint32_t nvariable = 0;
    bool *loc_tags;
    lint32_t nfunction = 0;
    lint32_t nclass = 0;
    lint32_t nconst = 0;

    lint32_t nswitch = 0;
    // 第几个：case：goto
    std::vector<std::unordered_map<lint32_t, lint32_t>> lookup_table;
    // 第几个：goto
    std::unordered_map<lint32_t, lint32_t> defaults;
};

#endif