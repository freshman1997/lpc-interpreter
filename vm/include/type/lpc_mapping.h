#ifndef __LPC_MAPPING_H__
#define __LPC_MAPPING_H__

struct lpc_value_t;

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
public:
    lpc_value_t * copy();
    lpc_value_t * get(lpc_value_t *k);
    void set(lpc_value_t *k, lpc_value_t *v);
    lpc_value_t * upset(lpc_value_t *k, lpc_value_t *v);

private:
    bucket_t *members;
    int size = 0;
};

#endif