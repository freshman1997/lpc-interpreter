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

class LpcMapping
{
public:
    lpc_value_t * copy();

private:
    bucket_t *members;
    int size = 0;
};

#endif