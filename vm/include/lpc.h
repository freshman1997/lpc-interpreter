#ifndef __LPC__
#define __LPC__

typedef unsigned char           luint8_t;
typedef char                    lint8_t;
typedef unsigned short          luint16_t;
typedef short                   lint16_t;
typedef unsigned int            luint32_t;
typedef int                     lint32_t;
typedef unsigned long long      luint64_t;
typedef long long               lint64_t;
typedef float                   lfloat32_t;
typedef double                  lfloat64_t;
typedef const char *            lstring_t;


#define lpc_assert

struct gc_header{
    void * next;
    lint8_t marked;
    lint8_t type;
};

class lpc_stack_t;

typedef void (*efun_t)(lpc_stack_t *, lint32_t);

#endif
