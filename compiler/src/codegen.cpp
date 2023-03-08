#include <iostream>
#include "codegen.h"
#include "opcode.h"

using namespace std;

#define LOAD_IDX_2(con, idx) const char *idx2char = (const char *)&idx; \
    con.push_back(idx2char[0]); \
    con.push_back(idx2char[1]);

#define LOAD_IDX_4(con, idx) const char *idx2char = (const char *)&idx; \
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
        lint16_t idx = find_local_idx(val->val.sval->strval, scopeLocals); \
        if (idx < 0) { \
            /* TODO undefine identifier */ \
        } \
        \
        const Local &loc = scopeLocals[idx]; \
        if (loc.type != vType) { \
            /* TODO 类型不匹配 */ \
        } \
        \
        opcodes.push_back((luint8_t)(OpCode::op_load_local)); \
        LOAD_IDX_2(opcodes, idx) \
    }

#define GENERATE_OP(exp) \
        if (exp->get_type() == ExpressionType::index_) { \
            generate_index(exp, false); \
        } else if (exp->get_type() == ExpressionType::uop_) { \
            generate_unop(exp); \
        } else if (exp->get_type() == ExpressionType::oper_) { \
            generate_binary(exp); \
        } else if (exp->get_type() == ExpressionType::triple_) { \
            generate_triple(exp); \
        } else if (exp->get_type() == ExpressionType::call_) { \
            generate_call(exp); \
        } else if (exp->get_type() == ExpressionType::value_) { \
            vector<Local> &scopeLocals = locals[cur_scope]; \
            Func &fun = funcs[cur_scope]; \
            GENERATE_VALUE(exp, fun.retType) \
        }

extern void error(Token *tok);

lint16_t CodeGenerator::find_local_idx(const string &name, const std::vector<Local> &vec)
{
    lint16_t idx = 0;
    for (auto &it : vec) {
        if (it.name->strval == name) return idx;
        ++idx;
    }

    return -1;
}

void CodeGenerator::generate(AbstractExpression *exp)
{
    DocumentExpression *doc = dynamic_cast<DocumentExpression *>(exp);
    locals[doc->file_name] = {};
    
    cur_scope = doc->file_name;

    for (auto &it : doc->contents) {
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
    
}

void CodeGenerator::generate_decl(AbstractExpression *exp, bool fromOp)
{
    VarDeclExpression *var = dynamic_cast<VarDeclExpression *>(exp);
    vector<Local> &scopeLocals = locals[cur_scope];
    lint16_t idx = find_local_idx(var->name->strval, scopeLocals);
    if (idx >= 0) {
        cout << "redeclare variable: " << var->name->strval << endl;
        error(var->name);
    }

    idx = (lint16_t)scopeLocals.size();
    scopeLocals.push_back({var->is_static, var->dtype, var->name, false, idx, var->is_arr});
    if (fromOp) {
        if (on_var_decl) {
            var_init_codes.push_back((luint8_t)(OpCode::op_load_global));
        } else {
            opcodes.push_back((luint8_t)(OpCode::op_load_local));
        }

        LOAD_IDX_2(var_init_codes, idx)
    }
}

static void calc_const()
{

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

// 怎么做常量折叠　？
void CodeGenerator::generate_binary(AbstractExpression *exp)
{
    BinaryExpression *bin = dynamic_cast<BinaryExpression *>(exp);
    AbstractExpression *left = bin->l;
    AbstractExpression *right = bin->r;

    // 变量声明一定是在左边
    if (left->get_type() == ExpressionType::var_decl_) {
        generate_decl(left, true);
    } else {
        // 给标识符、函数调用、下标索引（mapping、array）登赋值的
        if (left->get_type() == ExpressionType::index_) {
            generate_index(left, true);
        } else if (left->get_type() == ExpressionType::call_) {
            generate_call(left);
        } else {
            // TODO 
        }
    }

    if (on_var_decl) {
        var_init_codes.push_back((luint8_t)(op2code[bin->oper]));
    } else {
        opcodes.push_back((luint8_t)(op2code[bin->oper]));
    }

    // 右边的类型有：值，
    switch (right->get_type())
    {
    case ExpressionType::oper_:{
        generate_binary(right);
        break;
    }
    case ExpressionType::uop_:{
        generate_binary(right);
        break;
    }
    case ExpressionType::call_:{
        generate_binary(right);
        break;
    }
    case ExpressionType::index_:{
        generate_binary(right);
        break;
    }
    default:
        break;
    }

}

static void generate_body(CodeGenerator &generator, vector<AbstractExpression *> body, vector<lint32_t> &forBreak, lint32_t &forContinue, const string &cur_scope)
{
    
}

void CodeGenerator::generate_if_else(AbstractExpression *exp, lint32_t forContinue, vector<lint32_t> &forBreaks)
{

}

void CodeGenerator::generate_triple(AbstractExpression *exp)
{

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
            // TODO error
        }
    }

    lint32_t forContinue = opcodes.size();
    vector<lint32_t> forBreaks;
    if (forExp->conditions.size() > 1) {
        // TODO error
    }

    if (!forExp->conditions.empty()) {
        AbstractExpression *cond = forExp->conditions.front();
        GENERATE_OP(cond)
    }

    for (auto &it : forExp->body) {
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
                // 里面 break、continue
                generate_if_else(it, forContinue, forBreaks);
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
            case ExpressionType::break_: {
                push_code((luint8_t)(OpCode::op_goto));
                forBreaks.push_back(opcode_size());
                break;
            }
            case ExpressionType::continue_: {
                push_code((luint8_t)(OpCode::op_goto));
                LOAD_IDX_4(codes(), forContinue)
                break;
            }
            case ExpressionType::switch_case_: {
                // generate_do_while(it)
                break;
            }

            case ExpressionType::return_: {
                Func *fun = &get_funcs()[cur_scope];
                if (fun->retType == DeclType::void_) {
                    // TODO error
                }
                generate_return(it);
                break;
            }
            default: {
                // TODO report error
                break;
            }
        }
    }

    lint32_t forBreak = opcodes.size();
    const char *idxCode = (const char *)(&forBreak);

    // Note. 修正跳转的位置信息
    for (auto &it : forBreaks) {
        for (int i = 0; i < 4; ++i) {
            opcodes[it + i] = idxCode[i];
        }    
    }
}

