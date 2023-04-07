#ifndef __VM_H__
#define __VM_H__
#include "lpc.h"
#include "type/lpc_proto.h"
#include "type/lpc_mapping.h"
#include "type/lpc_object.h"
#include "runtime/interpreter.h"

struct lpc_value_t;
class lpc_stack_t;
class lpc_allocator_t;
class lpc_gc_t;

struct call_info_t
{
    call_info_t *pre = nullptr, *next = nullptr;
    const char *savepc;         // 当前执行到的指令
    lpc_value_t *top;           // 栈顶位置
    lpc_value_t *base;          // 开始执行栈底位置
    object_proto_t *father = nullptr;
    lpc_object_t *cur_obj;      // 当前执行的对象
    int funcIdx;                // 当前对象执行的函数的位置
    bool call_other = false;
    bool call_init = false;
    lint16_t inherit_offset = 0;
};

typedef void (*exit_hook_t)(void);

class lpc_vm_t
{
    friend lpc_allocator_t;
public:
    lpc_vm_t(lpc_vm_t &) = delete;
    lpc_vm_t & operator=(lpc_vm_t &) = delete;

    static lpc_vm_t * create_vm();

    void bootstrap();
    void set_entry(const char *);
    void on_start();
    void on_exit();
    void run() 
    {
        vm::eval(this);
    }

    call_info_t * get_call_info();
    call_info_t * get_base_call()
    {
        return this->base_ci;
    }

    lpc_stack_t * get_stack();

    call_info_t * new_frame(lpc_object_t *, lint16_t idx, bool init = false);
    void pop_frame();
    lpc_gc_t * get_gc();

    object_proto_t * load_object_proto(const char *name);
    lpc_object_t * load_object(const char *name);
    void on_loaded_object(lpc_object_t *, const char *);
    lpc_object_t * find_oject(lpc_value_t *);

    lpc_allocator_t * get_alloc()
    {
        return this->alloc;
    }

    void register_efun(efun_t *efuns)
    {
        this->efuns = efuns;
    }

    efun_t * get_efuns()
    {
        return this->efuns;
    }

    lpc_object_t * get_sfun_object()
    {
        return sfun_obj;
    }

    lpc_object_t * get_entry_object()
    {
        return entry_obj;
    }

    lpc_mapping_t * get_object_cache()
    {
        return loaded_protos;
    }

    void eval_init_codes(lpc_object_t *obj);
    void on_create_object(lpc_object_t *obj);
    void on_load_in_object(lpc_object_t *obj);
    void on_destruct_object(lpc_object_t *obj);

    void traceback();
    void panic();
    void stack_overflow();

private:
    lpc_vm_t();
    const char *entry;
    lpc_object_t *entry_obj;
    exit_hook_t hook;
    lpc_mapping_t *loaded_protos;
    call_info_t *cur_ci;
    call_info_t *base_ci;
    
    lpc_stack_t *stack;
    lint32_t init_stack_size;
    lint32_t ncall = 0;

    efun_t *efuns;               // efun table
    lint32_t size_efun;

    const char *sfun_object_name;
    lpc_object_t *sfun_obj;     // sfun obj，调用是根据偏移找到
    lpc_allocator_t * alloc;
    lpc_gc_t *gc;
};

#endif
