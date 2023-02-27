#ifndef __LPC_STRING__
#define __LPC_STRING__

struct lpc_value_t;

class lpc_string_t
{
public:
    unsigned char get(int i);
    int get_size();
    lpc_value_t * copy();

private:
    const char *str = nullptr;
    int size = 0;
    int hash = 0;
};

#endif