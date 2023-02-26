#ifndef __LPC_STRING__
#define __LPC_STRING__
class LpcString
{

private:
    const char *str = nullptr;
    int size = 0;
    int hash = 0;
};

#endif