#include <cmath>
#include <limits.h>
#include <ctime>
#include "util/hash.h"

static int otable_size;             // 表的大小
static int otable_size_minus_one;   // 表的大小 - 1

#define ObjHash(s) whashstr(s, 40) & otable_size_minus_one
/*
 ** A simple and fast generic string hasher based on Peter K. Pearson's
 ** article in CACM 33-6, pp. 677.
 */
static int T[] =
{
    1, 87, 49, 12, 176, 178, 102, 166, 121, 193, 6, 84, 249, 230, 44, 163, 14, 197, 213, 181, 161, 85, 218, 80, 64, 239, 24, 226, 236, 142, 38, 200, 110, 177, 104, 103, 
141, 253, 255, 50, 77, 101, 81,
    18, 45, 96, 31, 222, 25, 107, 190, 70, 86, 237, 240, 34, 72, 242, 20, 214, 244, 227, 149, 235, 97, 234, 57, 22, 60, 250, 82, 175, 208, 5, 127, 199, 111, 62, 135, 248
, 174, 169, 211, 58, 66, 154,
    106, 195, 245, 171, 17, 187, 182, 179, 0, 243, 132, 56, 148, 75, 128, 133, 158, 100, 130, 126, 91, 13, 153, 246, 216, 219, 119, 68, 223, 78, 83, 88, 201, 99, 122, 11
, 92, 32, 136, 114, 52, 10,
    138, 30, 48, 183, 156, 35, 61, 26, 143, 74, 251, 94, 129, 162, 63, 152, 170, 7, 115, 167, 241, 206, 3, 150, 55, 59, 151, 220, 90, 53, 23, 131, 125, 173, 15, 238, 79,
 95, 89, 16, 105, 137, 225,
    224, 217, 160, 37, 123, 118, 73, 2, 157, 46, 116, 9, 145, 134, 228, 207, 212, 202, 215, 69, 229, 27, 188, 67, 124, 168, 252, 42, 4, 29, 108, 21, 247, 19, 205, 39, 203, 233, 40, 186, 147, 198, 192,
    155, 33, 164, 191, 98, 204, 165, 180, 117, 76, 140, 36, 210, 172, 41, 54, 159, 8, 185, 232, 113, 196, 231, 47, 146, 120, 51, 65, 28, 144, 254, 221, 93, 189, 194, 139
, 112, 43, 71, 109, 184, 209,
};

inline int hashstr(const char *s,  /* string to hash */
int maxn,  /* maximum number of chars to consider */
int hashs)
{
    unsigned int h;
    unsigned char *p;

    h = (unsigned char) *s;
    if (h)
    {
        if (hashs > 256)
        {
            int oh = T[(unsigned char) *s];

            for (p = (unsigned char*)s + 1;  *p && p <= (unsigned char*)s + maxn; p++)
            {
                h = T[h ^  *p];
                oh = T[oh ^  *p];
            }
            h |= (oh << 8);
        }
        else {
            for (p = (unsigned char*)s + 1;  *p && p <= (unsigned char*)s + maxn; p++)
            {
                h = T[h ^  *p];
            }
        }
    }
    return (int) (h % hashs);
}

/*
 * whashstr is faster than hashstr, but requires an even power-of-2 table size
 * Taken from Amylaars driver.
 */
inline int whashstr(const char *s, int maxn)
{
    unsigned char oh, h;
    unsigned char *p;
    int i;

    if (! *s)
    {
        return 0;
    }

    p = (unsigned char*)s;
    oh = T[ *p];
    h = (*(p++) + 1) &0xff;

    for (i = maxn - 1;  *p && --i >= 0;)
    {
        oh = T[oh ^  *p];
        h = T[h ^ *(p++)];
    }

    return (oh << 8) + h;
}

#define POINTER_INT int
#define MAP_POINTER_HASH(x) (((POINTER_INT)(x) >> 4) ^ ((POINTER_INT)(x) & 0xFFFFFFF))

