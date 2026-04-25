#ifndef __LPC_CLOSURE_H__
#define __LPC_CLOSURE_H__
#include "lpc.h"
#include "type/lpc_function.h"

struct lpc_value_t;
class lpc_allocator_t;

class lpc_closure_t : public lpc_function_t
{
    
public:
    lpc_value_t * copy();
    void init(lpc_allocator_t *alloc);
    lpc_value_t * get(int i);
    void set(int i, lpc_value_t *);
    void dtor(lpc_allocator_t *alloc);
private:
    lpc_value_t *upvalues;
    lpc_allocator_t *alloc_ = nullptr;
};

#endif
