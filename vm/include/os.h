#ifndef __OS__
#define __OS__
#include "lpc.h"

namespace  os
{

    void init_seed(lint64_t);
    lint64_t random();

} // namespace  os

#endif
