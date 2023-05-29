#ifndef __OS__
#define __OS__
#include <string>

#include "lpc.h"

namespace  os
{
    void init_seed(lint64_t);
    lint64_t random();

    void register_exception_handler();

    std::string GetFormatTime();

} // namespace  os

#endif
