#ifndef __LPC_FUNCTION__
#define __LPC_FUNCTION__
#include "lpc_array.h"
#include "lpc_proto.h"

class lpc_object_t;

enum class function_type
{
    normal_,
    external_,
    simulate_,
    local_,
};

struct lpc_function_local
{
    short index;
};

struct lpc_function_normal
{
    int offset = 0;
    int num_args = 0;
    int num_upvalues = 0;
    int num_local = 0;
    object_proto_t *proto = nullptr;
};

struct lpc_function_detail
{
    int ref = 0;
    function_type type = function_type::normal_;
    lpc_object_t *owner = nullptr;
};

union lpc_function_component
{
    lpc_function_local local;
    lpc_function_local efun;
    lpc_function_local sfun;
    lpc_function_normal func;
};

class lpc_function_t
{
    
public:
    lpc_function_t(lpc_function_t &) = delete;
    lpc_function_t *operator=(lpc_function_t *);
    lpc_value_t * copy();
    lpc_function_detail * get_decl();
    lpc_function_component * get_component();

    lpc_value_t *locals;

private:
    lpc_function_detail decl;
    lpc_function_component f;
};

#endif
