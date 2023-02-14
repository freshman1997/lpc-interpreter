#ifndef __COMPILER_TOKEN_H__
#define __COMPILER_TOKEN_H__
#include <string>
#include "type_decl.h"

enum class TokenKind
{
    k_integer,                  // 100
    k_number,                   // 1002.2
    k_string,                   // ""

    k_key_word,
    k_identity,

    k_symbol_qs1,               // (
    k_symbol_qs2,               // )
    k_symbol_qm1,               // [
    k_symbol_qm2,               // ]
    k_symbol_qg1,               // {
    k_symbol_qg2,               // }
    k_symbol_no,                // #
    k_symbol_next,              // \\
    k_symbol_co,                // ;
    k_symbol_sep,               // ,
    k_symbol_show,              // :
    k_symbol_comment,           // //
    k_symbol_comment1,          // /*
    k_symbol_comment2,          // */

    k_oper_plus,                // +
    k_oper_minus,               // -
    k_oper_mul,                 // *
    k_oper_div,                 // /
    k_oper_mod,                 // %

    k_oper_plus_plus,           // ++
    k_oper_sub_sub,             // --

    k_oper_assign,              // =
    k_oper_plus_assign,         // +=
    k_oper_minus_assign,        // -=
    k_oper_mul_assign,          // *=
    k_oper_div_assign,          // /=
    k_oper_mod_assign,          // %=
    k_oper_pointer,             // ->

    k_cmp_gt,                   // >
    k_cmp_gte,                  // >=
    k_cmp_lt,                   // <
    k_cmp_lte,                  // <=
    k_cmp_not,                  // !
    k_cmp_neq,                  // !=
    k_cmp_eq,                   // ==
    k_cmp_or,                   // ||
    k_cmp_and,                  // &&
    k_cmp_quetion,              // ?
};

struct Token
{
    Token *next;
    TokenKind kind;
    lint32_t ival;
    lfloat64_t dval;
    std::string strval;

    const char *filename;
    int lineno;
    int col;

    // If this is expanded from a macro, the origin token
    Token *origin;
};


#endif
