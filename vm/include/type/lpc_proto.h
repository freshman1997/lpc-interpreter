#ifndef __LPC_PROTO__
#define __LPC_PROTO__
#include <unordered_map>
#include <vector>
#include "lpc.h"
#include "type/lpc_string.h"

class variable_proto_t
{
public:
    const char *name = nullptr;
    lint32_t offset;
};

enum upvalue_source_kind : lint16_t
{
    upvalue_from_parent_local = 0,
    upvalue_from_parent_upvalue = 1,
};

class function_proto_t
{
public:
    const char *name;
    bool is_static;
    lint8_t retType;
    lint32_t nargs = 0;
    lint32_t nlocal = 0;
    lint32_t nupvalue = 0;
    lint32_t offset;
    lint32_t fromPC = 0;                // 在当前对象指令的位置
    lint32_t toPC = 0;
    lint16_t *upvalue_source_kind = nullptr;
    lint16_t *upvalue_source_index = nullptr;
    variable_proto_t *vprotos = nullptr;
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

class object_proto_t
{
public:
    gc_header header;

public:
    const char *name;
    const char *instructions;
    lint32_t instruction_size;

    void **inherits;
    lint16_t *inherit_offsets;
    lint32_t ninherit;

    lint16_t create_idx;
    lint16_t on_load_in_idx;
    lint16_t on_destruct_idx;

    const char *init_codes;
    lint32_t ninit;
    function_proto_t *init_fun;

    variable_proto_t *variable_table;

    constant_proto_t *iconst;
    lint32_t niconst;

    constant_proto_t *fconst;
    lint32_t nfconst;
    
    constant_proto_t *sconst;
    lint32_t nsconst;

    class_proto_t *class_table;
    function_proto_t *func_table;
    lint32_t nvariable = 0;
    bool *loc_tags;
    lint32_t nfunction = 0;
    lint32_t nclass = 0;
    lint32_t nconst = 0;

    lint32_t nswitch = 0;
    std::vector<std::unordered_map<lint32_t, lint32_t>> *lookup_table;
    std::unordered_map<lint32_t, lint32_t> *defaults;

    variable_proto_t *gvprotos = nullptr;
    
    std::vector<std::pair<luint32_t, luint32_t>> *initLineMap = nullptr;
    std::vector<std::pair<luint32_t, luint32_t>> *lineMap = nullptr;
};

#endif
