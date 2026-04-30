#ifndef __OS__
#define __OS__
#include <string>
#include <cstdint>

namespace  os
{
    void init_seed(int64_t);
    int64_t random();

    void register_exception_handler();

    std::string GetFormatTime();

} // namespace  os

#endif
