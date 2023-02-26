#ifndef __LPC_MAPPING_H__
#define __LPC_MAPPING_H__

struct lpc_value_t;

class LpcArray
{

private:
    int size = 0;
    lpc_value_t *members;
};

#endif