#include "lpc.h"
#include "runtime/vm.h"
#include "lpc_value.h"
#include "runtime/stack.h"

extern void debug_message(const char *fmt, ...);

static void print(lpc_stack_t *sk, lint32_t nparam)
{
    lpc_value_t *val = sk->pop();
    lpc_string_t *str = reinterpret_cast<lpc_string_t *>(val->gcobj);
    debug_message("%s\n", str->get_str());
}

void init_efuns(lpc_vm_t *vm)
{
    efun_t *efuns = new efun_t[2];
    efuns[1] = print;
    vm->register_efun(efuns);
}

