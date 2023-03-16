#include <iostream>
#include <fstream>
#ifdef WIN32
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "codegen.h"
#include "opcode.h"

using namespace std;

static vector<Func> efuns = {
    {false, true, nullptr, DeclType::void_, nullptr, 0, 0, 0, 0, "call_other"},
};

static vector<Func> sfuns = {};

void init_sfun_and_efun(const char *sfunFile, const char *efunDefFile)
{
    // TODO
    
}

static lint16_t find_fun_idx(const string &name, int type)
{
    vector<Func> &con = type ? efuns : sfuns;
    lint16_t idx = 0;
    for (auto &it : con) {
        if (it.efunName == name) {
            return idx;
        }
        ++idx;
    }

    return -1;
}

#define LOAD_IDX_2(con, idx) const char *idx2char = (const char *)&idx; \
    con.push_back(idx2char[0]); \
    con.push_back(idx2char[1]);

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
        opcodes.push_back((luint8_t)(OpCode::op_load_iconst)); \
        LOAD_IDX_2(opcodes, idx) \
    } else if (val->valType == 1) { \
        lint16_t idx = find_const<lfloat32_t>(val->val.dval, floatConsts); \
        if (idx < 0) { \
            idx = intConsts.size(); \
            intConsts.push_back(val->val.dval); \
        } \
        \
        opcodes.push_back((luint8_t)(OpCode::op_load_fconst)); \
        LOAD_IDX_2(opcodes, idx) \
    } else if (val->valType == 2) { \
        /* TODO optimize */ \
        lint16_t idx = find_const<string>(val->val.sval->strval, stringConsts); \
        if (idx < 0) { \
            idx = intConsts.size(); \
            stringConsts.push_back(val->val.sval->strval); \
        } \
        \
        opcodes.push_back((luint8_t)(OpCode::op_load_sconst)); \
        LOAD_IDX_2(opcodes, idx) \
    } else if (val->valType == 3) { \
        if (val->val.ival) { \
            opcodes.push_back((luint8_t)(OpCode::op_load_1)); \
        } else { \
            opcodes.push_back((luint8_t)(OpCode::op_load_0)); \
        } \
    } else if (val->valType == 4) { \
        if (vType == DeclType::user_define_) { \
            /* ""->xxx */ \
            /* TODO optimize */ \
            lint16_t idx = find_const<string>(val->val.sval->strval, stringConsts); \
            if (idx < 0) { \
                idx = intConsts.size(); \
                stringConsts.push_back(val->val.sval->strval); \
            } \
            \
            opcodes.push_back((luint8_t)(OpCode::op_load_sconst)); \
            LOAD_IDX_2(opcodes, idx) \
        } else { \
            lint16_t idx = find_local_idx(val->val.sval->strval, scopeLocals); \
            Local *loc = nullptr; \
            if (idx < 0) { \
                idx = find_local_idx(object_name, locals[object_name]); \
                if (idx < 0) { \
                    /* TODO undefine identifier */ error_at(__LINE__); \
                } \
                loc = &locals[object_name][idx]; \
                opcodes.push_back((luint8_t)(OpCode::op_load_global)); \
            } else {\
                loc = &scopeLocals[idx]; \
                if (object_name != cur_scope) { \
                    opcodes.push_back((luint8_t)(OpCode::op_load_local)); \
                } else { \
                    opcodes.push_back((luint8_t)(OpCode::op_load_global)); \
                } \
            } \
            \
            if (vType != DeclType::none_ && loc->type != vType) { \
                /* TODO 类型不匹配 */ error_at(__LINE__);\
            } \
            \
            LOAD_IDX_2(opcodes, idx) \
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
                generate_decl(it, false); \
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
                push_code((luint8_t)(OpCode::op_goto)); \
                if (cons != nullptr) { \
                    cons->push_back(opcodes.size()); \
                } \
                const char *idx2char = nullptr; \
                LOAD_IDX_4(codes(), forContinue) \
                break; \
            } \
            case ExpressionType::switch_case_: { \
                generate_switch_case(it); \
                break; \
            } \
            \
            case ExpressionType::return_: { \
                Func *fun = &get_funcs()[cur_scope]; \
                if (fun->retType == DeclType::void_) { \
                    error_at(__LINE__); \
                } \
                generate_return(it); \
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
    exit(-1);
}

lint16_t CodeGenerator::find_local_idx(const string &name, const std::vector<Local> &vec)
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

void CodeGenerator::generate(AbstractExpression *exp)
{
    DocumentExpression *doc = dynamic_cast<DocumentExpression *>(exp);
    locals[doc->file_name] = {};
    object_name = doc->file_name;

    for (auto &it : doc->contents) {
        cur_scope = doc->file_name;
        switch (it->get_type()) {
            case ExpressionType::var_decl_: {
                on_var_decl = true;
                generate_decl(it, false);
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

void CodeGenerator::generate_unop(AbstractExpression *exp)
{
    UnaryExpression *uop = dynamic_cast<UnaryExpression *>(exp);
    if (uop->exp->get_type() != ExpressionType::value_ && uop->exp->get_type() != ExpressionType::index_ 
        && uop->exp->get_type() != ExpressionType::call_ && uop->exp->get_type() != ExpressionType::oper_
        && uop->exp->get_type() != ExpressionType::uop_ && uop->exp->get_type() != ExpressionType::triple_) {
        error_at(__LINE__);
    }

    if (uop->exp->get_type() == ExpressionType::value_) {
        ValueExpression *v =dynamic_cast<ValueExpression *>(uop->exp);
        if (v->valType != 4) {
            error_at(__LINE__);
        }
    }

    GENERATE_OP(uop->exp, DeclType::none_)

    if (uop->op == TokenKind::k_oper_minus) {
        opcodes.push_back((luint8_t)(OpCode::op_minus));
    } else if (uop->op == TokenKind::k_oper_sub_sub) {
        opcodes.push_back((luint8_t)(OpCode::op_dec));
    } else if (uop->op == TokenKind::k_oper_plus_plus) {
        opcodes.push_back((luint8_t)(OpCode::op_inc));
    } else if (uop->op == TokenKind::k_cmp_not) {
        opcodes.push_back((luint8_t)(OpCode::op_cmp_not));
    } else {
        error_at(__LINE__);
    }
}

void CodeGenerator::generate_decl(AbstractExpression *exp, bool gen)
{
    VarDeclExpression *var = dynamic_cast<VarDeclExpression *>(exp);
    if (on_var_decl) {
        lint16_t idx = find_local_idx(var->name->strval, locals[object_name]);
        if (idx >= 0) {
            cout << "redeclare variable: " << var->name->strval << endl;
            error(var->name);
        }

        idx = (lint16_t)locals[object_name].size();
        locals[object_name].push_back({var->is_static, var->dtype, var->name, false, idx, var->is_arr, var->user_define_type});

        if (gen) {
            idx = (lint16_t)locals[object_name].size();
            var_init_codes.push_back((luint8_t)(OpCode::op_load_global));
            LOAD_IDX_2(var_init_codes, idx)
        }
    } else {
        lint16_t idx = find_local_idx(var->name->strval, locals[object_name]);
        if (idx >= 0) {
            cout << "redeclare variable: " << var->name->strval << endl;
            error(var->name);
        }

        vector<Local> &scopeLocals = locals[cur_scope];
        idx = find_local_idx(var->name->strval, scopeLocals);
        if (idx >= 0) {
            cout << "redeclare variable: " << var->name->strval << endl;
            error(var->name);
        }

        idx = (lint16_t)scopeLocals.size();
        scopeLocals.push_back({var->is_static, var->dtype, var->name, false, idx, var->is_arr,var->user_define_type});

        if (gen) {
            opcodes.push_back((luint8_t)(OpCode::op_load_local));
            LOAD_IDX_2(opcodes, idx)
        }
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
    {TokenKind::k_oper_assign, OpCode::op_assign},
    {TokenKind::k_oper_plus_assign, OpCode::op_add_assign},
    {TokenKind::k_oper_minus_assign, OpCode::op_sub_assign},
    {TokenKind::k_oper_mul_assign, OpCode::op_mul_assign},
    {TokenKind::k_oper_div_assign, OpCode::op_div_assign},
    {TokenKind::k_oper_mod_assign, OpCode::op_mod_assign},
    
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
    {TokenKind::k_oper_pointer, OpCode::op_pointor},
};

static std::set<TokenKind> assignSet = { 
    TokenKind::k_oper_assign, 
    TokenKind::k_oper_plus_assign, 
    TokenKind::k_oper_minus_assign, 
    TokenKind::k_oper_mul_assign, 
    TokenKind::k_oper_div_assign,
    TokenKind::k_oper_mod_assign,
};

#define CTOR(exp) \
    ConstructExpression *ctor = dynamic_cast<ConstructExpression *>(exp); \
    lint32_t count = 0; \
    for (auto &it : ctor->body) { \
        GENERATE_OP(it, DeclType::none_) \
        ++count; \
    } \
    \
    const char * idx2char = (const char *)&count; \
    if (ctor->type) { \
        if (count % 2 != 0) { \
            error_at(__LINE__); \
        } \
        opcodes.push_back((luint8_t)(OpCode::op_new_mapping)); \
    } else { \
        opcodes.push_back((luint8_t)(OpCode::op_new_array)); \
    } \
    \
    LOAD_IDX_4(opcodes, count)

// 怎么做常量折叠　？
void CodeGenerator::generate_binary(AbstractExpression *exp)
{
    BinaryExpression *bin = dynamic_cast<BinaryExpression *>(exp);
    vector<Local> &scopeLocals = locals[cur_scope];
    // 条件、赋值
    if (constFold.count(bin->oper)) {
        AbstractExpression *res = calc_const(bin);
        if (res != bin) {
            bin->r = res;
        }
    } else if (assignSet.count(bin->oper) && bin->r->get_type() == ExpressionType::oper_) {
        AbstractExpression *res = calc_const(dynamic_cast<BinaryExpression *>(bin->r));
        if (res != bin->r) {
            bin->r = res;
        }
    }

    AbstractExpression *left = bin->l;
    AbstractExpression *right = bin->r;

    // 变量声明一定是在左边
    if (left->get_type() == ExpressionType::var_decl_) {
        generate_decl(left, true);
    } else {
        // 给标识符、函数调用、下标索引（mapping、array）登赋值的
        if (bin->oper != TokenKind::k_oper_assign && assignSet.count(bin->oper)) {
            if (left->get_type() == ExpressionType::value_){
                ValueExpression *v = dynamic_cast<ValueExpression *>(left);
                if (v->valType != 4) {
                    error_at(__LINE__);
                }
            }

            if (left->get_type() != ExpressionType::value_ && left->get_type() != ExpressionType::index_ 
                && left->get_type() != ExpressionType::call_ && left->get_type() != ExpressionType::oper_
                && left->get_type() != ExpressionType::uop_ && left->get_type() != ExpressionType::triple_) {
                error_at(__LINE__);
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
            CTOR(left)
        } else {
            GENERATE_OP(left, DeclType::none_)
        }
    }

    DeclType dtype = DeclType::none_;
    if (bin->oper == TokenKind::k_oper_pointer) {
        if (left->get_type() == ExpressionType::value_) {
            // h->id, user->GetId(), ""->xx, ""->hello()
            ValueExpression *v = dynamic_cast<ValueExpression *>(left);
            if (v->valType != 2 && v->valType != 4) {
                error_at(__LINE__);
            }

            if (right->get_type() == ExpressionType::value_) {
                ValueExpression *v1 = dynamic_cast<ValueExpression *>(right);
                if (v1->valType != 4) {
                    error_at(__LINE__);
                }

                if (v->valType == 4) {
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

                        opcodes.push_back((luint8_t)(OpCode::op_set_class_field));
                        LOAD_IDX_2(opcodes, idx)
                        return;
                    } else if (loc->type != DeclType::object_) {
                        error_at(__LINE__);
                    }
                }
            }
        }

        dtype = DeclType::user_define_;
    }

    // 右边的类型有：值，
    if (bin->oper != TokenKind::k_oper_assign && (constFold.count(bin->oper) || assignSet.count(bin->oper))) {
        ValueExpression *v = dynamic_cast<ValueExpression *>(right);
        if (v->valType == 3) {
            error_at(__LINE__);
        }
    }

    if (right->get_type() == ExpressionType::construct_) {
        CTOR(right)
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

        opcodes.push_back((luint8_t)(OpCode::op_new_class));
        LOAD_IDX_2(opcodes, idx)
    } else {
        GENERATE_OP(right, dtype)
    }

    if (on_var_decl) {
        var_init_codes.push_back((luint8_t)(op2code[bin->oper]));
    } else {
        opcodes.push_back((luint8_t)(op2code[bin->oper]));
    }
}

void CodeGenerator::generate_if_else(AbstractExpression *exp, lint32_t forContinue, vector<lint32_t> &forBreaks)
{
    IfExpression *ifExp = dynamic_cast<IfExpression *>(exp);
    if (ifExp->exps.empty()) {
        error_at(__LINE__);
    }
    
    lint32_t type0 = 0, type1 = 2, type2 = 0;
    lint32_t goto1 = 0, goto2 = 0;
    const char *idx2char = nullptr;
    vector<lint32_t> *p = nullptr;
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

            lint32_t t = opcodes.size();
            idx2char = (const char *)&t;
            for (int i = 0; i < 4; ++i) {
                opcodes[goto2 + i] = idx2char[i];
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

            lint32_t t = opcodes.size();
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
            // error
        }
    }
}

void CodeGenerator::generate_triple(AbstractExpression *exp)
{
    TripleExpression *tri = dynamic_cast<TripleExpression *>(exp);
    GENERATE_OP(tri->cond, DeclType::none_)
    opcodes.push_back((luint8_t)(OpCode::op_test));
    lint32_t second = opcodes.size() + 4, end = 0;
    const char *idx2char = nullptr;
    LOAD_IDX_4(opcodes, second)

    GENERATE_OP(tri->first, DeclType::none_)

    opcodes.push_back((luint8_t)(OpCode::op_goto));
    end = opcodes.size();
    LOAD_IDX_4(opcodes, end)

    lint32_t t = opcodes.size();
    GENERATE_OP(tri->second, DeclType::none_)

    const char *arr = (const char *)&t;
    for (int i = 0; i < 4; i++) {
        opcodes[second + i] = arr[i];
    }

    t = opcodes.size();
    arr = (const char *)&t;
    for (int i = 0; i < 4; i++) {
        opcodes[end + i] = arr[i];
    }
}

void CodeGenerator::generate_for(AbstractExpression *exp)
{
    ForNormalExpression *forExp = dynamic_cast<ForNormalExpression *>(exp);
    for (auto &it : forExp->inits) {
        if (it->get_type() == ExpressionType::var_decl_) {
            generate_decl(it, false);
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
    }

    vector<lint32_t> *p = nullptr;
    GENERATE_BODY(forExp->body, p)

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
}

void CodeGenerator::generate_foreach(AbstractExpression *exp)
{
    ForeachExpression *fe = dynamic_cast<ForeachExpression *>(exp);

    if (fe->decls.size() > 2) {
        error_at(__LINE__);
    }

    // TODO 后面考虑是否要作用域，目前全都是局部变量
    for (auto &it : fe->decls) {
        if (it->get_type() != ExpressionType::var_decl_) {
            error_at(__LINE__);
        }

        generate_decl(it, false);
    }

    GENERATE_OP(fe->container, DeclType::none_)
    opcodes.push_back((luint8_t)(OpCode::op_foreach_step1));

    lint32_t forContinue = opcodes.size();

    vector<Local> &scopeLocals = locals[cur_scope];
    lint8_t i = 0;
    for (auto &it : fe->decls) {
        VarDeclExpression *var = dynamic_cast<VarDeclExpression *>(it);
        lint16_t idx = find_local_idx(var->name->strval, scopeLocals);
        // 到这里，idx 不应该不存在
        opcodes.push_back((luint8_t)(OpCode::op_load_local));
        LOAD_IDX_2(opcodes, idx)

        // load 起迭代器的第 i 个值
        opcodes.push_back((luint8_t)(OpCode::op_foreach_step2));
        opcodes.push_back((luint8_t)(i++));

        // 给上面的声明的变量赋值
        opcodes.push_back((luint8_t)(OpCode::op_assign));
    }

    vector<lint32_t> forBreaks;
    vector<lint32_t> *p = nullptr;
    GENERATE_BODY(fe->body, p)

    // 跳回赋值处
    opcodes.push_back((luint8_t)(OpCode::op_goto));
    const char *idx2char = (const char *)(&forContinue);
    LOAD_IDX_4(opcodes, forContinue)

    lint32_t t = opcodes.size();
    const char *idxCode = (const char *)&t;
    // Note. 修正跳转的位置信息
    for (auto &it : forBreaks) {
        for (int i = 0; i < 4; ++i) {
            opcodes[it + i] = idxCode[i];
        }    
    }
}

void CodeGenerator::generate_while(AbstractExpression *exp)
{
    whileExpression *w = dynamic_cast<whileExpression *>(exp);
    lint32_t forContinue = opcodes.size();
    vector<lint32_t> forBreaks;
    GENERATE_OP(w->cond, DeclType::none_)
    opcodes.push_back((luint8_t)(OpCode::op_test));
    lint32_t gotoEnd = opcodes.size();

    vector<lint32_t> *p = nullptr;
    GENERATE_BODY(w->body, p)

    // 跳回条件处
    opcodes.push_back((luint8_t)(OpCode::op_goto));
    const char *idx2char = (const char *)(&forContinue);
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
}

void CodeGenerator::generate_do_while(AbstractExpression *exp)
{
    DoWhileExpression *dw = dynamic_cast<DoWhileExpression *>(exp);
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
}

void CodeGenerator::generate_switch_case(AbstractExpression *exp)
{

}

void CodeGenerator::generate_class(AbstractExpression *exp)
{
    ClassExpression *cle = dynamic_cast<ClassExpression *>(exp);

    ClassDecl decl = {cle->is_static, cle->className, (lint16_t)cle->fields.size()};
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
            opcodes.push_back((luint8_t)(OpCode::op_load_global));
        } else {
            loc = &scopeLocals[idx];
            opcodes.push_back((luint8_t)(OpCode::op_load_local));
        }

        if (loc->type != DeclType::mapping_ || !loc->is_arr) {
            error_at(__LINE__);
        }

        LOAD_IDX_2(opcodes, idx)
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

            opcodes.push_back((luint8_t)(OpCode::op_load_iconst)); 
            LOAD_IDX_2(opcodes, idx)
        } else {
            GENERATE_OP(idex->idx1, DeclType::none_)
        }

        opcodes.push_back((luint8_t)(OpCode::op_sub_arr));
    } else {
        if (lhs) {
            opcodes.push_back((luint8_t)(OpCode::op_upset));
        } else {
            opcodes.push_back((luint8_t)(OpCode::op_index));
        }
    }
}

/// @brief h->hello(), ""->hello()
/// @param exp 
void CodeGenerator::generate_call(AbstractExpression *exp, DeclType type)
{
    CallExpression *call = dynamic_cast<CallExpression *>(exp);
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
        }

        if (funIdx < 0) {
            cout << "undefined function: " << id->val.sval->strval << endl;
            exit(-1);
        }
    }

    if (type == DeclType::none_) {
        const Func &f = funcs[id->val.sval->strval];
        // 参数类型检查，查找当前上下文
        // 1、变量类型，2、函数调用返回值类型，4、op操作类型最后的类型
        // 参数数量是否一致
        if (call->params.size() > f.nparams) {
            error_at(__LINE__);
        }
    } 

    int i = 0;
    for (auto &it : call->params) {
        switch (it->get_type()) {
            case ExpressionType::value_: {
                vector<Local> &scopeLocals = locals[cur_scope];
                vector<Local> &callScope = locals[id->val.sval->strval];
                GENERATE_VALUE(it, callScope[i].type)
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
            case ExpressionType::index_: {
                generate_index(it, false);
                break;
            }
            case ExpressionType::call_: {
                CallExpression *argCall = dynamic_cast<CallExpression *>(it);
                ValueExpression *id1 = dynamic_cast<ValueExpression *>(argCall->callee);
                if (!locals.count(id1->val.sval->strval)) {
                    cout << "undefined function: " << id1->val.sval->strval << endl;
                    exit(-1);
                }

                const Func &f1 = funcs[id1->val.sval->strval];
                const Func &f = funcs[id->val.sval->strval];
                if (f1.retType != f.retType) {
                    error_at(__LINE__);
                }

                generate_call(it);
                break;
            }

            default: break;
        }

        ++i;
    }

    lint8_t extArgs = 0;
    if (type == DeclType::user_define_) {
        /* TODO optimize */
        lint16_t idx = find_const<string>(id->val.sval->strval, stringConsts);
        if (idx < 0) {
            idx = intConsts.size();
            stringConsts.push_back(id->val.sval->strval);
        } 
        
        opcodes.push_back((luint8_t)(OpCode::op_load_sconst));
        LOAD_IDX_2(opcodes, idx)

        string call_other = "call_other";
        funIdx = find_fun_idx(call_other, 1);
        if (funIdx < 0) {
            error_at(__LINE__);
        }

        extArgs = 2;
    }

    opcodes.push_back((luint8_t)(OpCode::op_call));
    if (type == DeclType::none_){
        opcodes.push_back((luint8_t)(3));
    } else if (is_sfun) {
        Func &f = sfuns[funIdx];
        if (call->params.size() > f.nparams) {
            error_at(__LINE__);
        }

        opcodes.push_back((luint8_t)(2));
        LOAD_IDX_2(opcodes, funIdx)
    } else {
        opcodes.push_back((luint8_t)(1));
        // 调用的 efun 位置
        LOAD_IDX_2(opcodes, funIdx)

        Func &f = efuns[funIdx];
        if (!f.is_varargs && call->params.size() > f.nparams) {
            error_at(__LINE__);
        }

        opcodes.push_back((luint8_t)(call->params.size() + extArgs));
    }
}

void CodeGenerator::generate_return(AbstractExpression *exp)
{
    ReturnExpression *ret = dynamic_cast<ReturnExpression *>(exp);
    if (ret->ret) {
        Func &fun = funcs[cur_scope];
        GENERATE_OP(ret->ret, fun.retType)

        if (ret->ret->get_type() == ExpressionType::new_) {
            // TODO
        }
    }

    opcodes.push_back((luint8_t)(OpCode::op_return));
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
    if (pre_decl_funcs.count(funDecl->name->strval)) {
        scope = &locals[funDecl->name->strval];
        preDef = true;
    } else {
        locals[funDecl->name->strval] = scopeLocals;
        scope = &locals[funDecl->name->strval];
        funcs[funDecl->name->strval] = {funDecl->is_static, funDecl->is_varargs, funDecl->name, funDecl->returnType, funDecl->user_define_type, (luint16_t)funDecl->params.size()};
    }

    Func &f = funcs[funDecl->name->strval];
    if (!preDef) {
        lint16_t idx = 0;
        f.fromPc = opcodes.size();
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
                    generate_decl(it, false);
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
                    // generate_do_while(it)
                    break;
                }

                case ExpressionType::return_: {
                    if (funDecl->returnType == DeclType::void_) {
                        error_at(__LINE__);
                    }
                    generate_return(it);
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

    f.toPc = opcodes.size() - 1;
    f.nlocals = scope->size();
    funcs[funDecl->name->strval] = f;
}

extern string get_cwd();

static void recurve_mkdir(const string &file)
{
    string cwd = get_cwd();
    luint32_t idx = 0;
    for (;;) {
#ifdef WIN32
#else
    luint32_t pos = file.find("/", idx);
    if (pos == string::npos) {
        break;
    }

    string dir = cwd + "/" + file.substr(0, pos);
    if (access(dir.c_str(), 0) == -1) {
        if (mkdir(dir.c_str(), 0777)) //如果不存在就用mkdir函数来创建
        {
            printf("creat dir failed!!!\n");
            exit(-1);
        }
    }

    idx = pos + 1;
#endif
    }
}

void CodeGenerator::dump()
{
    luint32_t idx = object_name.find_last_of(".");
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

    /*string name = object_name.substr(0, idx);
    luint32_t sz = name.size();
    out.write((char *)&sz, 4);
    out.write(name.c_str(), sz);*/

    bool is_static = false;
    luint32_t sz = this->funcs.size();
    out.write((char *)&sz, 4);
    for (auto &it : funcs) {
        sz = it.first.size();
        out.write((char *)&sz, 4);
        out.write(it.first.c_str(), sz);
        is_static = it.second.is_static;

        out.write((char *)&is_static, 1);
        out.write((char *)&it.second.nparams, 2);
        out.write((char *)&it.second.nlocals, 2);
        out.write((char *)&it.second.fromPc, 4);
        out.write((char *)&it.second.toPc, 4);

        // TODO closure
    }

    sz = this->locals[object_name].size();
    out.write((char *)&sz, 4);
    for (auto &it : locals[object_name]) {
        is_static = it.is_static;
        out.write((char *)&is_static, 1);
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
            is_static = it.is_static;
            out.write((char *)&is_static, 1);
            out.write((char *)&it.nfields, 4);
        }
    }

    sz = this->var_init_codes.size();
    out.write((char *)&sz, 4);
    out.write(var_init_codes.data(), sz);

    sz = this->opcodes.size();
    out.write((char *)&sz, 4);
    out.write(opcodes.data(), sz);
}
