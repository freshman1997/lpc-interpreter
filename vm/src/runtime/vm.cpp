#include "runtime/vm.h"

lpc_vm_t * lpc_vm_t::create_vm()
{
    return nullptr;
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

lpc_object_t * lpc_vm_t::get_current_object()
{
    return this->current_object;
}

void lpc_vm_t::set_current_object(lpc_object_t *obj)
{
    this->current_object = obj;
}

