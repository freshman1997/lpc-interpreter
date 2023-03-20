#ifndef __LPC_CLOSURE_H__
#define __LPC_CLOSURE_H__

struct lpc_value_t;

class lpc_closure_t
{
public:
    gc_header header;
    
public:
    lpc_value_t * copy();


private:
    
};

#endif
