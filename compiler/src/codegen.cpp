#include <iostream>
#include <fstream>

#include "ast.h"
#include "parser.h"
#include "codegen.h"
#include "opcode.h"

using namespace std;

static vector<EfunDecl> efuns = {
    {"call_other", {DeclType::object_, DeclType::varargs_}, true, DeclType::void_},
    {"debug_message", {DeclType::string_, DeclType::varargs_}, true, DeclType::void_},
    {"puts", {DeclType::mixed_, }, false, DeclType::void_},
    {"sleep", {DeclType::mixed_}, false, DeclType::void_},
    {"sizeof", {DeclType::mixed_}, false, DeclType::int_},
    {"random", {DeclType::int_}, false, DeclType::int_},
    {"intp", {DeclType::mixed_}, false, DeclType::bool_},
    {"floatp", {DeclType::mixed_}, false, DeclType::bool_},
    {"stringp", {DeclType::mixed_}, false, DeclType::bool_},
    {"arrayp", {DeclType::mixed_}, false, DeclType::bool_},
    {"mapp", {DeclType::mixed_}, false, DeclType::bool_},
    {"undefinedp", {DeclType::mixed_}, false, DeclType::bool_},
};

static vector<Func> sfuns;

void init_sfun(const char *sfunFile, Parser &parser)
{
    try {
        AbstractExpression *exp = parser.parse_one(sfunFile);
        DocumentExpression *doc = dynamic_cast<DocumentExpression *>(exp);
        if (doc->contents.empty()) {
            cout << "init simulate object: " << sfunFile << " fail !!!\n";
            exit(-1);
        }

        CodeGenerator g;
        g.generate(exp);
        g.dump();
        sfuns.assign(g.get_funcs().size(), {});
        std::copy(g.get_funcs().begin(), g.get_funcs().end(), sfuns.begin());
    } catch (...) {
        cout << "init simulate object: " << sfunFile << "fail !!!\n";
        exit(-1);
    }
}

static lint16_t find_fun_idx(const string &name, int type)
{
    lint16_t idx = 0;
    if (type == 0) {
        if (sfuns.empty()) return -1;

        for (auto &it : sfuns) {
            if (it.name->strval == name) {
                return idx;
            }
            ++idx;
        }
    } else {
        for (lint16_t i = 0; i < lint16_t(efuns.size()); ++i) {
            if (efuns[i].name == name) {
                return i;
            }
        }
    }

    return -1;
}

static luint16_t find_func_idx(const string &name, vector<Func> &con)
{
    luint16_t idx = 0;
    for (auto &it : con) {
        if (it.name->strval == name) return idx;
        ++idx;
    }

    return -1;
}

#define LOAD_IDX_2(con, idx) const char *idx2char = (const char *)&idx; \
    con.push_back(idx2char[0]); \
    con.push_back(idx2char[1]);

#define LOAD_IDX_2_1(con, idx) const char *idx2char1 = (const char *)&idx; \
    con.push_back(idx2char1[0]); \
    con.push_back(idx2char1[1]);    

#define LOAD_IDX_4(con, idx) idx2char = (const char *)&idx; \
    con.push_back(idx2char[0]); \
    con.push_back(idx2char[1]); \
    con.push_back(idx2char[2]); \
    con.push_back(idx2char[3]); \

#define GENERATE_VALUE(v, vType) \
    ValueExpression *val = dynamic_cast<ValueExpression *>(v); \
    if (val->valType == 0) { \
        lint16_t idx = find_const<lint32_t>(val->val.ival, intConsts); \
        if (idx < 0) { \
            idx = intConsts.size(); \
            intConsts.push_back(val->val.ival); \
        } \
        \
        (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_load_iconst)); \
        LOAD_IDX_2((on_var_decl ? var_init_codes : opcodes), idx) \
    } else if (val->valType == 1) { \
        lint16_t idx = find_const<lfloat32_t>(val->val.dval, floatConsts); \
        if (idx < 0) { \
            idx = floatConsts.size(); \
            floatConsts.push_back(val->val.dval); \
        } \
        \
        (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_load_fconst)); \
        LOAD_IDX_2((on_var_decl ? var_init_codes : opcodes), idx) \
    } else if (val->valType == 2) { \
        /* TODO optimize */ \
        lint16_t idx = find_const<string>(val->val.sval->strval, stringConsts); \
        if (idx < 0) { \
            idx = stringConsts.size(); \
            stringConsts.push_back(val->val.sval->strval); \
        } \
        \
        (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_load_sconst)); \
        LOAD_IDX_2((on_var_decl ? var_init_codes : opcodes), idx) \
    } else if (val->valType == 3) { \
        if (val->val.ival) { \
            (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_load_1)); \
        } else { \
            (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_load_0)); \
        } \
    } else if (val->valType == 4) { \
        if (vType == DeclType::user_define_) { \
            /* TODO optimize */ \
            lint16_t idx = find_const<string>(val->val.sval->strval, stringConsts); \
            if (idx < 0) { \
                idx = stringConsts.size(); \
                stringConsts.push_back(val->val.sval->strval); \
            } \
            \
            (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_load_sconst)); \
            LOAD_IDX_2((on_var_decl ? var_init_codes : opcodes), idx) \
        } else { \
            lint16_t idx = find_local_idx(val->val.sval->strval, scopeLocals); \
            Local *loc = nullptr; \
            bool isFun = false; \
            if (idx < 0) { \
                idx = find_local_idx(val->val.sval->strval, locals[object_name]); \
                if (idx < 0) { \
                    idx = find_func_idx(val->val.sval->strval, funcs); \
                    if (idx < 0) { \
                    /* TODO undefine identifier */ error_at(__LINE__); \
                    } \
                    isFun = true; \
                } \
                if (!isFun) { \
                    loc = &locals[object_name][idx]; \
                    (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_load_global)); \
                } else {\
                    (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_load_func)); \
                } \
            } else {\
                loc = &scopeLocals[idx]; \
                if (object_name != cur_scope) { \
                    (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_load_local)); \
                } else { \
                    (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_load_global)); \
                } \
            } \
            \
            if (!isFun && vType != DeclType::none_ && loc->type != vType) { \
                /* TODO 类型不匹配 */ error_at(__LINE__);\
            } \
            \
            LOAD_IDX_2((on_var_decl ? var_init_codes : opcodes), idx) \
        } \
    }

#define GENERATE_OP(exp, check) \
    if (exp->get_type() == ExpressionType::index_) { \
        generate_index(exp, false); \
    } else if (exp->get_type() == ExpressionType::uop_) { \
        generate_unop(exp); \
    } else if (exp->get_type() == ExpressionType::oper_) { \
        generate_binary(exp); \
    } else if (exp->get_type() == ExpressionType::triple_) { \
        generate_triple(exp); \
    } else if (exp->get_type() == ExpressionType::call_) { \
        generate_call(exp, check); \
    } else if (exp->get_type() == ExpressionType::value_) { \
        vector<Local> &scopeLocals = locals[cur_scope]; \
        GENERATE_VALUE(exp, check) \
    } else { \
        error_at(__LINE__);\
    }


#define GENERATE_BODY(body, cons) \
    for (auto &it : body) { \
        switch (it->get_type()) { \
            case ExpressionType::var_decl_: { \
                generate_decl(it); \
                break; \
            } \
            case ExpressionType::oper_: { \
                generate_binary(it); \
                break; \
            } \
            case ExpressionType::uop_: { \
                generate_unop(it); \
                break; \
            } \
            case ExpressionType::if_: { \
                /* 里面 break、continue */ \
                generate_if_else(it, forContinue, forBreaks); \
                break; \
            } \
            case ExpressionType::for_normal_: { \
                generate_for(it); \
                break; \
            } \
            case ExpressionType::foreach_: { \
                generate_foreach(it); \
                break; \
            } \
            case ExpressionType::while_: { \
                generate_while(it); \
                break; \
            } \
            case ExpressionType::do_while_: { \
                generate_do_while(it); \
                break; \
            } \
            case ExpressionType::index_: { \
                generate_index(it, true); \
                break;\
            } \
            case ExpressionType::break_: { \
                push_code((luint8_t)(OpCode::op_goto)); \
                lint32_t t = opcode_size(); \
                const char *idx2char = nullptr; \
                LOAD_IDX_4(codes(), t) \
                forBreaks.push_back(t); \
                break; \
            } \
            case ExpressionType::continue_: { \
                if (forContinue < 0) { \
                    error_at(__LINE__); \
                } \
                push_code((luint8_t)(OpCode::op_goto)); \
                if (cons != nullptr) { \
                    cons->push_back((on_var_decl ? var_init_codes : opcodes).size()); \
                } \
                const char *idx2char = nullptr; \
                LOAD_IDX_4(codes(), forContinue) \
                break; \
            } \
            case ExpressionType::switch_case_: { \
                generate_switch_case(it, forContinue); \
                break; \
            } \
            \
            case ExpressionType::return_: { \
                generate_return(it); \
                break; \
            } \
            case ExpressionType::call_: { \
                generate_call(it, DeclType::none_); \
                break; \
            } \
            default: { \
                error_at(__LINE__);\
                break; \
            } \
        } \
    }


