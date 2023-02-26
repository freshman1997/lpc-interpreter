#ifndef __VM_H__
#define __VM_H__

class LpcVm
{

public:
    void bootstrap();
    void set_entry();
    void on_start();
    void on_exit();
    void set_exit_hook();
};

#endif
