#ifndef __LPC_OBJECT__
#define __LPC_OBJECT__

struct lpc_value_t;
class Object
{
public:
    lpc_value_t * copy();

private:
    lpc_value_t *constants;
    lpc_value_t *fields;
    lpc_value_t *functions;
};

#endif