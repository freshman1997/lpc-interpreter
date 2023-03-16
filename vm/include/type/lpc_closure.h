#ifndef __LPC_CLOSURE_H__
#define __LPC_CLOSURE_H__
#include "lpc_value.h"

class lpc_closure_t
{
public:
    lpc_gc_object_header_t header;

public:
    lpc_value_t * copy();


private:
    
};

#endif
