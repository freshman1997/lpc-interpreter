#ifndef __VM_H__
#define __VM_H__
#include "type/lpc_proto.h"
#include "type/lpc_mapping.h"
#include "type/lpc_object.h"

struct lpc_value_t;
class lpc_stack_t;

struct call_info_t
{
    call_info_t *pre, *next;
    const char *savepc;         // 当前执行到的指令
    lpc_value_t *top;           // 栈顶位置
    lpc_value_t *base;          // 开始执行栈底位置
    lpc_object_t *cur_obj;      // 当前执行的对象
    int funcIdx;                // 当前对象执行的函数的位置
};

typedef void (*exit_hook_t)(void);

class lpc_vm_t
{

public:
    lpc_vm_t(lpc_vm_t &) = delete;
    lpc_vm_t & operator=(lpc_vm_t &) = delete;

    static lpc_vm_t * create_vm();

    void bootstrap();
    void set_entry();
    void on_start();
    void on_exit();

    call_info_t * get_call_info();
    lpc_stack_t * get_stack();

    void call_efun();
    void call_sfun();
    
    call_info_t * new_frame();
    void pop_frame();

private:
    lpc_vm_t();
    const char *entry;
    exit_hook_t hook;
    lpc_mapping_t *loaded_protos;
    call_info_t *ci;
    call_info_t *cur_ci;
    lpc_stack_t *stack;
    int init_stack_size;
    int ncall;

    void **efuns;               // efun table
    int size_efun;

    lpc_object_t *sfun_obj;     // sfun obj，调用是根据偏移找到
};

#endif
