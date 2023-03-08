#ifndef __LPC_STRING__
#define __LPC_STRING__

struct lpc_value_t;

class lpc_string_t
{
public:
    lpc_string_t(const char * _str);
    lpc_string_t(lpc_string_t &);
    lpc_string_t * operator=(lpc_string_t &);
    unsigned char get(int i);
    int get_size();
    lpc_value_t * copy();
    const char * get_str();
    int get_hash();

private:
    const char *str = nullptr;
    int size = 0;
    int hash = 0;
};

#endif