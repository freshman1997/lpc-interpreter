﻿#ifndef __LPC_MAPPING_H__
#define __LPC_MAPPING_H__
#include "lpc.h"
#include "opcode.h"

struct lpc_value_t;
class lpc_array_t;
class lpc_allocator_t;

// 链地址法
struct bucket_t
{
    bucket_t *next = nullptr;
    lpc_value_t *pair = nullptr;
};

class lpc_mapping_t
{
public:
    gc_header header;

public:
    lpc_mapping_t(lpc_allocator_t *);
    lpc_mapping_t * copy();
    bucket_t * get(lpc_value_t *k);
    lpc_value_t * get_value(lpc_value_t *k);
    void set(lpc_value_t *k, lpc_value_t *v);
    bool upset(lpc_value_t *k, lpc_value_t *v, OpCode op);
    bucket_t * get_members();
    luint32_t get_size();
    bucket_t * get_bucket(int i);
    bucket_t * iterate(int i);
    void remove(lpc_value_t *);
    void grow();
    void reset_iterator();

    void dtor(lint32_t &freeBytes);

private:
    int calc_hash(lpc_value_t *);
    void place(bucket_t *newBuckets, int newSize, bucket_t *buck, bool reuse = false);
    
    lpc_allocator_t *alloc;
    bucket_t *members;
    bucket_t *cur;

    luint32_t fill;
    luint32_t used;
    luint32_t size;

    lint32_t idx;
};

lpc_array_t * mapping_values(lpc_mapping_t *, lpc_allocator_t *);
lpc_array_t * mapping_keys(lpc_mapping_t *, lpc_allocator_t *);
void map_delete(lpc_mapping_t *map, lpc_value_t *k);

#endif