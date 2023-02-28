#ifndef __LPC_OBJECT__
#define __LPC_OBJECT__
#include "lpc_value.h"
#include "lpc_proto.h"

struct lpc_value_t;
class lpc_object_t
{
public:
    lpc_value_t * copy();
    object_proto_t * get_proto();
    void set_proto(object_proto_t *);

private:
    const char *name = nullptr;
    int hash = 0;
    int no = 0;
    int size_fields = 0;
    int size_func = 0;
    int size_class = 0;
    object_proto_t *proto = nullptr;
    char *pc = nullptr;

};

#endif