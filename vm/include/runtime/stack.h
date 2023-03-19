#ifndef __RUNTIME_STACK_H__
#define __RUNTIME_STACK_H__

struct lpc_value_t;

class lpc_stack_t
{
public:
    lpc_stack_t(int sz);
    bool push(lpc_value_t *);
    lpc_value_t * get(int idx);
    lpc_value_t * top();
    lpc_value_t * pop();
    bool pop_n(int n);
    
    void set_local_size(int n)
    {
        idx += n;
    }

private:
    lpc_value_t *stack;
    int idx;
    int size;
};

#endif

