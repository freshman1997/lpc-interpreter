#ifndef __LPC_MAPPING_H__
#define __LPC_MAPPING_H__
#include "lpc.h"

struct lpc_value_t;
class lpc_mapping_iterator_t;
class lpc_array_t;

int calc_hash(lpc_value_t *val);

// 开放地址法

struct item_pair_t
{

};

struct bucket_t
{

};

class lpc_mapping_t
{
    friend lpc_mapping_iterator_t;
public:
    lpc_value_t * copy();
    lpc_value_t * get(lpc_value_t *k);
    void set(lpc_value_t *k, lpc_value_t *v);
    lpc_value_t * upset(lpc_value_t *k, lpc_value_t *v);
    bucket_t * get_members();
    luint32_t get_size();
    bucket_t * get_bucket(int i);

private:
    bucket_t *members;
    luint32_t size = 0;
};

class lpc_mapping_iterator_t
{
public:
    lpc_mapping_iterator_t() : idx(0) {}
    bool has_next() const;
    bucket_t *next();
    void set_mapping(lpc_mapping_t *);

private:
    luint32_t idx;
    lpc_mapping_t *m;
};

lpc_array_t * mapping_values(lpc_mapping_t *);
lpc_array_t * mapping_keys(lpc_mapping_t *);
void map_delete(lpc_mapping_t *map, lpc_value_t *k);

#endif