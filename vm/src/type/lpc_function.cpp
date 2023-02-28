#include "lpc_value.h"
#include "type/lpc_function.h"

lpc_function_t *lpc_function_t::operator=(lpc_function_t *)
{
    return nullptr;
}

lpc_value_t * lpc_function_t::copy()
{
    return nullptr;
}

lpc_function_detail * lpc_function_t::get_decl()
{
    return &this->decl;
}

lpc_function_component * lpc_function_t::get_component()
{
    return &this->f;
}
