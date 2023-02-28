#include "type/lpc_object.h"

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
}
