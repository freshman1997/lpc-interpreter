#ifndef __LPC_OBJECT__
#define __LPC_OBJECT__
#include "lpc_value.h"
#include "lpc_proto.h"

struct lpc_value_t;
class lpc_object_t
{
public:
    lpc_gc_object_header_t header;
    
public:
    lpc_value_t * copy();
    object_proto_t * get_proto();
    void set_proto(object_proto_t *);

    const char * get_pc();
    lpc_string_t * get_next_object_name();
    

private:
    const char *name = nullptr;
    int hash = 0;
    int no = 0;

    lpc_value_t *locals;
    object_proto_t *proto = nullptr;
};

#endif