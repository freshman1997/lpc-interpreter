#include <iostream>
#include <ctime>

#include "os/os.h"
#include "efun/efun.h"
#include "runtime/vm.h"
#include "runtime/interpreter.h"
#include "type/lpc_mapping.h"

extern void debug_message(const char *fmt, ...);
extern unsigned int luaS_hash (const char *str, size_t l, unsigned int seed);

using namespace std;
int _abs(int num)
{
    if (num < 0) {
        /* 负数补码先减一, 再取反, 得到原码, 即正数 */
        return ~(--num);
    }
    return num;
}

int main(int argc, char **argv)
{
    lpc_mapping_t *m = new lpc_mapping_t;
    lpc_value_t *k = new lpc_value_t;
    k->type = value_type::int_;
    k->val.number = 12;
    lpc_value_t *v = new lpc_value_t;
    v->type = value_type::int_;
    v->val.number = 100;
    m->upset(k, v);
    
	return 0;
}
