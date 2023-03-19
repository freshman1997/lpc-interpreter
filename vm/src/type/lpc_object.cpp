#include "type/lpc_object.h"
#include "lpc_value.h"

lpc_value_t * lpc_object_t::copy()
{
    return nullptr;
}

object_proto_t * lpc_object_t::get_proto()
{
    return this->proto;
}

void lpc_object_t::set_proto(object_proto_t *proto)
{
    this->proto = proto;
    if (proto->nvariable > 0) {
        this->locals = new lpc_value_t[proto->nvariable];
    }
}

const char * lpc_object_t::get_pc()
{
    return this->proto->instructions;
}

lpc_object_t::~lpc_object_t()
{
    if (this->locals) delete[] this->locals;
}
