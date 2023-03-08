#include <cstring>
#include "type/lpc_string.h"
lpc_string_t::lpc_string_t(const char * _str)
{

}

lpc_string_t::lpc_string_t(lpc_string_t &l)
{

    //this->str = strcpy(l.get_str());
}

lpc_string_t * lpc_string_t::operator=(lpc_string_t &l)
{
    return nullptr;
}

const char * lpc_string_t::get_str()
{
    return this->str;
}

int lpc_string_t::get_hash()
{
    return hash;
}

unsigned char lpc_string_t::get(int i)
{
    return 0;
}

int lpc_string_t::get_size()
{
    return 0;
}

lpc_value_t * lpc_string_t::copy()
{
    return nullptr;
}