extern void error(Token *tok);

static void error_at(int line)
{
    cout << "error found in line " << line << endl;
    throw "xxx";
}

static lint16_t find_local_idx(const string &name, const std::vector<Local> &vec)
{
    lint16_t idx = 0;
    for (auto &it : vec) {
        if (it.name->strval == name) return idx;
        ++idx;
    }

    return -1;
}

static lint16_t find_class_idx(std::vector<ClassDecl> &con, const string &name)
{
    lint16_t idx = 0;
    for (auto &it : con) {
        if (it.name->strval == name) return idx;
        ++idx;
    }

    return -1;
}

static void find_all_inherits(vector<AbstractExpression *> &container, vector<DocumentExpression *> &inherits, DocumentExpression *doc)
{
    const string &cwd = get_cwd();
    const string &realName = cwd + "\\" + doc->inherit;
    for (auto &it : container) {
        if (it == doc) continue;

        DocumentExpression *d = dynamic_cast<DocumentExpression *>(it);
        if (d->file_name == realName) {
            inherits.push_back(d);
            
            if (!d->inherit.empty()) {
                if (d->inherit == doc->inherit) {
                    error_at(__LINE__);
                }
                
                find_all_inherits(container, inherits, d);
            }
        }
    }
}

extern void generate_one(DocumentExpression *doc, vector<AbstractExpression *> *docs);

void CodeGenerator::generate(AbstractExpression *exp)
{
    DocumentExpression *doc = dynamic_cast<DocumentExpression *>(exp);
    locals[doc->file_name] = {};
    object_name = doc->file_name;
    if (doc->inherit.size() > 0) {
        // copy field
        find_all_inherits(*this->parsed_files, inherits, doc);
        if (inherits.empty()) {
            error_at(__LINE__);
        }

        auto &locs = this->locals[object_name];
        for (int i = inherits.size() - 1; i >= 0; --i) {
            if (!inherits[i]->gen) {
                generate_one(inherits[i], parsed_files);
            }

            CodeGenerator *g = reinterpret_cast<CodeGenerator *>(inherits[i]->gen);
            std::vector<Local> *objGlobal = g->get_scope_locals(inherits[i]->file_name);
            if (objGlobal) {
                int sz = locs.size();
                locs.assign(objGlobal->size(), {});
                copy(objGlobal->begin(), objGlobal->end(), locs.begin() + sz);
            }
        }
    }

    for (auto &it : doc->contents) {
        cur_scope = doc->file_name;
        on_var_decl = false;
        switch (it->get_type()) {
            case ExpressionType::var_decl_: {
                on_var_decl = true;
                generate_decl(it);
                break;
            }
            case ExpressionType::oper_: {
                on_var_decl = true;
                generate_binary(it);
                break;
            }
            case ExpressionType::class_: {
                generate_class(it);
                break;
            }
            case ExpressionType::func_decl_: {
                generate_func(it);
                break;
            }
            case ExpressionType::import_: {
                // 这个引入符号的，应当有个地方存储起来
                on_var_decl = true;
                generate_import(it);
                break;
            }
            default: {
                cout << "unexpected expression type found!\n";
                exit(-1);
                break;
            }
        }
    }
}

void CodeGenerator::generate_import(AbstractExpression *exp)
{
    ImportExpression *imp = dynamic_cast<ImportExpression *>(exp);
    string &str = imp->path[0]->strval;
    for (int i = 1; i < imp->path.size(); ++i) {
        str.push_back('/');
        str.append(imp->path[i]->strval);
    }

    VarDeclExpression *dec = new VarDeclExpression;
    dec->dtype = DeclType::string_;
    dec->name = imp->asName;
    ValueExpression *val = new ValueExpression;
    val->valType = 2;
    val->val.sval = imp->path[0];

    BinaryExpression *bin = new BinaryExpression;
    bin->l = dec;
    bin->r = val;
    bin->oper = TokenKind::k_oper_assign;

    delete imp;

    generate_binary(bin);
}


void CodeGenerator::generate_unop(AbstractExpression *exp)
{
    UnaryExpression *uop = dynamic_cast<UnaryExpression *>(exp);
    if (uop->exp->get_type() != ExpressionType::value_ && uop->exp->get_type() != ExpressionType::index_ 
        && uop->exp->get_type() != ExpressionType::uop_ && uop->exp->get_type() != ExpressionType::triple_) {
        error_at(__LINE__);
    }

    if (uop->exp->get_type() == ExpressionType::value_) {
        ValueExpression *v =dynamic_cast<ValueExpression *>(uop->exp);
        if (v->valType == 2 || v->valType == 3) {
            error_at(__LINE__);
        }
    }

    if (uop->exp->get_type() == ExpressionType::index_) {
        generate_index(exp, true);
    } else {
        GENERATE_OP(uop->exp, DeclType::none_)
    }

    if (uop->op == TokenKind::k_oper_minus) {
        (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_minus));
    } else if (uop->op == TokenKind::k_oper_sub_sub) {
        (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_dec));
    } else if (uop->op == TokenKind::k_oper_plus_plus) {
        (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_inc));
    } else if (uop->op == TokenKind::k_cmp_not) {
        (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_cmp_not));
    } else {
        error_at(__LINE__);
    }

    // 有个保存的过程
    if (uop->exp->get_type() == ExpressionType::value_) {
        ValueExpression *val = dynamic_cast<ValueExpression *>(uop->exp);
        if (uop->op != TokenKind::k_cmp_not && uop->op != TokenKind::k_oper_minus) {
            if (val->valType == 4) {
                lint16_t idx = find_local_idx(val->val.sval->strval, locals[cur_scope]);
                if (idx < 0) {
                    idx = find_local_idx(val->val.sval->strval, locals[object_name]);
                    if (idx < 0) {
                        error_at(__LINE__);
                    }
                    opcodes.push_back((luint8_t)(OpCode::op_store_global));
                } else {
                    opcodes.push_back((luint8_t)(OpCode::op_store_local));
                }
                LOAD_IDX_2(opcodes, idx)
            } else {
                error_at(__LINE__);
            }
        }
    } else if (uop->exp->get_type() == ExpressionType::index_) {
        luint8_t op = opcodes.back();
        opcodes.pop_back();
        opcodes.push_back((luint8_t)OpCode::op_upset);
        opcodes.push_back(op);
    } else if (uop->exp->get_type() == ExpressionType::uop_) {
        generate_unop(uop->exp);
    } else if (uop->exp->get_type() == ExpressionType::oper_) {
        generate_binary(uop->exp);
    } else if (uop->exp->get_type() == ExpressionType::call_) { 
        generate_call(uop->exp);
    } else if (uop->exp->get_type() == ExpressionType::call_) {
        generate_triple(uop->exp);
    } else {
        error_at(__LINE__);
    }
}

void CodeGenerator::generate_decl(AbstractExpression *exp)
{
    VarDeclExpression *var = dynamic_cast<VarDeclExpression *>(exp);
    if (on_var_decl) {
        if (locals[object_name].size() >= MAX_LOCAL) {
            cout << "too many object field!\n";
            exit(-1);
        }

        lint16_t idx = find_local_idx(var->name->strval, locals[object_name]);
        if (idx >= 0) {
            cout << "redeclare variable: " << var->name->strval << endl;
            error(var->name);
        }

        idx = (lint16_t)locals[object_name].size();
        locals[object_name].push_back({var->is_static, var->dtype, var->name, false, idx, var->is_arr, var->user_define_type});
    } else {
        lint16_t idx = find_local_idx(var->name->strval, locals[object_name]);
        if (idx >= 0) {
            cout << "redeclare variable: " << var->name->strval << endl;
            error(var->name);
        }
        
        vector<Local> &scopeLocals = locals[cur_scope];
        if (scopeLocals.size() >= MAX_LOCAL) {
            cout << "too many object field!\n";
            exit(-1);
        }

        idx = find_local_idx(var->name->strval, scopeLocals);
        if (idx >= 0) {
            cout << "redeclare variable: " << var->name->strval << endl;
            error(var->name);
        }

        idx = (luint16_t)scopeLocals.size();
        scopeLocals.push_back({var->is_static, var->dtype, var->name, false, idx, var->is_arr,var->user_define_type});
        ++funcs.back().nlocals;
    }
}

