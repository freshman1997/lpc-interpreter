#ifndef __LPC_FUNCTION__
#define __LPC_FUNCTION__

#include "lpc_value.h"
struct lpc_value_t;
class LpcObject;
class lpc_function_t
{
public:
    lpc_value_t * copy();

private:
    LpcObject *self = nullptr;
    lpc_value_t *fields;
    int pc = 0;
};

#endif
