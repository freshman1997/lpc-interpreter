﻿#ifndef __LPC__
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

union lpc_gc_object_t;
struct gc_header{
    lpc_gc_object_t * next = nullptr;
    lpc_gc_object_t *gclist = nullptr;
    lint8_t marked = 0;
    lint8_t type = 0;
};

class lpc_vm_t;

typedef void (*efun_t)(lpc_vm_t *, lint32_t);

#endif
