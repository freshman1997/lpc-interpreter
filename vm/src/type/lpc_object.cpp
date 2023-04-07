#include "type/lpc_object.h"
#include "lpc_value.h"

lpc_value_t * lpc_object_t::copy()
{
    return nullptr;
}

lpc_proto_t * lpc_object_t::get_proto()
{
    return this->proto;
}

void lpc_object_t::set_proto(lpc_proto_t *proto)
{
    this->proto = proto;
    if (proto->proto->nvariable > 0) {
        this->locals = new lpc_value_t[proto->proto->nvariable];
    }
}

const char * lpc_object_t::get_pc()
{
    return this->proto->proto->instructions;
}
