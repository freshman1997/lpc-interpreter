#ifndef __LPC_OBJECT__
#define __LPC_OBJECT__
#include "type/lpc_string.h"
#include "lpc_proto.h"

struct lpc_value_t;
class lpc_object_t
{
public:
    ~lpc_object_t();
    lpc_value_t * copy();
    object_proto_t * get_proto();
    void set_proto(object_proto_t *);

    const char * get_pc();
    lpc_string_t * get_next_object_name();

    lpc_value_t * get_locals()
    {
        return this->locals;
    }
    
private:
    const char *name = nullptr;
    int hash = 0;
    int no = 0;

    lpc_value_t *locals;
    object_proto_t *proto = nullptr;
};

#endif