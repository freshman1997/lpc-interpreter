#ifndef __LPC_MAPPING_H__
#define __LPC_MAPPING_H__

struct lpc_value_t;

class LpcArray
{
public:
    lpc_value_t * get(int i);
    void set(lpc_value_t *val, int i);
    lpc_value_t * copy();

private:
    int size = 0;
    lpc_value_t *members;
};

lpc_value_t * array_add(lpc_value_t *l, lpc_value_t *r);
lpc_value_t * array_sub(lpc_value_t *l, lpc_value_t *r);

#endif