static std::set<TokenKind> constFold = {
    TokenKind::k_oper_mul,
    TokenKind::k_oper_div,
    TokenKind::k_oper_plus,
    TokenKind::k_oper_minus,
    TokenKind::k_oper_mod
};

#define DO_FOLD_CONST(op) \
    ValueExpression *val = new ValueExpression; \
    if (left->valType == 0 && right->valType == 0) { \
        val->valType = 0; \
        val->val.ival = left->val.ival + right->val.ival; \
    } else { \
        val->valType = 0; \
        val->val.dval = (left->valType == 0 ? left->val.ival : left->val.dval) op (right->valType == 0 ? right->val.ival : right->val.dval); \
    } \
    res = val;

static AbstractExpression * calc(BinaryExpression *bin)
{
    AbstractExpression *res = bin;
    ValueExpression *left = dynamic_cast<ValueExpression *>(bin->l);
    ValueExpression *right = dynamic_cast<ValueExpression *>(bin->r);
    if (constFold.count(bin->oper) && (left->valType == 0 || left->valType == 1) && (right->valType == 0 || right->valType == 1) ) {
        switch (bin->oper)
        {
        case TokenKind::k_oper_plus: {
            // int + int, int + float
            DO_FOLD_CONST(+)
            break;
        }
        case TokenKind::k_oper_minus: {
            DO_FOLD_CONST(-)
            break;
        }
        case TokenKind::k_oper_mul: {
            DO_FOLD_CONST(*)
            break;
        }
        case TokenKind::k_oper_div: {
            DO_FOLD_CONST(/)
            break;
        }
        case TokenKind::k_oper_mod: {
            if (left->valType == 0 && right->valType == 0) {
                ValueExpression *val = new ValueExpression;
                val->valType = 0;
                val->val.ival = left->val.ival % right->val.ival;
                res = val;
            } else {
                // TODO
            }
            break;
        }
        default:
            break;
        }
    } else {
        if ((left->valType == 3 || right->valType == 3) && bin->oper != TokenKind::k_oper_assign) {
            error_at(__LINE__);
        }
    }

    return res;
}

static AbstractExpression * calc_const(BinaryExpression *bin)
{
    if (bin->r->get_type() == ExpressionType::value_) {
        if (bin->l->get_type() == ExpressionType::value_) {
            return calc(bin);
        } else {
            if (bin->l->get_type() == ExpressionType::oper_) {
                AbstractExpression *res = calc_const(dynamic_cast<BinaryExpression *>(bin->l));
                // 折叠完成
                if (res != bin->l) {
                    bin->l = res;
                    return calc(bin);
                }
            } else if (bin->r->get_type() == ExpressionType::oper_) {
                AbstractExpression *res = calc_const(dynamic_cast<BinaryExpression *>(bin->r));
                // 折叠完成
                if (res != bin->r) {
                    bin->r = res;
                    return calc(bin);
                }
            }
        }
    }

    return bin;
}

static std::unordered_map<TokenKind, OpCode> op2code = {
    {TokenKind::k_oper_assign, OpCode::op_load_fconst},
    {TokenKind::k_oper_plus_assign, OpCode::op_load_fconst},
    {TokenKind::k_oper_minus_assign, OpCode::op_load_fconst},
    {TokenKind::k_oper_mul_assign, OpCode::op_load_fconst},
    {TokenKind::k_oper_div_assign, OpCode::op_load_fconst},
    {TokenKind::k_oper_mod_assign, OpCode::op_load_fconst},
    
    {TokenKind::k_cmp_or, OpCode::op_cmp_or},
    {TokenKind::k_cmp_and, OpCode::op_cmp_and},

    {TokenKind::k_oper_bin_or, OpCode::op_binary_or},
    {TokenKind::k_oper_bin_and, OpCode::op_binary_and},
    {TokenKind::k_oper_bin_lm, OpCode::op_binary_lm},
    {TokenKind::k_oper_bin_rm, OpCode::op_binary_rm},

    {TokenKind::k_cmp_eq, OpCode::op_cmp_eq},
    {TokenKind::k_cmp_neq, OpCode::op_cmp_neq},

    {TokenKind::k_cmp_gt, OpCode::op_cmp_gt},
    {TokenKind::k_cmp_gte, OpCode::op_cmp_gte},
    {TokenKind::k_cmp_lt, OpCode::op_cmp_lt},
    {TokenKind::k_cmp_lte, OpCode::op_cmp_lte},

    {TokenKind::k_oper_plus, OpCode::op_add},
    {TokenKind::k_oper_minus, OpCode::op_sub},

    {TokenKind::k_oper_mul, OpCode::op_mul},
    {TokenKind::k_oper_div, OpCode::op_div},
    {TokenKind::k_oper_mod, OpCode::op_mod},

    {TokenKind::k_key_word_or, OpCode::op_or},
};

static std::set<TokenKind> assignSet = { 
    TokenKind::k_oper_assign, 
    TokenKind::k_oper_plus_assign, 
    TokenKind::k_oper_minus_assign, 
    TokenKind::k_oper_mul_assign, 
    TokenKind::k_oper_div_assign,
    TokenKind::k_oper_mod_assign,
};


#define LHS(op) \
    if (bin->l->get_type() == ExpressionType::index_) { \
        generate_index(left, true); \
        (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)OpCode::op_upset); \
        (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)op); \
    } else { \
        ValueExpression *val = dynamic_cast<ValueExpression *>(bin->l); \
        if (val->valType != 4) { \
            error_at(__LINE__); \
        } \
         \
        lint16_t idx = find_local_idx(val->val.sval->strval, scopeLocals); \
        if (idx < 0) { \
            idx = find_local_idx(val->val.sval->strval, locals[object_name]); \
            if (idx < 0) { \
                error_at(__LINE__); \
            } \
            (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)OpCode::op_load_global); \
            LOAD_IDX_2((on_var_decl ? var_init_codes : opcodes), idx) \
            (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)op); \
            (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)OpCode::op_store_global); \
        } else { \
            (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)OpCode::op_load_local); \
            LOAD_IDX_2((on_var_decl ? var_init_codes : opcodes), idx) \
            (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)op); \
            (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)OpCode::op_store_local); \
        } \
        LOAD_IDX_2((on_var_decl ? var_init_codes : opcodes), idx) \
    }

void CodeGenerator::generate_ctor(AbstractExpression *exp)
{
    ConstructExpression *ctor = dynamic_cast<ConstructExpression *>(exp);
    lint32_t count = 0;
    for (auto &it : ctor->body) {
        if (it->get_type() == ExpressionType::construct_) {
            generate_ctor(it);
        } else {
            GENERATE_OP(it, DeclType::none_)
        }
        ++count;
    } 
    
    const char * idx2char = (const char *)&count;
    if (ctor->type) {
        if (count % 2 != 0) { 
            error_at(__LINE__); 
        } 
        (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_new_mapping)); 
    } else { 
        (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_new_array)); 
    } 
    
    LOAD_IDX_4((on_var_decl ? var_init_codes : opcodes), count)
}

#define PRIMARY_CHECK \
    if (right->get_type() == ExpressionType::value_) { \
        ValueExpression *val = dynamic_cast<ValueExpression *>(right); \
        if (val->valType == 4) { \
            if (!on_var_decl) { \
                lint16_t idx = find_local_idx(val->val.sval->strval, locals[cur_scope]); \
                if (idx < 0) { \
                    idx = find_local_idx(val->val.sval->strval, locals[object_name]); \
                    if (idx < 0) { \
                        error_at(__LINE__); \
                    } \
                    \
                    Local &l = locals[object_name][idx]; \
                    if (l.type != DeclType::int_ && l.type != DeclType::float_) { \
                        error_at(__LINE__); \
                    } \
                } else { \
                    Local &l = locals[cur_scope][idx]; \
                    if (l.type != DeclType::int_ && l.type != DeclType::float_) { \
                        error_at(__LINE__); \
                    } \
                } \
            } else { \
                lint16_t idx = find_local_idx(val->val.sval->strval, locals[object_name]); \
                if (idx < 0) { \
                    error_at(__LINE__); \
                } \
                Local &l = locals[object_name][idx]; \
                if (l.type != DeclType::int_ && l.type != DeclType::float_) { \
                    error_at(__LINE__); \
                } \
            }\
        } else if (val->valType != 0 && val->valType != 1) { \
            error_at(__LINE__); \
        } \
    }

