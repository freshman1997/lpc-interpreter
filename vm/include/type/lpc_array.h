#ifndef __LPC_ARRAY_H__
#define __LPC_ARRAY_H__
#include "lpc.h"
#include "lpc_value.h"

struct lpc_value_t;
class lpc_array_iterator_t;
class lpc_gc_t;

class lpc_array_t
{
    friend lpc_array_iterator_t;
public:
    lpc_gc_object_header_t header;
    
public:
    lpc_array_t(luint32_t sz, lpc_value_t *);
    lpc_value_t * get(int i);
    void set(lpc_value_t *val, int i);
    lpc_value_t * copy();
    luint32_t get_size() const;
    
private:
    luint32_t size = 0;
    lpc_value_t *members;
};

lpc_array_t * array_add(lpc_array_t *l, lpc_array_t *r, lpc_gc_t *gc);
lpc_array_t * array_sub(lpc_array_t *l, lpc_array_t *r, lpc_gc_t *gc);

class lpc_array_iterator_t
{
public:
    lpc_array_iterator_t() : idx(0), arr(nullptr) {}
    void set_array(lpc_array_t *);
    bool has_next();
    lpc_value_t *next();

private:
    luint32_t idx;
    lpc_array_t *arr;
};

#endif