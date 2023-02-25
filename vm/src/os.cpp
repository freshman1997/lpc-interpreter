#include <cassert>

#include "os.h"

static lint64_t _rand_seed = 1;

// referrence to jvm source code
void os::init_seed(lint64_t initval)
{
    _rand_seed = initval;
}

lint64_t os::random()
{
    /* standard, well-known linear congruential random generator with
    * next_rand = (16807*seed) mod (2**31-1)
    * see
    * (1) "Random Number Generators: Good Ones Are Hard to Find",
    *      S.K. Park and K.W. Miller, Communications of the ACM 31:10 (Oct 1988),
    * (2) "Two Fast Implementations of the 'Minimal Standard' Random
    *     Number Generator", David G. Carta, Comm. ACM 33, 1 (Jan 1990), pp. 87-88.
    */
    const long a = 16807;
    const unsigned long m = 2147483647;
    const long q = m / a;        //assert(q == 127773, "weird math");
    const long r = m % a;        //assert(r == 2836, "weird math");

    // compute az=2^31p+q
    unsigned long lo = a * (long)(_rand_seed & 0xFFFF);
    unsigned long hi = a * (long)((unsigned long)_rand_seed >> 16);
    lo += (hi & 0x7FFF) << 16;

    // if q overflowed, ignore the overflow and increment q
    if (lo > m) {
        lo &= m;
        ++lo;
    }
    lo += hi >> 15;

    // if (p+q) overflowed, ignore the overflow and increment (p+q)
    if (lo > m) {
        lo &= m;
        ++lo;
    }
    return (_rand_seed = lo);
}