void CodeGenerator::oper_type_check(BinaryExpression *bin)
{
    AbstractExpression *left = bin->l;
    AbstractExpression *right = bin->r;
    if (bin->oper == TokenKind::k_oper_assign) {
        if (left->get_type() == ExpressionType::var_decl_) {
            VarDeclExpression *decl = dynamic_cast<VarDeclExpression *>(left);
            DeclType dtype = decl->dtype;
            if (dtype == DeclType::int_) {
                PRIMARY_CHECK
            } else if (dtype == DeclType::float_) {
                PRIMARY_CHECK
            } else if (dtype == DeclType::string_) {
                if (right->get_type() == ExpressionType::value_) { 
                    ValueExpression *val = dynamic_cast<ValueExpression *>(right);
                    if (val->valType == 4) {
                        if (on_var_decl) {
                            lint16_t idx = find_local_idx(val->val.sval->strval, locals[object_name]); 
                            if (idx < 0) { 
                                error_at(__LINE__); 
                            } 

                            Local &l = locals[object_name][idx]; 
                            if (l.type != DeclType::string_) { 
                                error_at(__LINE__); 
                            } 
                        } else {
                            lint16_t idx = find_local_idx(val->val.sval->strval, locals[cur_scope]);
                            if (idx < 0) {
                                idx = find_local_idx(val->val.sval->strval, locals[object_name]);
                                if (idx < 0) {
                                    error_at(__LINE__);
                                } 
                                
                                Local &l = locals[object_name][idx];
                                if (l.type != DeclType::string_) {
                                    error_at(__LINE__);
                                } 
                            } else {
                                Local &l = locals[cur_scope][idx];
                                if (l.type != DeclType::string_) {
                                    error_at(__LINE__);
                                }
                            }
                        }
                    } else if (val->valType != 2 && val->valType != 0) {
                        error_at(__LINE__);
                    }
                } 
            } else if (dtype == DeclType::bool_) {
                if (right->get_type() == ExpressionType::value_) { 
                    ValueExpression *val = dynamic_cast<ValueExpression *>(right);
                    if (val->valType == 4) {
                        if (on_var_decl) {
                            lint16_t idx = find_local_idx(val->val.sval->strval, locals[object_name]); 
                            if (idx < 0) { 
                                error_at(__LINE__); 
                            } 

                            Local &l = locals[object_name][idx]; 
                            if (l.type != DeclType::bool_ && l.type != DeclType::int_) { 
                                error_at(__LINE__); 
                            } 
                        } else {
                            lint16_t idx = find_local_idx(val->val.sval->strval, locals[cur_scope]);
                            if (idx < 0) {
                                idx = find_local_idx(val->val.sval->strval, locals[object_name]);
                                if (idx < 0) {
                                    error_at(__LINE__);
                                } 
                                
                                Local &l = locals[object_name][idx];
                                if (l.type != DeclType::bool_ && l.type != DeclType::int_) {
                                    error_at(__LINE__);
                                } 
                            } else {
                                Local &l = locals[cur_scope][idx];
                                if (l.type != DeclType::bool_ && l.type != DeclType::int_) {
                                    error_at(__LINE__);
                                }
                            }
                        }
                    } else if (val->valType != 3 && val->valType != 0) {
                        error_at(__LINE__);
                    }
                } 
            }
        }
    }
}

// 怎么做常量折叠　？
void CodeGenerator::generate_binary(AbstractExpression *exp, bool fromAssign)
{
    BinaryExpression *bin = dynamic_cast<BinaryExpression *>(exp);

    oper_type_check(bin);

    if (on_var_decl && bin->oper != TokenKind::k_oper_assign) {
        error_at(__LINE__);
    }

    vector<Local> *t = nullptr;
    if (on_var_decl) {
        t = &locals[object_name];
    } else {
        t = &locals[cur_scope];
    }

    vector<Local> &scopeLocals = *t;

    // 条件、赋值
    if (constFold.count(bin->oper)) {
        AbstractExpression *res = calc_const(bin);
        if (res != bin) {
            GENERATE_VALUE(res, DeclType::none_)
            return;
        }
    } else if (assignSet.count(bin->oper) && bin->r->get_type() == ExpressionType::oper_) {
        AbstractExpression *res = calc_const(dynamic_cast<BinaryExpression *>(bin->r));
        if (res != bin->r) {
            bin->r = res;
        }
    }
    
    AbstractExpression *left = bin->l;
    AbstractExpression *right = bin->r;
    
    if ((left->get_type() == ExpressionType::var_decl_) && bin->oper != TokenKind::k_oper_assign) {
        error_at(__LINE__);
    }

    if (assignSet.count(bin->oper)) {
        if (left->get_type() != ExpressionType::value_ && left->get_type() != ExpressionType::index_ && left->get_type() != ExpressionType::var_decl_
            /*&& left->get_type() != ExpressionType::call_ */&& left->get_type() != ExpressionType::oper_
            /*&& left->get_type() != ExpressionType::uop_ && left->get_type() != ExpressionType::triple_*/) {
            error_at(__LINE__);
        }

        if (left->get_type() == ExpressionType::oper_) {
            BinaryExpression *tmp = dynamic_cast<BinaryExpression *>(left);
            if (tmp->oper != TokenKind::k_oper_pointer || tmp->r->get_type() != ExpressionType::value_) {
                error_at(__LINE__);
            }
        }
    }

try_parse_field:
    // 变量声明一定是在左边
    if (left->get_type() == ExpressionType::var_decl_) {
        generate_decl(left);
    } else {
        // 给标识符、函数调用、下标索引（mapping、array）登赋值的
        if (bin->oper != TokenKind::k_oper_assign && assignSet.count(bin->oper)) {
            if (left->get_type() == ExpressionType::value_){
                ValueExpression *v = dynamic_cast<ValueExpression *>(left);
                if (v->valType != 4) {
                    error_at(__LINE__);
                }
            }
        }

        if (bin->oper == TokenKind::k_oper_assign) {
            if (left->get_type() == ExpressionType::value_) {
                ValueExpression *v = dynamic_cast<ValueExpression *>(left);
                if (v->valType != 4) {
                    error_at(__LINE__);
                }
            }
        }

        if (left->get_type() == ExpressionType::construct_) {
            generate_ctor(left);
        } else {
            if (left->get_type() == ExpressionType::oper_ && assignSet.count(bin->oper)) {
                BinaryExpression *tmp = dynamic_cast<BinaryExpression *>(left);
                if (tmp->oper != TokenKind::k_oper_pointer) {
                    GENERATE_OP(left, DeclType::none_)
                }
            } else {
                if (!assignSet.count(bin->oper) && bin->oper != TokenKind::k_oper_pointer) {
                    GENERATE_OP(left, DeclType::none_)
                }
            }
        }
    }

    if (bin->oper == TokenKind::k_oper_pointer) {
        if (left->get_type() == ExpressionType::value_) {
            // h->id, user->GetId(), ""->xx, ""->hello()
            ValueExpression *v = dynamic_cast<ValueExpression *>(left);
            if (v->valType != 2 && v->valType != 4) {
                error_at(__LINE__);
            }

            if (right->get_type() == ExpressionType::value_) {
                ValueExpression *v1 = dynamic_cast<ValueExpression *>(right);
                if (v1->valType != 4 || v->valType != 4) {
                    error_at(__LINE__);
                }

                // 找到定义的 class 名称
                Local *loc = nullptr;
                lint16_t idx = find_local_idx(v->val.sval->strval, scopeLocals);
                if (idx < 0) {
                    idx = find_local_idx(v->val.sval->strval, locals[object_name]);
                    if (idx < 0) {
                        error_at(__LINE__);
                    }
                    loc = &locals[object_name][idx];
                } else {
                    loc = &scopeLocals[idx];
                }

                if (loc->type == DeclType::user_define_) {
                    idx = find_class_idx(this->clazz, loc->declName->strval);
                    if (idx < 0) {
                        error_at(__LINE__);
                    }

                    ClassDecl &cde = this->clazz[idx];
                    bool found = false;
                    idx = 0;
                    for (auto &it : cde.fields) {
                        if (it.first.first == v1->val.sval->strval) {
                            found = true;
                            break;
                        }

                        ++idx;
                    }

                    // 没找到 class 字段
                    if (!found) {
                        error_at(__LINE__);
                    }

                    if (fromAssign) {
                        (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_set_class_field));
                    } else {
                        (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_load_class_field));
                    }
                    LOAD_IDX_2((on_var_decl ? var_init_codes : opcodes), idx)
                } else {
                    error_at(__LINE__);
                }
            } else if (right->get_type() == ExpressionType::call_) {
                // ""->hello(), user->GetId()
                generate_call(right, DeclType::user_define_);
                char pre[5];
                for (int i = 4; i >= 0; --i) {
                    pre[i] = opcodes[opcodes.size() - 1];
                    opcodes.pop_back();
                }
                GENERATE_OP(left, DeclType::none_)
                for (int i = 0; i < 5; ++i) {
                    opcodes.push_back(pre[i]);
                }
            } else {
                error_at(__LINE__);
            }
        } else {
            error_at(__LINE__);
        }

        return;
    }

    // 右边的类型有：值，
    if (bin->oper != TokenKind::k_oper_assign && (constFold.count(bin->oper) || assignSet.count(bin->oper))) {
        ValueExpression *v = dynamic_cast<ValueExpression *>(right);
        if (v && v->valType == 3) {
            error_at(__LINE__);
        }
    }

    if (right->get_type() == ExpressionType::construct_) {
        generate_ctor(right);
    } else if (right->get_type() == ExpressionType::new_) {
        if (bin->oper != TokenKind::k_oper_assign) {
            error_at(__LINE__);
        }

        NewExpression *n = dynamic_cast<NewExpression *>(right);
        ValueExpression *id = dynamic_cast<ValueExpression *>(n->id);

        lint16_t idx = find_class_idx(this->clazz, id->val.sval->strval);
        if (idx < 0) {
            error_at(__LINE__);
        }

        (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_new_class));
        LOAD_IDX_2((on_var_decl ? var_init_codes : opcodes), idx)
    } else {
        if (right->get_type() == ExpressionType::oper_) {
            BinaryExpression *rbin = dynamic_cast<BinaryExpression *>(right);
            if (rbin->oper == TokenKind::k_oper_pointer) {
                generate_binary(right, true);
                goto next;
            }
        }

        GENERATE_OP(right, DeclType::none_)
    }

