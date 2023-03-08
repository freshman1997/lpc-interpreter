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
    lint64_t seed = time(NULL);
	//debug_message("xxxxxxxxxxxxxxxxxxxxx %d:%d,%d,%d\n", 100, _abs(-1), hash_("hello"), luaS_hash("hello", 5, (unsigned int)(seed)));
	os::init_seed(seed);
	cout << "random value: " << os::random() << endl;

    lpc_vm_t *vm = lpc_vm_t::create_vm();
    vm::eval(vm);
	return 0;
}
