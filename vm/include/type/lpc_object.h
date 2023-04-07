#ifndef __LPC_OBJECT__
#define __LPC_OBJECT__
#include "type/lpc_string.h"
#include "lpc_proto.h"

struct lpc_value_t;
class lpc_object_t
{
public: 
    gc_header header;

public:
    lpc_value_t * copy();
    lpc_proto_t * get_proto();
    void set_proto(lpc_proto_t *);
    const char * get_pc();

    lpc_value_t * get_locals()
    {
        return this->locals;
    }

    const char * get_name()
    {
        return name;
    }

    void set_name(const char *name)
    {
        this->name = name;
    }

    int get_hash()
    {
        return hash;
    }

    void set_hash(int hash, int no)
    {
        this->hash = hash;
        this->no = no;
    }
    
private:
    const char *name = nullptr;
    int hash = 0;
    int no = 0;

    lpc_value_t *locals;
    lpc_proto_t *proto = nullptr;
};

#endif