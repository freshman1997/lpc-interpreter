#ifndef __LPC_FUNCTION__
#define __LPC_FUNCTION__

struct lpc_value_t;
class LpcFunction
{
public:
    lpc_value_t * copy();

private:
    lpc_value_t *fields;
    int pc = 0;
};

#endif
