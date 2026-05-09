#ifndef LPC_VM_RUNTIME_EFUN_H
#define LPC_VM_RUNTIME_EFUN_H

#include <cstdint>

namespace lpc {
namespace vm {

enum class Efun : std::uint16_t {
    CallOther      = 0,
    Print          = 1,
    Puts           = 2,
    Sleep          = 3,
    Sizeof         = 4,
    Random         = 5,
    Keys           = 6,
    Values         = 7,
    Typeof         = 8,
    ToString       = 9,
    ToInt          = 10,
    ThisObject     = 11,
    CloneObject    = 12,
    Destruct       = 13,
    Sprintf        = 14,
    Write          = 15,
    Time           = 16,
    MemberArray    = 17,
    Explode        = 18,
    Implode        = 19,
    Stringp        = 20,
    Intp           = 21,
    Floatp         = 22,
    Arrayp         = 23,
    Mappingp       = 24,
    Objectp        = 25,
    Nullp          = 26,
    Functionp      = 27,
    ToFloat        = 28,
    Abs            = 29,
    Strlen         = 30,
    MapDelete      = 31,
    Capitalize     = 32,
    LowerCase      = 33,
    UpperCase      = 34,
    Allocate       = 35,
    Reverse        = 36,
    Min            = 37,
    Max            = 38,
    Sqrt           = 39,
    Ctime          = 40,
    Strsrch        = 41,
    ReplaceString  = 42,
    SortArray      = 43,
    Instanceof     = 44,
    Getenv         = 45,
    CallLater      = 46,
    CancelTimer    = 47,
    Regexp         = 48,
    RegexReplace   = 49,
    TimerExists    = 50,
    PendingTimers  = 51,
    TimerInfo      = 52,
    TimerClearModule = 53,
    TimerStats     = 54,
    RegexStats     = 55,
};

constexpr bool EfunIsVoid(Efun e) {
    return e == Efun::Print || e == Efun::Puts || e == Efun::Sleep
        || e == Efun::Destruct || e == Efun::Write || e == Efun::MapDelete;
}

} // namespace vm
} // namespace lpc

#endif
