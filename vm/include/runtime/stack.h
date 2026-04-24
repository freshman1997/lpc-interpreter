#ifndef __RUNTIME_STACK_H__
#define __RUNTIME_STACK_H__
#include "lpc.h"

struct lpc_value_t;
class lpc_vm_t;

class lpc_stack_t
{
public:
    lpc_stack_t(lint32_t sz, lpc_vm_t *);
    bool push(lpc_value_t *);
    bool push_value(lpc_value_t val);
    lpc_value_t * get(lint32_t idx);
    int get_idx() 
    {
        return idx;
    }
    lpc_value_t * top();
    lpc_value_t * pop();
    bool pop_n(lint32_t n);
    lint32_t frame_floor_idx() const;
    
    void set_local_size(lint32_t n);

private:
    lpc_value_t *stack;
    lpc_vm_t *vm;
    lint32_t idx;
    lint32_t size;
};

#endif

