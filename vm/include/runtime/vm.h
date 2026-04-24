#ifndef __VM_H__
#define __VM_H__
#include "lpc.h"
#include "type/lpc_proto.h"
#include "type/lpc_mapping.h"
#include "type/lpc_object.h"
#include "runtime/interpreter.h"
#include "profile/profiler.h"

#include <string>

struct lpc_value_t;
class lpc_stack_t;
class lpc_allocator_t;
class lpc_gc_t;
class lpc_debugger_t;
class lpc_function_t;

struct call_info_t
{
    call_info_t *pre = nullptr, *next = nullptr;
    const char *savepc;
    lpc_value_t *top;
    lpc_value_t *base;
    object_proto_t *father = nullptr;
    lpc_object_t *cur_obj;
    int funcIdx;
    bool call_other = false;
    bool call_init = false;
    lint16_t inherit_offset = 0;
    lpc_function_t *callee = nullptr;
};

class call_info_pool_t
{
public:
    ~call_info_pool_t()
    {
        call_info_t *p = free_list;
        while (p) {
            call_info_t *next = p->next;
            delete p;
            p = next;
        }
    }

    call_info_t *alloc()
    {
        if (free_list) {
            call_info_t *ci = free_list;
            free_list = ci->next;
            ci->pre = nullptr;
            ci->next = nullptr;
            ci->savepc = nullptr;
            ci->top = nullptr;
            ci->base = nullptr;
            ci->father = nullptr;
            ci->cur_obj = nullptr;
            ci->funcIdx = 0;
            ci->call_other = false;
            ci->call_init = false;
            ci->inherit_offset = 0;
            ci->callee = nullptr;
            return ci;
        }
        return new call_info_t();
    }

    void release(call_info_t *ci)
    {
        ci->next = free_list;
        free_list = ci;
    }

private:
    call_info_t *free_list = nullptr;
};

struct vm_frame_info_t
{
    const char *object_name = nullptr;
    const char *function_name = nullptr;
    luint32_t pc_offset = 0;
    luint32_t line = 0;
    bool has_line = false;
};

struct vm_abort_signal {};

typedef void (*exit_hook_t)(void);

class lpc_vm_t
{
    friend lpc_allocator_t;
public:
    ~lpc_vm_t();
    lpc_vm_t(lpc_vm_t &) = delete;
    lpc_vm_t & operator=(lpc_vm_t &) = delete;

    static lpc_vm_t * create_vm();

    void bootstrap();
    void set_entry(const char *);
    const char * get_entry() const
    {
        return this->entry;
    }
    void on_start();
    void on_exit();
    
    void load_config();

    void run() 
    {
        vm::eval(this);
    }

    void run_main();
    void set_non_fatal_mode(bool enable) { non_fatal_mode_ = enable; }
    bool non_fatal_mode() const { return non_fatal_mode_; }
    void set_last_error(const std::string &msg) { last_error_ = msg; }
    void clear_last_error() { last_error_.clear(); }
    bool has_error() const { return !last_error_.empty(); }
    const std::string &last_error() const { return last_error_; }
    bool has_last_int_result() const { return has_last_int_result_; }
    lint32_t last_int_result() const { return last_int_result_; }
    void set_memory_limit_bytes(luint64_t bytes);
    luint64_t memory_limit_bytes() const;
    luint64_t allocated_bytes() const;
    luint64_t gc_collect_count() const;
    luint64_t gc_last_freed_bytes() const;
    luint64_t gc_total_freed_bytes() const;
    luint64_t gc_last_collected_objects() const;
    luint64_t gc_total_collected_objects() const;
    luint64_t gc_write_barrier_count() const;
    void gc_write_barrier(lpc_gc_object_t *container, const lpc_value_t *value);
    luint64_t gc_minor_collect_count() const;
    luint64_t gc_major_collect_count() const;
    luint64_t gc_nursery_bytes() const;
    luint64_t gc_nursery_limit_bytes() const;
    luint64_t gc_remembered_set_size() const;
    luint64_t gc_minor_last_freed_bytes() const;
    luint64_t gc_minor_total_freed_bytes() const;
    luint64_t gc_major_last_freed_bytes() const;
    luint64_t gc_major_total_freed_bytes() const;
    luint64_t gc_minor_last_elapsed_us() const;
    luint64_t gc_minor_total_elapsed_us() const;
    luint64_t gc_major_last_elapsed_us() const;
    luint64_t gc_major_total_elapsed_us() const;
    void set_nursery_limit_bytes(luint64_t bytes);

    call_info_t * get_call_info();
    const object_proto_t * get_frame_proto(const call_info_t *ci) const;
    const function_proto_t * get_frame_function(const call_info_t *ci) const;
    const char * get_frame_code_base(const call_info_t *ci) const;
    const std::vector<std::pair<luint32_t, luint32_t>> * get_frame_line_map(const call_info_t *ci) const;
    bool get_frame_info(const call_info_t *ci, vm_frame_info_t *out) const;
    call_info_t * get_base_call()
    {
        return this->base_ci;
    }

    lpc_stack_t * get_stack();

    call_info_t * new_frame(lpc_object_t *, lint16_t idx, bool init = false, lpc_function_t *callee = nullptr);
    void pop_frame();
    lpc_gc_t * get_gc();

    object_proto_t * load_object_proto(const char *name);
    lpc_object_t * load_object(const char *name, bool newOne = false);
    void on_loaded_object(lpc_object_t *, const char *, bool newOne = false);
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
    std::string traceback_string() const;
    std::string current_frame_string() const;
    void panic();
    void stack_overflow();

    void on_debug_mode();
    bool is_debug() { return dbg != nullptr; }
    bool check_run();

    void start_debug();

    void enable_profiler(bool enable = true)
    {
        profiling = enable;
        if (enable && !profiler) {
            profiler = new lpc::profile::Profiler();
        }
    }

    bool is_profiling() const
    {
        return profiling && profiler != nullptr;
    }

    lpc::profile::Profiler * get_profiler()
    {
        return profiler;
    }

    std::string dump_profile() const
    {
        if (!profiler) {
            return std::string("{\"opcodes\":[],\"functions\":[]}");
        }
        return profiler->DumpJson();
    }

private:
    lpc_vm_t();
    void set_entry_owned(const std::string &entry)
    {
        this->entry_storage = entry;
        this->entry = this->entry_storage.c_str();
    }
    const char *entry;
    std::string entry_storage;
    lpc_object_t *entry_obj;
    exit_hook_t hook;
    lpc_mapping_t *loaded_protos;
    call_info_t *cur_ci;
    call_info_t *base_ci;
    call_info_pool_t ci_pool;
    
    lpc_stack_t *stack;
    lint32_t init_stack_size;
    lint32_t ncall = 0;

    efun_t *efuns;               // efun table
    lint32_t size_efun;

    const char *sfun_object_name;
    lpc_object_t *sfun_obj;     // sfun obj，调用是根据偏移找到
    lpc_allocator_t * alloc;
    lpc_gc_t *gc;
    lpc_debugger_t *dbg;
    bool profiling = false;
    lpc::profile::Profiler *profiler = nullptr;
    bool non_fatal_mode_ = false;
    std::string last_error_;
    bool has_last_int_result_ = false;
    lint32_t last_int_result_ = 0;
};

#endif