next:
    if (left->get_type() == ExpressionType::oper_ && assignSet.count(bin->oper)) {
        BinaryExpression *tmp = dynamic_cast<BinaryExpression *>(left);
        if (tmp->oper == TokenKind::k_oper_pointer) {
            bin = tmp;
            left = tmp->l;
            right = tmp->r;
            goto try_parse_field;
        }
    }

    // 一下的case左边只能是 variable 变量，而不能是值类型的
    switch (bin->oper)
    {
    case TokenKind::k_oper_assign:
        if (left->get_type() == ExpressionType::index_) {
            generate_index(left, true);
            (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)OpCode::op_upset);
            (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)0);
        } else {
            ValueExpression *val = dynamic_cast<ValueExpression *>(bin->l);
            string *str = nullptr;
            if (val) {
                if (val->valType != 4) error_at(__LINE__);
                str = &val->val.sval->strval;
            } else {
                VarDeclExpression *var = dynamic_cast<VarDeclExpression *>(bin->l);
                str = &var->name->strval;
            }

            lint16_t idx = find_local_idx(*str, scopeLocals);
            if (on_var_decl) {
                if (idx < 0) {
                    error_at(__LINE__);
                }

                (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)OpCode::op_store_global);
            } else {
                if (idx < 0) {
                    idx = find_local_idx(*str, locals[object_name]);
                    if (idx < 0) {
                        error_at(__LINE__);
                    }
                    
                    (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)OpCode::op_store_global);
                } else {
                    (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)OpCode::op_store_local);
                }
            }
            LOAD_IDX_2((on_var_decl ? var_init_codes : opcodes), idx)
        }
        return;
    case TokenKind::k_oper_plus_assign:
        LHS(OpCode::op_add)
        return;
    case TokenKind::k_oper_minus_assign:
        LHS(OpCode::op_minus)
        return;
    case TokenKind::k_oper_mul_assign:
        LHS(OpCode::op_mul)
        return;
    case TokenKind::k_oper_div_assign:
        LHS(OpCode::op_div)
        return;
    case TokenKind::k_oper_mod_assign:
        LHS(OpCode::op_mod)
        return;
    default: break;
    }

    if (on_var_decl) {
        var_init_codes.push_back((luint8_t)OpCode::op_store_global);
        VarDeclExpression *var = dynamic_cast<VarDeclExpression *>(bin->l);
        lint16_t idx = find_local_idx(var->name->strval, locals[object_name]);
        if (idx < 0) {
            error_at(__LINE__);
        }
        LOAD_IDX_2(var_init_codes, idx)
    } else {
        (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(op2code[bin->oper]));
    }
}

void CodeGenerator::generate_if_else(AbstractExpression *exp, lint32_t forContinue, vector<lint32_t> &forBreaks)
{
    IfExpression *ifExp = dynamic_cast<IfExpression *>(exp);
    if (ifExp->exps.empty()) {
        error_at(__LINE__);
    }
    
    vector<Local> &scopeLocals = locals[cur_scope];
    lint16_t nlocal = scopeLocals.size();
    lint32_t type0 = 0, type1 = 2, type2 = 0;
    lint32_t goto1 = 0, goto2 = 0;
    const char *idx2char = nullptr;
    vector<lint32_t> *p = nullptr;
    vector<lint32_t> gotoEnd;
    for (auto &it : ifExp->exps) {
        if (it.type == 0) {
            if (type0 > 1) {
                error_at(__LINE__);
            }

            ++type0;
            GENERATE_OP(it.cond, DeclType::none_)
            opcodes.push_back((luint8_t)(OpCode::op_test));
            goto1 = opcodes.size();
            LOAD_IDX_4(opcodes, goto1)
            
            GENERATE_BODY(it.body, p)
            
            // 这里是跳到结束
            opcodes.push_back((luint8_t)(OpCode::op_goto));
            lint32_t t = opcodes.size();
            gotoEnd.push_back(opcodes.size());
            LOAD_IDX_4(opcodes, t)

            // 这里是跳到下一个条件开始处
            t = opcodes.size();
            idx2char = (const char *)&t;
            for (int i = 0; i < 4; ++i) {
                opcodes[goto1 + i] = idx2char[i];
            }
        } else if (it.type == 1) {
            if (type0 == 0 || type0 > 1) {
                error_at(__LINE__);
            }

            ++type1;
            GENERATE_OP(it.cond, DeclType::none_)
            opcodes.push_back((luint8_t)(OpCode::op_test));
            goto2 = opcodes.size();
            LOAD_IDX_4(opcodes, goto2)
            GENERATE_BODY(it.body, p)

            // 这里是跳到结束
            opcodes.push_back((luint8_t)(OpCode::op_goto));
            lint32_t t = opcodes.size();
            gotoEnd.push_back(opcodes.size());
            LOAD_IDX_4(opcodes, t)

            // 这里是跳到下一个条件开始处
            t = opcodes.size();
            idx2char = (const char *)&t;
            for (int i = 0; i < 4; ++i) {
                opcodes[goto2 + i] = idx2char[i];
            }
        } else if (it.type == 2) {
            if (type0 == 0 || type0 > 1 || type2 > 1) {
                error_at(__LINE__);
            }

            ++type2;
            GENERATE_BODY(it.body, p)
        } else {
            error_at(__LINE__);
        }
    }

    // 修正跳转位置信息
    goto1 = opcodes.size();
    idx2char = (const char *)&goto1;
    for (auto &it : gotoEnd) {
        for (int i = 0; i < 4; ++i) {
            opcodes[it + i] = idx2char[i];
        }
    }

    int sz = scopeLocals.size();
    for (int i = nlocal; i < sz; ++i) {
        scopeLocals.pop_back();
    }
}

void CodeGenerator::generate_triple(AbstractExpression *exp)
{
    TripleExpression *tri = dynamic_cast<TripleExpression *>(exp);
    GENERATE_OP(tri->cond, DeclType::none_)
    (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_test));
    lint32_t second = (on_var_decl ? var_init_codes : opcodes).size(), end = 0;
    const char *idx2char = nullptr;
    LOAD_IDX_4((on_var_decl ? var_init_codes : opcodes), second)

    GENERATE_OP(tri->first, DeclType::none_)

    (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_goto));
    end = (on_var_decl ? var_init_codes : opcodes).size();
    LOAD_IDX_4((on_var_decl ? var_init_codes : opcodes), end)

    lint32_t t = (on_var_decl ? var_init_codes : opcodes).size();
    GENERATE_OP(tri->second, DeclType::none_)

    const char *arr = (const char *)&t;
    for (int i = 0; i < 4; i++) {
        (on_var_decl ? var_init_codes : opcodes)[second + i] = arr[i];
    }

    t = (on_var_decl ? var_init_codes : opcodes).size();
    arr = (const char *)&t;
    for (int i = 0; i < 4; i++) {
        (on_var_decl ? var_init_codes : opcodes)[end + i] = arr[i];
    }
}

