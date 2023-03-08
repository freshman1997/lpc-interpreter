#include "runtime/vm.h"
#include "runtime/stack.h"
#include "type/lpc_proto.h"
#include "type/lpc_object.h"

static object_proto_t * load_object_proto(const char *name)
{
    if (!name) return nullptr;

    return nullptr;
}

static lpc_object_t * load_object(const char *name)
{
    lpc_object_t *obj = new lpc_object_t;
    obj->set_proto(load_object_proto(name));

    return nullptr;
}

lpc_vm_t::lpc_vm_t()
{
    this->stack = new lpc_stack_t(20000);
    entry = "rc/main.c";
    call_info_t *ci = new call_info_t;
    ci->savepc = "\x05\x02\x07";
    ci->base = stack->top();
    lpc_object_t *obj = new lpc_object_t;
    ci->cur_obj = load_object(entry);

    this->ci = ci;
    this->cur_ci = ci;
}

lpc_vm_t * lpc_vm_t::create_vm()
{
    lpc_vm_t *vm = new lpc_vm_t();

    return vm;
}

void lpc_vm_t::bootstrap()
{

}

void lpc_vm_t::set_entry()
{

}

void lpc_vm_t::on_start()
{

}

void lpc_vm_t::on_exit()
{

}

lpc_stack_t * lpc_vm_t::get_stack()
{
    return this->stack;
}

call_info_t * lpc_vm_t::get_call_info()
{
    return this->cur_ci;
}

call_info_t * lpc_vm_t::new_frame()
{
    // TODO


    return cur_ci;
}

void lpc_vm_t::pop_frame()
{

}

lpc_gc_t * lpc_vm_t::get_gc()
{
    return this->gc;
}
