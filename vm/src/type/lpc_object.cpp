#include <functional>
#include <string>
#include <cstring>

#include "type/lpc_object.h"
#include "lpc_value.h"
#include <memory/memory.h>

extern int hash_(const char *str);
extern int hash_pointer(int x);

lpc_object_t::lpc_object_t(lpc_allocator_t *alloc) : alloc(alloc) {}

lpc_object_t * lpc_object_t::copy()
{
    lpc_object_t *obj = alloc->allocate_object();
    std::string newName = std::string(name) + "#" + std::to_string(no + 1);
    char *p = alloc->allocate<char, false>(newName.size() + 1);
    strcpy(p, newName.c_str());
    obj->name = p;
    obj->hash = hash_(p);
    obj->no = no + 1;
    if (proto->nvariable > 0) {
        obj->locals = alloc->allocate<lpc_value_t, true>(proto->nvariable);
    }
    obj->proto = proto;
    return obj;
}

object_proto_t * lpc_object_t::get_proto()
{
    return this->proto;
}

void lpc_object_t::set_proto(object_proto_t *proto)
{
    this->proto = proto;
    if (proto->nvariable > 0) {
        this->locals = alloc->allocate<lpc_value_t, true>(proto->nvariable);
    }

    name = proto->name;
    hash = hash_(name);    
}

const char * lpc_object_t::get_pc()
{
    return this->proto->instructions;
}