void CodeGenerator::generate_for(AbstractExpression *exp)
{
    ForNormalExpression *forExp = dynamic_cast<ForNormalExpression *>(exp);

    vector<Local> &scopeLocals = locals[cur_scope];
    lint16_t nlocal = scopeLocals.size();

    for (auto &it : forExp->inits) {
        if (it->get_type() == ExpressionType::var_decl_) {
            generate_decl(it);
        } else if (it->get_type() == ExpressionType::oper_) {
            generate_binary(it);
        } else if (it->get_type() == ExpressionType::uop_) {
            generate_unop(it);
        } else {
            error_at(__LINE__);
        }
    }

    lint32_t forContinue = opcodes.size(), gotoEnd = 0;
    vector<lint32_t> forBreaks;
    if (forExp->conditions.size() > 1) {
        error_at(__LINE__);
    }

    if (!forExp->conditions.empty()) {
        AbstractExpression *cond = forExp->conditions.front();
        GENERATE_OP(cond, DeclType::none_)
        opcodes.push_back((luint8_t)(OpCode::op_test));
        gotoEnd = opcodes.size();
        const char *idx2char = nullptr;
        LOAD_IDX_4(opcodes, gotoEnd)
    }

    vector<lint32_t> *p = nullptr;
    GENERATE_BODY(forExp->body, p)

    for (auto &it : forExp->operations) {
        GENERATE_OP(it, DeclType::none_)
    }

    // 跳回条件处
    opcodes.push_back((luint8_t)(OpCode::op_goto));
    const char *idx2char = (const char *)(&forContinue);
    LOAD_IDX_4(opcodes, forContinue)

    lint32_t forBreak = opcodes.size();
    idx2char = (const char *)(&forBreak);

    // Note. 修正跳转的位置信息
    for (auto &it : forBreaks) {
        for (int i = 0; i < 4; ++i) {
            opcodes[it + i] = idx2char[i];
        }    
    }

    for (int i = 0; i < 4; ++i) {
        opcodes[gotoEnd + i] = idx2char[i];
    } 

    int size = scopeLocals.size();
    for (int i = nlocal; i < size; ++i) {
        scopeLocals.pop_back();
    }
}

void CodeGenerator::generate_foreach(AbstractExpression *exp)
{
    ForeachExpression *fe = dynamic_cast<ForeachExpression *>(exp);

    if (fe->decls.empty() || fe->decls.size() > 2) {
        error_at(__LINE__);
    }

    vector<Local> &scopeLocals = locals[cur_scope];
    lint16_t nlocal = scopeLocals.size();

    // TODO 后面考虑是否要作用域，目前全都是局部变量
    for (auto &it : fe->decls) {
        if (it->get_type() != ExpressionType::var_decl_) {
            error_at(__LINE__);
        }

        generate_decl(it);
    }

    GENERATE_OP(fe->container, DeclType::none_)

    opcodes.push_back((luint8_t)(OpCode::op_foreach_step1));

    luint8_t sz = (luint8_t)fe->decls.size();
    if (sz <= 0 || sz > 2) {
        error_at(__LINE__);
    }

    lint32_t forContinue = opcodes.size();
    opcodes.push_back((luint8_t)(OpCode::op_foreach_step2));
    opcodes.push_back((luint8_t)(sz));
    lint32_t end = opcodes.size();
    // 结束后跳转位置
    const char *idx2char = nullptr;
    LOAD_IDX_4(opcodes, end)

    luint16_t idx = scopeLocals.size() - sz;
    for (; idx < scopeLocals.size(); ++idx) {
        // 这里需要容器还在栈中
        opcodes.push_back((luint8_t)(OpCode::op_store_local));
        LOAD_IDX_2(opcodes, idx)
    }

    vector<lint32_t> forBreaks;
    vector<lint32_t> *p = nullptr;
    GENERATE_BODY(fe->body, p)

    // 跳回赋值处
    opcodes.push_back((luint8_t)(OpCode::op_goto));
    idx2char = (const char *)(&forContinue);
    LOAD_IDX_4(opcodes, forContinue)

    lint32_t t = opcodes.size();
    const char *idxCode = (const char *)&t;
    // Note. 修正跳转的位置信息
    for (auto &it : forBreaks) {
        for (int i = 0; i < 4; ++i) {
            opcodes[it + i] = idxCode[i];
        }    
    }

    for (int i = 0; i < 4; ++i) {
        opcodes[end + i] = idxCode[i];
    } 

    while (sz--) {
        scopeLocals.pop_back();
    }
}

void CodeGenerator::generate_while(AbstractExpression *exp)
{
    whileExpression *w = dynamic_cast<whileExpression *>(exp);

    vector<Local> &scopeLocals = locals[cur_scope];
    lint16_t nlocal = scopeLocals.size();

    lint32_t forContinue = opcodes.size();
    vector<lint32_t> forBreaks;
    GENERATE_OP(w->cond, DeclType::none_)
    opcodes.push_back((luint8_t)(OpCode::op_test));
    lint32_t gotoEnd = opcodes.size();
    const char *idx2char = nullptr;
    LOAD_IDX_4(opcodes, gotoEnd)

    vector<lint32_t> *p = nullptr;
    GENERATE_BODY(w->body, p)

    // 跳回条件处
    opcodes.push_back((luint8_t)(OpCode::op_goto));
    idx2char = (const char *)(&forContinue);
    LOAD_IDX_4(opcodes, forContinue)

    lint32_t t = opcodes.size();
    const char *idxCode = (const char *)&t;
    // Note. 修正跳转的位置信息
    for (auto &it : forBreaks) {
        for (int i = 0; i < 4; ++i) {
            opcodes[it + i] = idxCode[i];
        }    
    }
    
    for (int i = 0; i < 4; ++i) {
        opcodes[gotoEnd + i] = idxCode[i];
    } 

    int sz = scopeLocals.size();
    for (int i = nlocal; i < sz; ++i) {
        scopeLocals.pop_back();
    }
}

void CodeGenerator::generate_do_while(AbstractExpression *exp)
{
    DoWhileExpression *dw = dynamic_cast<DoWhileExpression *>(exp);

    vector<Local> &scopeLocals = locals[cur_scope];
    lint16_t nlocal = scopeLocals.size();

    lint32_t forContinue = opcodes.size();
    vector<lint32_t> forBreaks;
    vector<lint32_t> forContinues;
    vector<lint32_t> *p = &forContinues;
    lint32_t gotoBegin = opcodes.size();
    GENERATE_BODY(dw->body, p)

    lint32_t gotoCond = opcodes.size();
    GENERATE_OP(dw->cond, DeclType::none_)

    const char *idxCode = (const char *)&gotoCond;
    for (auto &it : forContinues) {
        for (int i = 0; i < 4; ++i) {
            opcodes[it + i] = idxCode[i];
        }    
    }

    lint32_t t = opcodes.size();
    idxCode = (const char *)&t;
    // Note. 修正跳转的位置信息
    for (auto &it : forBreaks) {
        for (int i = 0; i < 4; ++i) {
            opcodes[it + i] = idxCode[i];
        }    
    }
    
    opcodes.push_back((luint8_t)(OpCode::op_test));
    const char *idx2char = nullptr;
    LOAD_IDX_4(opcodes, gotoBegin)

    int sz = scopeLocals.size();
    for (int i = nlocal; i < sz; ++i) {
        scopeLocals.pop_back();
    }
}

void CodeGenerator::generate_switch_case(AbstractExpression *exp, lint32_t forContinue)
{
    SwitchCaseExpression *sce = dynamic_cast<SwitchCaseExpression *>(exp);
    if (sce->cases.empty()) {
        return;
    }
    
    vector<Local> &scopeLocals = locals[cur_scope];
    lint16_t nlocal = scopeLocals.size();

    luint16_t t = lookup_switch.size();
    if (t >= MAX_LOCAL) {
        error_at(__LINE__);
    }

    lookup_switch.push_back({});
    GENERATE_OP(sce->selector, DeclType::none_)
    opcodes.push_back((luint8_t)(OpCode::op_switch));
    LOAD_IDX_2(opcodes, t)
    luint32_t gotoEnd = opcodes.size();
    LOAD_IDX_4(opcodes, gotoEnd)

    vector<pair<lint32_t, lint32_t>> &talbe = lookup_switch.back();

    vector<lint32_t> forBreaks;
    vector<lint32_t> forContinues;
    vector<lint32_t> *p = &forContinues;
    for (auto &it1 : sce->cases) {
        if (it1->get_type() == ExpressionType::case_) {
            CaseExpression *c = dynamic_cast<CaseExpression *>(it1);
            if (c->caser->get_type() != ExpressionType::value_) {
                error_at(__LINE__);
            }

            ValueExpression *val = dynamic_cast<ValueExpression *>(c->caser);
            // 目前支持整形的
            if (val->valType > 0) {
                error_at(__LINE__);
            }

            talbe.push_back({val->val.ival, opcodes.size()});

            GENERATE_BODY(c->bodys, p)
        } else if (it1->get_type() == ExpressionType::default_) {
            DefaultExpression *d = dynamic_cast<DefaultExpression *>(it1);
            talbe.push_back({0, - opcodes.size()});
            GENERATE_BODY(d->bodys, p)
        } else {
            error_at(__LINE__);
        }
    }

    luint32_t t1 = opcodes.size();
    const char *idxCode = (const char *)&t1;
    // Note. 修正跳转的位置信息
    for (auto &it : forBreaks) {
        for (int i = 0; i < 4; ++i) {
            opcodes[it + i] = idxCode[i];
        }    
    }

    for (int i = 0; i < 4; ++i) {
        opcodes[gotoEnd + i] = idxCode[i];
    }

    int sz = scopeLocals.size();
    for (int i = nlocal; i < sz; ++i) {
        scopeLocals.pop_back();
    }
}

