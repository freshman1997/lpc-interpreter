﻿#ifndef __LPC_BUFFER_H__
#define __LPC_BUFFER_H__
#include "lpc.h"

class lpc_buffer_t
{
public:
    gc_header header;
public:
    const char *buff;
    lint32_t size;
};

#endif
