#ifndef __VM_H__
#define __VM_H__
#include "type/lpc_proto.h"
#include "type/lpc_mapping.h"
#include "type/lpc_object.h"

struct lpc_value_t;

struct call_info_t
{
    call_info_t *pre, *next;
    const char *savepc;
    lpc_value_t *top;
    lpc_value_t *base;
    int funcIdx;
};

typedef void (*exit_hook_t)(void);

class lpc_vm_t
{

public:
    lpc_vm_t(lpc_vm_t &) = delete;
    lpc_vm_t & operator=(lpc_vm_t &) = delete;

    lpc_vm_t * create_vm();
    void bootstrap();
    void set_entry();
    void on_start();
    void on_exit();

    lpc_object_t * get_current_object();
    void set_current_object(lpc_object_t *);
    
private:
    call_info_t *ci;
    exit_hook_t hook;
    lpc_mapping_t *loaded_protos;
    lpc_object_t *current_object;
};

#endif