// 字符串表的大小
static int htable_size_minus_one = 100;

#define StrHash(s) (whashstr((s), 20) & (htable_size_minus_one))

int hash_(const char *str)
{
    return StrHash(str);
}

int hash_pointer(int x)
{
    return MAP_POINTER_HASH(x);
}

////////////////////////////////////// lua hash ///////////////////////////////////////////
#define lua_assert(c)		((void)0)
#define l_mathop(op)		op

#define lua_str2number(s,p)	strtod((s), (p))

#define LINT_MIN (-INT_MAX - 1)
#define LUA_MAXINTEGER		INT_MAX
#define LUA_MININTEGER		LINT_MIN
#define LUA_NUMBER          float
#define lua_numbertointeger(n,p) \
  ((n) >= (LUA_NUMBER)(LUA_MININTEGER) && \
   (n) < -(LUA_NUMBER)(LUA_MININTEGER) && \
      (*(p) = (int)(n), 1))

#define LUAI_HASHLIMIT		5



unsigned int luaS_hash (const char *str, size_t l, unsigned int seed) {
  unsigned int h = seed ^ (unsigned int)l;
  size_t step = (l >> LUAI_HASHLIMIT) + 1;
  for (; l >= step; l -= step)
    h ^= ((h<<5) + (h>>2) + (unsigned char)(str[l - 1]));
  return h;
}

/*
** Compute an initial seed as random as possible. Rely on Address Space
** Layout Randomization (if present) to increase randomness..
*/
#define addbuff(b,p,e) \
  { size_t t = (size_t)(e); \
    memcpy(b + p, &t, sizeof(t)); p += sizeof(t); }

#define luai_makeseed()		(unsigned int)(time(NULL))

static unsigned int makeseed (/*lua_State *L*/) {
  char buff[4 * sizeof(size_t)];
  unsigned int h = luai_makeseed();
  int p = 0;
  /*
  addbuff(buff, p, L);  // heap variable
  addbuff(buff, p, &h);  // local variable
  addbuff(buff, p, luaO_nilobject);  // global variable
  addbuff(buff, p, &lua_newstate);  // public function
  */
  lua_assert(p == sizeof(buff));
  return luaS_hash(buff, p, h);
}

/*
unsigned int luaS_hashlongstr (TString *ts) {
  lua_assert(ts->tt == LUA_TLNGSTR);
  if (ts->extra == 0) {  // no hash?
    ts->hash = luaS_hash(getstr(ts), ts->u.lnglen, ts->hash);
    ts->extra = 1;  // now it has its hash
  }
  return ts->hash;
}
*/

static int l_hashfloat (float n) {
  int i;
  int ni;
  n = l_mathop(frexp)(n, &i) * -(LUA_NUMBER)(INT_MIN);
  if (!lua_numbertointeger(n, &ni)) {  /* is 'n' inf/-inf/NaN? */
    lua_assert(luai_numisnan(n) || l_mathop(fabs)(n) == cast_num(HUGE_VAL));
    return 0;
  }
  else {  /* normal case */
    unsigned int u = (unsigned int)(i) + (unsigned int)(ni);
    return (int)(u <= (unsigned int)INT_MAX ? u : ~u);
  }
}

#define gnode(t,i)	(&(t)->node[i])
#define hashpow2(t,n)		(gnode(t, lmod((n), sizenode(t))))

#define hashstr(t,str)		hashpow2(t, (str)->hash)
#define hashboolean(t,p)	hashpow2(t, p)
#define hashint(t,i)		hashpow2(t, i)


/*
** for some types, it is better to avoid modulus by power of 2, as
** they tend to have many 2 factors.
*/
#define hashmod(t,n)	(gnode(t, ((n) % ((sizenode(t)-1)|1))))

#define point2uint(p)	((unsigned int)((size_t)(p) & UINT_MAX))
#define hashpointer(t,p)	hashmod(t, point2uint(p))
