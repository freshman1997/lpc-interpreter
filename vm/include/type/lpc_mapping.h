#ifndef __LPC_MAPPING_H__
#define __LPC_MAPPING_H__
#include "lpc.h"

struct lpc_value_t;
class lpc_array_t;
class lpc_gc_t;

int calc_hash(lpc_value_t *val);

// 链地址法

struct bucket_t
{
    lpc_value_t *pair = nullptr;
    bucket_t *next = nullptr;
};

class lpc_mapping_t
{
public:
    gc_header header;

public:
    lpc_mapping_t(lpc_gc_t *);
    lpc_value_t * copy();
    bucket_t * get(lpc_value_t k);
    void set(lpc_value_t k, lpc_value_t v);
    void upset(lpc_value_t k, lpc_value_t v);
    bucket_t * get_members();
    luint32_t get_size();
    bucket_t * get_bucket(int i);
    int calc_hash(lpc_value_t *);
    void remove(lpc_value_t);

    void grow();

private:
    lpc_gc_t *gc;
    luint32_t fill;
    bucket_t *members;
    luint32_t size;
};

lpc_array_t * mapping_values(lpc_mapping_t *, lpc_gc_t *);
lpc_array_t * mapping_keys(lpc_mapping_t *);
void map_delete(lpc_mapping_t *map, lpc_value_t *k);

#endif