void CodeGenerator::generate_foreach(AbstractExpression *exp)
{

}

void CodeGenerator::generate_while(AbstractExpression *exp)
{

}

void CodeGenerator::generate_do_while(AbstractExpression *exp)
{

}

void CodeGenerator::generate_switch_case(AbstractExpression *exp)
{

}

void CodeGenerator::generate_class(AbstractExpression *exp)
{

}

void CodeGenerator::generate_index(AbstractExpression *exp, bool lhs)
{
    IndexExpression *idex = dynamic_cast<IndexExpression *>(exp);
    // id, call, index, 
    if (idex->l->get_type() == ExpressionType::value_) {
        ValueExpression *val = dynamic_cast<ValueExpression *>(idex->l);
        if (val->valType != 4) {
            // TODO error
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
            // TODO error
        }

        LOAD_IDX_2(opcodes, idx)
    } else if (idex->l->get_type() == ExpressionType::call_) {
        generate_call(idex->l);
    } else if (idex->l->get_type() == ExpressionType::index_) {
        // 多重 index 
        generate_index(idex->l, false);
    } else {
        // TODO error
    }

    GENERATE_OP(idex->idx)
    if (idex->idx1) {
        if (lhs) {
            // TODO error
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
            GENERATE_OP(idex->idx1)
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

void CodeGenerator::generate_call(AbstractExpression *exp)
{
    CallExpression *call = dynamic_cast<CallExpression *>(exp);
    
    ValueExpression *id = dynamic_cast<ValueExpression *>(call->callee);
    if (id->valType != 4) {
        // TODO
    }

    // 函数没有声明
    if (!locals.count(id->val.sval->strval)) {
        cout << "undefined function: " << id->val.sval->strval << endl;
        exit(-1);
    }

    const Func &f = funcs[id->val.sval->strval];
    vector<Local> &callScope = locals[id->val.sval->strval];
    vector<Local> &scopeLocals = locals[cur_scope];

    // 参数类型检查，查找当前上下文
    // 1、变量类型，2、函数调用返回值类型，4、op操作类型最后的类型
    // 参数数量是否一致
    int i = 0;
    for (auto &it : call->params) {
        switch (it->get_type()) {
            case ExpressionType::value_: {
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
                if (f1.retType != f.retType) {
                    // TODO 
                }

                generate_call(it);
                break;
            }

            default: break;
        }

        ++i;
    }
}

void CodeGenerator::generate_return(AbstractExpression *exp)
{
    ReturnExpression *ret = dynamic_cast<ReturnExpression *>(exp);
    if (ret->ret) {
        GENERATE_OP(ret->ret)

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

    vector<Local> scopeLocals;
    vector<Local> *scope = &scopeLocals;
    bool preDef = false;
    if (pre_decl_funcs.count(funDecl->name->strval)) {
        scope = &locals[funDecl->name->strval];
        preDef = true;
    } else {
        funcs[funDecl->name->strval] = {funDecl->is_static, funDecl->is_varargs, funDecl->name, funDecl->returnType, funDecl->user_define_type, (luint16_t)funDecl->params.size()};
    }

    Func &f = funcs[funDecl->name->strval];
    if (!preDef) {
        lint16_t idx = 0;
        f.fromPc = opcodes.size() - 1;
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
                        // TODO error
                    }
                    generate_return(it);
                    break;
                }
                default: {
                    // TODO report error
                    break;
                }
            }
        }
    } else {
        pre_decl_funcs.insert(funDecl->name->strval);
    }

    f.toPc = opcodes.size() - 1;
    f.nlocals = scope->size();
    locals[funDecl->name->strval] = *scope;
    funcs[funDecl->name->strval] = f;
}

void CodeGenerator::dump()
{

}
