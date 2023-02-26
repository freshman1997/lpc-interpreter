#ifndef __COMPILER_TOKEN_H__
#define __COMPILER_TOKEN_H__
#include <string>
#include "type_decl.h"

enum class TokenKind
{
    k_none,

    k_integer,                  // 100
    k_number,                   // 1002.2
    k_string,                   // ""

    k_key_word_int,
    k_key_word_float,
    k_key_word_mapping,
    k_key_word_bool,
    k_key_word_mixed,
    k_key_word_string,
    k_key_word_object,
    k_key_word_void,
    k_key_word_static,
    k_key_word_private,
    k_key_word_include,
    k_key_word_define,
    k_key_word_undef,
    k_key_word_if,
    k_key_word_else,
    k_key_word_for,
    k_key_word_foreach,
    k_key_word_do,
    k_key_word_while,
    k_key_word_in,
    k_key_word_or,
    k_key_word_switch,
    k_key_word_case,
    k_key_word_break,
    k_key_word_continue,
    k_key_word_default,
    k_key_word_return,
    k_key_word_varargs,
    k_key_word_import,
    k_key_word_as,
    k_key_word_true,
    k_key_word_false,
    k_key_word_class,
    k_key_word_new,
    k_key_word_inherit,
    k_key_word_fun,

    k_identity,

    k_symbol_qs1,               // (
    k_symbol_qs2,               // )
    k_symbol_qm1,               // [
    k_symbol_qm2,               // ]
    k_symbol_qg1,               // {
    k_symbol_qg2,               // }
    k_symbol_no,                // #
    k_symbol_next,              // \\ //
    k_symbol_co,                // ;
    k_symbol_sep,               // ,
    k_symbol_show,              // :
    k_symbol_dot,               // .
    k_symbol_sub_arr,           // ..
    k_symbol_var_arg,           // ...
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

    k_oper_bin_and,             // &
    k_oper_bin_or,              // |
    k_oper_bin_lm,              // <<
    k_oper_bin_rm,              // >>


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
    Token *next = nullptr;
    TokenKind kind = TokenKind::k_none;
    lint32_t ival = 0;
    lfloat64_t dval = 0;
    std::string strval;

    const char *filename;
    int lineno = 0;
    int col = 0;
    bool is_space = false;
    bool newline = false;

    // If this is expanded from a macro, the origin token
    Token *origin = nullptr;
};


#endif
