#ifndef __LPC_ARRAY_H__
#define __LPC_ARRAY_H__
#include "lpc.h"
#include "opcode.h"

struct lpc_value_t;
class lpc_gc_t;

class lpc_array_t
{
public:
    gc_header header;

public:
    lpc_array_t(luint32_t sz, lpc_value_t *);
    lpc_value_t * get(int i);
    void set(lpc_value_t *val, lint32_t i);
    lpc_value_t * copy();
    luint32_t get_size() const;
    lpc_value_t * get_members()
    {
        return members;
    }
    
private:
    luint32_t size = 0;
    lpc_value_t *members;
};

lpc_array_t * array_add(lpc_array_t *l, lpc_array_t *r, lpc_gc_t *gc);
lpc_array_t * array_sub(lpc_array_t *l, lpc_array_t *r, lpc_gc_t *gc);


#endif