void CodeGenerator::generate_class(AbstractExpression *exp)
{
    ClassExpression *cle = dynamic_cast<ClassExpression *>(exp);

    ClassDecl decl = {cle->is_static, cle->className, (luint16_t)cle->fields.size()};
    for (auto &it : cle->fields) {
        if (it->get_type() != ExpressionType::var_decl_) {
            error_at(__LINE__);
        }

        VarDeclExpression *var = dynamic_cast<VarDeclExpression *>(it);
        pair<std::string, DeclType> p;
        p.first = var->name->strval;
        p.second = var->dtype;
        lint16_t idx = 0;
        if (var->dtype == DeclType::user_define_) {
            idx = find_class_idx(this->clazz, var->name->strval);
            if (idx < 0) {
                error_at(__LINE__);
            }
        }

        decl.fields.push_back({p, idx});
    }

    this->clazz.push_back(decl);
}

void CodeGenerator::generate_index(AbstractExpression *exp, bool lhs)
{
    IndexExpression *idex = dynamic_cast<IndexExpression *>(exp);
    // id, call, index, 
    if (idex->l->get_type() == ExpressionType::value_) {
        ValueExpression *val = dynamic_cast<ValueExpression *>(idex->l);
        if (val->valType != 4) {
            error_at(__LINE__);
        }

        Local *loc = nullptr;
        vector<Local> &scopeLocals = locals[cur_scope];
        lint16_t idx = find_local_idx(val->val.sval->strval, scopeLocals);
        if (idx < 0) {
            idx = find_local_idx(val->val.sval->strval, locals[object_name]);
            if (idx < 0) {
                cout << "undefined identifier: " << val->val.sval->strval << endl;
                error(val->val.sval);
            }
            loc = &locals[object_name][idx];
            (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_load_global));
        } else {
            loc = &scopeLocals[idx];
            (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_load_local));
        }

        if (loc->type != DeclType::mapping_ && !loc->is_arr && loc->type != DeclType::mixed_) {
            error_at(__LINE__);
        }

        LOAD_IDX_2((on_var_decl ? var_init_codes : opcodes), idx)
    } else if (idex->l->get_type() == ExpressionType::call_) {
        generate_call(idex->l);
    } else if (idex->l->get_type() == ExpressionType::index_) {
        // 多重 index 
        generate_index(idex->l, false);
    } else {
        error_at(__LINE__);
    }

    GENERATE_OP(idex->idx, DeclType::none_)
    if (idex->idx1) {
        if (lhs) {
            error_at(__LINE__);
        }
        
        // id, value, index, call, triple, pointor_, uop_, oper_
        if (idex->toend) {
            lint32_t end = -1;
            lint16_t idx = find_const<lint32_t>(end, intConsts); 
            if (idx < 0) { 
                idx = intConsts.size(); 
                intConsts.push_back(end); 
            } 

            (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_load_iconst)); 
            LOAD_IDX_2((on_var_decl ? var_init_codes : opcodes), idx)
        } else {
            GENERATE_OP(idex->idx1, DeclType::none_)
        }

        (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_sub_arr));
    } else if (!lhs) {
        (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_index));
    }
}

/// @brief h->hello(), ""->hello(), hello()
/// @param exp 
void CodeGenerator::generate_call(AbstractExpression *exp, DeclType type)
{
    CallExpression *call = dynamic_cast<CallExpression *>(exp);
    // TODO 这里还需要处理其他类型的函数调用 
    if (call->callee->get_type() == ExpressionType::value_) {
        ValueExpression *id = dynamic_cast<ValueExpression *>(call->callee);
        if (id->valType != 4) {
            error_at(__LINE__);
        }

        bool is_sfun = false;
        lint16_t funIdx = -1;

        // 函数没有声明, sfun、efun 
        if (type == DeclType::none_ && !locals.count(id->val.sval->strval)) {
            // TODO find if is simulate function or external function
            funIdx = find_fun_idx(id->val.sval->strval, 1);
            if (funIdx < 0) {
                funIdx = find_fun_idx(id->val.sval->strval, 0);
                is_sfun = funIdx >= 0;
            }
        } else if (type == DeclType::none_ && !call->callInherit) {
            funIdx = find_func_idx(id->val.sval->strval, funcs);
            if (funIdx < 0) {
                error_at(__LINE__);
            }
            
            const Func &f = funcs[funIdx];
            // 参数类型检查，查找当前上下文
            // 1、变量类型，2、函数调用返回值类型，4、op操作类型最后的类型
            // 参数数量是否一致
            if (call->params.size() > f.nparams) {
                error_at(__LINE__);
            }
        } 

        // 特殊处理，函数名和对象名最后才压入栈
        bool skip = id->val.sval->strval == "call_other";

        int i = 0;
        for (auto &it : call->params) {
            if (skip && i < 2) continue;

            if (it->get_type() == ExpressionType::value_) {
                vector<Local> &scopeLocals = locals[cur_scope];
                if (locals.count(id->val.sval->strval)) {
                    vector<Local> &callScope = locals[id->val.sval->strval];
                    GENERATE_VALUE(it, callScope[i].type)
                } else {
                    GENERATE_VALUE(it, DeclType::none_)
                }
            } else {
                GENERATE_OP(it, DeclType::none_);
            }

            ++i;
        }

        lint8_t extArgs = 0;
        if (type == DeclType::user_define_) {
            /* TODO optimize */
            lint16_t idx = find_const<string>(id->val.sval->strval, stringConsts);
            if (idx < 0) {
                idx = stringConsts.size();
                stringConsts.push_back(id->val.sval->strval);
            } 
            
            (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_load_sconst));
            LOAD_IDX_2((on_var_decl ? var_init_codes : opcodes), idx)

            string call_other = "call_other";
            funIdx = find_fun_idx(call_other, 1);
            if (funIdx < 0) {
                error_at(__LINE__);
            }

            extArgs = 2;
        }

        if (skip) {
            if (call->params.size() < 2) {
                error_at(__LINE__);
            }

            for (i = 1; i >= 0; --i) {
                auto &it = call->params[i];
                if (it->get_type() == ExpressionType::value_) {
                    vector<Local> &scopeLocals = locals[cur_scope];
                    if (locals.count(id->val.sval->strval)) {
                        vector<Local> &callScope = locals[id->val.sval->strval];
                        GENERATE_VALUE(it, callScope[i].type)
                    } else {
                        GENERATE_VALUE(it, DeclType::none_)
                    }
                } else {
                    GENERATE_OP(it, DeclType::none_);
                }
            }
        }

        if (call->callInherit || funIdx < 0) {
            const pair<lint16_t, lint16_t> &p = find_inherit_func(id->val.sval->strval);
            if (p.first < 0) {
                cout << "undefined function: " << id->val.sval->strval << endl;
                exit(-1);
            }
            
            (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_call_virtual));
            LOAD_IDX_2((on_var_decl ? var_init_codes : opcodes), p.first)
            LOAD_IDX_2_1((on_var_decl ? var_init_codes : opcodes), p.second)
            return;
        }

        (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_call));
        if (type == DeclType::none_ && locals.count(id->val.sval->strval)) {
            (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(3));
            LOAD_IDX_2((on_var_decl ? var_init_codes : opcodes), funcs[funIdx].idx)
        } else if (is_sfun) {
            Func &f = sfuns[funIdx];
            if (call->params.size() > f.nparams) {
                error_at(__LINE__);
            }

            (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(2));
            LOAD_IDX_2((on_var_decl ? var_init_codes : opcodes), funIdx)
        } else {
            (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(1));
            // 调用的 efun 位置
            LOAD_IDX_2((on_var_decl ? var_init_codes : opcodes), funIdx)

            EfunDecl &f = efuns[funIdx];
            luint16_t nparam  = f.varargs ? f.paramTypes.size() - 1 : f.paramTypes.size();
            if (!f.varargs && call->params.size() > nparam) {
                error_at(__LINE__);
            }

            (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(call->params.size() + extArgs));
        }
    } else {
        for (auto &it : call->params) {
            GENERATE_OP(it, DeclType::none_);
        }

        GENERATE_OP(call->callee, DeclType::none_)
        (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_call));
        (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(0));
    }
}

