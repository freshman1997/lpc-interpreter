#ifndef __LPC_FUNCTION__
#define __LPC_FUNCTION__
#include "lpc.h"
#include "lpc_array.h"
#include "lpc_object.h"
class lpc_function_t
{
public:
    gc_header header;

public:
    int ref = 0;
    lpc_object_t *owner = nullptr;
    lint16_t idx = -1;
    function_proto_t *proto = nullptr;
};

#endif