void CodeGenerator::generate_return(AbstractExpression *exp)
{
    ReturnExpression *ret = dynamic_cast<ReturnExpression *>(exp);
    if (ret->ret) {
        luint16_t idx = find_func_idx(cur_scope, funcs);
        if (idx < 0) {
            error_at(__LINE__);
        }

        Func &fun = funcs[idx];
        if (ret->ret->get_type() == ExpressionType::new_) {
            NewExpression *n = dynamic_cast<NewExpression *>(ret->ret);
            ValueExpression *id = dynamic_cast<ValueExpression *>(n->id);

            lint16_t idx = find_class_idx(this->clazz, id->val.sval->strval);
            if (idx < 0) {
                error_at(__LINE__);
            }

            (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_new_class));
            LOAD_IDX_2((on_var_decl ? var_init_codes : opcodes), idx)
        } else if (ret->ret->get_type() != ExpressionType::construct_) {
            GENERATE_OP(ret->ret, fun.retType)
        } else {
            generate_ctor(ret->ret);
        }
    }

    (on_var_decl ? var_init_codes : opcodes).push_back((luint8_t)(OpCode::op_return));
}

void CodeGenerator::generate_func(AbstractExpression *exp)
{
    // Note. 先不考虑闭包
    FunctionDeclExpression *funDecl = dynamic_cast<FunctionDeclExpression *>(exp);
    if (locals.count(funDecl->name->strval) && !pre_decl_funcs.count(funDecl->name->strval)) {
        cout << "redefined function: " << funDecl->name->strval << endl;
        exit(-1);
    }

    cur_scope = funDecl->name->strval;
    vector<Local> scopeLocals;
    vector<Local> *scope = &scopeLocals;
    bool preDef = false;
    luint16_t idx = -1;
    if (pre_decl_funcs.count(funDecl->name->strval)) {
        scope = &locals[funDecl->name->strval];
        preDef = true;
    } else {
        locals[funDecl->name->strval] = scopeLocals;
        scope = &locals[funDecl->name->strval];
        idx = (luint16_t)funcs.size();
        funcs.push_back({
            funDecl->is_static, 
            funDecl->is_varargs, 
            funDecl->name, 
            funDecl->returnType, 
            funDecl->user_define_type, 
            (lint16_t)funDecl->params.size(), 
            0,
            0,
            0,
            (lint16_t)funcs.size()
        });
    }

    Func &f = funcs[idx];
    if (!preDef) {
        lint16_t idx = 0;
        f.fromPc = (on_var_decl ? var_init_codes : opcodes).size();
        for (auto &it : funDecl->params) {
            VarDeclExpression *var = dynamic_cast<VarDeclExpression *>(it);
            scope->push_back({false, var->dtype, var->name, var->varargs, idx, var->is_arr});
            ++idx;
        }
    }

    if (!funDecl->body.empty()) {
        for (auto &it : funDecl->body) {
            switch (it->get_type()) {
                case ExpressionType::var_decl_: {
                    generate_decl(it);
                    break;
                }
                case ExpressionType::oper_: {
                    generate_binary(it);
                    break;
                }
                case ExpressionType::uop_: {
                    generate_unop(it);
                    break;
                }
                case ExpressionType::if_: {
                    vector<lint32_t> tmp;
                    generate_if_else(it, 0, tmp);
                    break;
                }
                case ExpressionType::for_normal_: {
                    generate_for(it);
                    break;
                }
                case ExpressionType::foreach_: {
                    generate_foreach(it);
                    break;
                }
                case ExpressionType::while_: {
                    generate_while(it);
                    break;
                }
                case ExpressionType::do_while_: {
                    generate_do_while(it);
                    break;
                }
                case ExpressionType::index_: {
                    generate_index(it, true);
                    break;
                }
                case ExpressionType::switch_case_: {
                    generate_switch_case(it, -1);
                    break;
                }

                case ExpressionType::return_: {
                    generate_return(it);
                    break;
                }
                case ExpressionType::call_: {
                    generate_call(it);
                    //CallExpression *call = dynamic_cast<CallExpression *>(it);
                    //opcodes.push_back((luint8_t)(OpCode::op_pop));
                    break;
                }
                default: {
                    error_at(__LINE__);
                    break;
                }
            }
        }
    } else {
        pre_decl_funcs.insert(funDecl->name->strval);
    }

    opcodes.push_back((luint8_t)(OpCode::op_return));
    f.toPc = opcodes.size();

    if (f.name) {
        if (f.name->strval == "create") {
            this->create_idx = f.idx;
        } else if (f.name->strval == "on_loadin") {
            this->onload_in_idx = f.idx;
        } else if (f.name->strval == "on_destruct") {
            this->on_destruct_idx = f.idx;
        }
    }
}

static string get_pure_name(const string &src)
{
    const string &cwd = get_cwd();
    int i = 0;
    for (; i < cwd.size(); ++i) {
        if (src[i] != cwd[i]) break;
    }

    return src.substr(i + 1);
}

void CodeGenerator::dump()
{
    size_t idx = object_name.find_last_of(".");
    if (idx == string::npos) {
        return;
    }

    string bFile = object_name.substr(0, idx) + ".b";
    //recurve_mkdir(bFile);
    ofstream out;
    out.open(bFile.c_str(), ios_base::binary);
    if (!out.good()) {
        cout << "can not open file: " << bFile.c_str() << endl;
        return;
    }

    string name = std::move(get_pure_name(object_name));
    luint32_t sz = name.size();
    out.write((char *)&sz, 4);
    out.write(name.c_str(), sz);

    sz = inherits.size();
    out.write((char *)&sz, 4);
    lint16_t nvar = 0;
    if (sz > 0) {
        for (auto &it : inherits) {
            CodeGenerator *g = reinterpret_cast<CodeGenerator *>(it->gen);
            vector<Local> *locs = g->get_scope_locals(it->file_name);
            nvar += lint16_t(locs->size());
            out.write((char *)&nvar, 2);

            string iname = std::move(get_pure_name(it->file_name));
            sz = iname.size();
            out.write((char *)&sz, 4);
            out.write(iname.c_str(), sz);
        }
    }

    out.write((char *)&create_idx, 2);
    out.write((char *)&onload_in_idx, 2);
    out.write((char *)&on_destruct_idx, 2);

    sz = this->lookup_switch.size();
    out.write((char *)&sz, 4);
    for (auto &it : lookup_switch) {
        sz = it.size();
        out.write((char *)&sz, 4);
        for (auto &it1 : it) {
            out.write((char *)&it1.first, 4);
            out.write((char *)&it1.second, 4);
        }
    }

    sz = this->funcs.size();
    out.write((char *)&sz, 4);
    for (auto &it : funcs) {
        sz = it.name->strval.size();
        out.write((char *)&sz, 4);
        out.write(it.name->strval.c_str(), sz);

        out.write((char *)&it.retType, 1);
        out.write((char *)&it.is_static, 1);
        out.write((char *)&it.nparams, 2);
        out.write((char *)&it.nlocals, 2);
        out.write((char *)&it.fromPc, 4);
        out.write((char *)&it.toPc, 4);
        // TODO closure
    }

    sz = this->locals[object_name].size();
    out.write((char *)&sz, 4);
    for (auto &it : locals[object_name]) {
        out.write((char *)&it.is_static, 1);
    }

    sz = this->intConsts.size();
    out.write((char *)&sz, 4);
    for (auto &it : intConsts) {
        out.write((char *)&it, 4);
    }

    sz = this->floatConsts.size();
    out.write((char *)&sz, 4);
    for (auto &it : floatConsts) {
        out.write((char *)&it, 4);
    }

    sz = this->stringConsts.size();
    out.write((char *)&sz, 4);
    for (auto &it : stringConsts) {
        sz = it.size();
        out.write((char *)&sz, 4);
        out.write(it.c_str(), sz);
    }

    bool hasClazz = this->clazz.empty();
    out.write((char *)&hasClazz, 1);

    if (!hasClazz) {
        sz = this->clazz.size();
        out.write((char *)&sz, 4);
        for (auto &it : this->clazz) {
            out.write((char *)&it.is_static, 1);
            out.write((char *)&it.nfields, 2);
        }
    }

    sz = this->var_init_codes.size();
    out.write((char *)&sz, 4);
    out.write(reinterpret_cast<char *>(var_init_codes.data()), sz);

    sz = opcodes.size();
    out.write((char *)&sz, 4);
    out.write(reinterpret_cast<char *>((on_var_decl ? var_init_codes : opcodes).data()), sz);

    out.close();
}
