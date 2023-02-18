#include <iostream>
#include <set>

#include "parser.h"

static void error()
{

}

static void error_at()
{

}

static string build_include_path(const string &headerName)
{
    const string &cwd = get_cwd();
    string res = cwd + "/" + headerName;
    std::cout << res << '\n';
    return res;
}

void Parser::init_base_include(const vector<string> &includes)
{
    for (auto &it : includes) {
        incs[it] = parse_file(it.c_str());
    }
}

static MacroParam * read_macro_params(Token *tok, Token *end)
{
    if (!tok || tok->is_space ||tok->kind != TokenKind::k_symbol_qs1) {
        if (end) end->next = tok->next;
        return NULL;
    }

    tok = tok->next;
    MacroParam head{};
    MacroParam *cur = nullptr;
    while (tok)
    {
        if (tok->kind != TokenKind::k_identity) {
            abort();
            break;
        }

        MacroParam *param = new MacroParam;
        param->name = tok->strval;

        if (!head.next) head.next = param;

        if (!cur) cur = param;
        else {
            cur->next = param;
            cur = param;
        }

        tok = tok->next;

        if (tok->kind == TokenKind::k_symbol_qs2) break;

        if (tok->kind != TokenKind::k_symbol_sep) {
            abort();
            break;
        }

        tok = tok->next;
    }

    if (!tok || tok->kind != TokenKind::k_symbol_qs2) {
        abort();
        return NULL;
    }

    if (end) end->next = tok->next;

    return head.next;
}
static Token * copy_macro(Token *mt, unordered_map<string, MacroArg*> *args)
{
    Token *ap = NULL;
    Token *head = NULL;

    // try copy
    while (mt) {
        Token *toCopy = nullptr;
        Token *tmp = nullptr;
        // TODO
        if (mt->kind == TokenKind::k_identity && args && args->count(mt->strval)) {
            tmp = copy_macro((*args)[mt->strval]->tok, nullptr);
            toCopy = tmp;
        }
        else toCopy = mt;

        if (tmp) {
            if (!ap) {
                ap = tmp->origin;
                head = tmp;
            }
            else {
                ap->next = tmp;
                ap = tmp->origin;
            }

            head->origin = ap;
            mt = mt->next;
            continue;
        }

        Token *t = new Token;
        *t = *toCopy;
        t->next = nullptr;
        t->is_space = mt->is_space;
        t->newline = mt->newline;

        if (!ap) {
            ap = t;
            head = t;
        }
        else {
            ap->next = t;
            ap = t;
        }
        
        head->origin = ap;
        mt = mt->next;
    }

    return head;
}

static void read_macro_args(MacroArg *arg, Token *tok, Token *end, string &q)
{
    Token *list = nullptr;
    Token *cur = nullptr;
    while (tok) {
        // TODO 读取一个参数
        // ""->load(uer, ss)
        if (tok->kind == TokenKind::k_symbol_sep && q.size() == 1) {
            break;
        }

        if (tok->kind == TokenKind::k_symbol_qs2) {
            if (q.size() > 1) q.pop_back();
            else break;
        }
        else if (tok->kind == TokenKind::k_symbol_qs1) q.push_back('(');

        Token *t = new Token;
        *t = *tok;
        t->next = nullptr;

        if (!list) {
            list = t;
            cur = t;
        }
        else {
            cur->next = t;
            cur = t;
        }
        
        if (q.size() >= 1) tok = tok->next;
    }

    if (!list) abort();

    end->next = tok;
    arg->tok = list;
}

void Parser::enter_dir()
{
    
}

void Parser::exit_dir()
{

}

Token * Parser::preprocessing(Token *tok)
{
    Token *h = nullptr;
    Token *cur = tok;
    Token *pre = nullptr;
    Token *pre1 = nullptr;

    while (cur) {
        if (cur->kind == TokenKind::k_identity) {
            // 替换原来的Token，copy macro 然后链接起来
            if (macros.count(cur->strval)) {
                Macro *macro = macros[cur->strval];
                // built in macro
                if (macro->handler) {
                    macro->handler(cur);
                    continue;
                }

                unordered_map<string, MacroArg*> args;
                int i = 0;
                // 入参
                if (cur->next && cur->next->kind == TokenKind::k_symbol_qs1) {
                    // TODO 读取入参
                    Token *at = cur;
                    at = at->next;
                    string q;
                    
                    while (at) {
                        // hello("", 100, 200, a, b)
                        if (at->kind == TokenKind::k_symbol_qs1) {
                            if (q.empty()) {
                                at = at->next;
                                q.push_back('(');
                            }
                        }
                        
                        MacroArg *arg = new MacroArg;
                        arg->tok = nullptr;
                        Token tmp{};
                        read_macro_args(arg, at, &tmp, q);

                        at = tmp.next;

                        if (!at) {
                            cout << "读取宏入参错误\n";
                            exit(-1);
                        }

                        if ((at->kind == TokenKind::k_symbol_sep || at->kind == TokenKind::k_symbol_qs2) && q.size() == 1) {
                            MacroParam *p = macro->params;
                            for (int c = i; c > 0; --c) {
                                p = p->next;
                                if (!p) {
                                    cout << "宏定义参数数量与传入参数数量不匹配! \n";
                                    exit(-1);
                                }
                            }

                            args[p->name] = arg;
                            
                            if (at->kind == TokenKind::k_symbol_sep) at = at->next;
                            ++i;
                        }
                        
                        if (at->kind == TokenKind::k_symbol_qs2) q.pop_back();
                        if (q.empty()) break;
                    }

                    cur = at;
                }

                // replace and insert body, and chain them
                if (!pre) {
                    abort();
                }

                Token *h = copy_macro(macro->body, &args);
                pre->next = h;
                h->origin->next = cur->next;
            }
        }
        else if (pre && pre->kind == TokenKind::k_symbol_no && cur->kind == TokenKind::k_key_word_define) {
                // read params and body and insert new macro

                // 名称
                cur = cur->next;
                Token *macroName = nullptr;
                if (cur->kind != TokenKind::k_identity) {
                    abort();
                }

                macroName = cur;
                if (macros.count(macroName->strval)) {
                    cout << "redefine macro: " << macroName->strval << "\n";
                    exit(-1);
                }

                if (!cur->is_space) cur = cur->next;

                Macro *m = new Macro;
                m->handler = nullptr;

                Token tmp{};
                MacroParam *params = read_macro_params(cur, &tmp);
                if (!params) {  // object like
                    m->isobj_like = true;
                }

                cur = tmp.next;
                
                Token *from = nullptr;
                Token *c = nullptr;

                while (cur) {
                    Token *t = new Token;
                    *t = *cur;
                    t->next = nullptr;

                    if (!c) {
                        from = t;
                        c = t;
                    }
                    else {
                        c->next = t;
                        c = t;
                    }

                    if (cur->newline) {
                        if (cur->kind == TokenKind::k_symbol_next) {
                            cur = cur->next;
                            continue;
                        }

                        c->newline = false;
                        cur->newline = true;
                        break;
                    }

                    cur = cur->next;
                }

                m->body = from;
                m->params = params;
                macros[macroName->strval] = m;

                if (pre1) {
                    pre1->next = cur;
                } else {
                    h = cur->next;
                }
            }
            else if (cur->kind == TokenKind::k_key_word_include) {
                // TODO
                if (pre->kind != TokenKind::k_symbol_no || !(cur->next && cur->next->kind == TokenKind::k_string)) {
                    goto proc;
                }

                Token *ap = nullptr;
                if (!incs.count(cur->strval)) {
                    Token *t = parse_file(build_include_path(cur->next->strval).c_str());
                    if (!t) {
                        abort();
                    }

                    t = preprocessing(t);
                    if (!t) {
                        abort();
                    }

                    incs[cur->next->strval] = t;
                }

                ap = incs[cur->next->strval];

                Token *head = copy_macro(ap, nullptr);
                Token *apt = cur->next->next;
                Token *end = head->origin;

                delete pre;
                delete cur->next;
                delete cur;

                if (pre1) {
                    pre1->next = head;
                }
                else h = head;
                
                end->next = apt;
                cur = apt;
            }
    proc:
        pre1 = pre;
        pre = cur;
        cur = cur->next;
    }

    return h ? h : tok;
}

Token * Parser::parse_file(const char *filename)
{
    Scanner *sc = new Scanner;
    sc->set_file(filename);
    TokenReader *reader = new TokenReader;
    reader->set_scanner(sc);

    while (!reader->is_eof()) {
        Token * t = reader->next();
        if (!t && !reader->is_eof()) {
            Token *cur = reader->get_head();
            while (cur) {
                if (cur->kind == TokenKind::k_string) cout << '"';
                cout << cur->strval;
                if (cur->kind == TokenKind::k_string) cout << '"';

                if (cur->is_space) cout << ' ';
                if (cur->newline) cout << '\n';
                cur = cur->next;
            }

            std::cout << "unexpected!!!\n";
            abort();
            break;
        }
    }

    Token *res = reader->get_head();
    reader->get_cur()->next = nullptr;
    delete sc;
    delete reader;

    return res;
}

// 用于跳过作用域，尽可能发现更多错误
static void skip(Token *tok, Token *t)
{

}

static void require_expect(Token *tok, Token *t, string exp)
{
    if (tok->kind != TokenKind::k_string) {
        error();
    }

    if (tok->strval != exp) {
        error();
    }

    // consume
    t->next = tok->next;
}

static AbstractExpression * parse_call(Token *tok, Token *t)
{
    return nullptr;
}

static AbstractExpression * parse_index(Token *tok, Token *t)
{
    return nullptr;
}

static AbstractExpression * parse_return(Token *tok, Token *t)
{
    return nullptr;
}

static AbstractExpression * parse_triple(Token *tok, Token *t)
{
    return nullptr;
}

static AbstractExpression * parse_switch_case(Token *tok, Token *t)
{
    return nullptr;
}

static AbstractExpression * parse_foreach(Token *tok, Token *t)
{
    return nullptr;
}

static AbstractExpression * parse_for(Token *tok, Token *t)
{
    return nullptr;
}

static AbstractExpression * parse_if_exp(Token *tok, Token *t)
{
    return nullptr;
}

static AbstractExpression * parse_assignment(Token *tok, Token *t)
{
    return nullptr;
}

static AbstractExpression * parse_function_decl(Token *tok, Token *t)
{
    return nullptr;
}

static AbstractExpression * parse_class_decl(Token *tok, Token *t)
{
    return nullptr;
}

static AbstractExpression * parse_var_decl(Token *tok, Token *t)
{
    return nullptr;
}

static AbstractExpression * parse_import(Token *tok, Token *t)
{
    return nullptr;
}

static unordered_map<TokenKind, int> priority_map = {
    {TokenKind::k_oper_assign, 2},
    {TokenKind::k_oper_plus_assign, 2},
    {TokenKind::k_oper_minus_assign, 2},
    {TokenKind::k_oper_mul_assign, 2},
    
    {TokenKind::k_cmp_or, 4},
    {TokenKind::k_cmp_and, 5},

    {TokenKind::k_oper_bin_or, 6},
    {TokenKind::k_oper_bin_and, 7},

    {TokenKind::k_cmp_eq, 8},
    {TokenKind::k_cmp_neq, 8},

    {TokenKind::k_cmp_gt, 10},
    {TokenKind::k_cmp_gte, 10},
    {TokenKind::k_cmp_lt, 10},
    {TokenKind::k_cmp_lte, 10},

    {TokenKind::k_oper_plus, 12},
    {TokenKind::k_oper_minus, 12},

    {TokenKind::k_oper_mul, 13},
    {TokenKind::k_oper_div, 13},
    {TokenKind::k_oper_mod, 13},

    {TokenKind::k_oper_pointer, 14},
};

static AbstractExpression * parse_primary(Token *tok, Token *t)
{
    AbstractExpression *exp = nullptr;
    bool is_static = false;
    TokenKind initType;

start:
    TokenKind k = tok->kind;
    if (k >= TokenKind::k_key_word_int && k <= TokenKind::k_key_word_as) {
        switch (k) {
            case TokenKind::k_key_word_true: {
                ValueExpression *falseVal = new ValueExpression;
                falseVal->valType = 3;
                falseVal->val.ival = 1;
                exp = falseVal;

                require_expect(tok->next, t, ";");
                tok = t->next;
                break;
            }
            case TokenKind::k_key_word_false: {
                ValueExpression *falseVal = new ValueExpression;
                falseVal->valType = 3;
                falseVal->val.ival = 0;
                exp = falseVal;

                require_expect(tok->next, t, ";");
                tok = t->next;
                break;
            }

pri_kw:
            case TokenKind::k_key_word_int: 
            case TokenKind::k_key_word_float: 
            case TokenKind::k_key_word_object: 
            case TokenKind::k_key_word_mapping: 
            case TokenKind::k_key_word_string: 
            case TokenKind::k_key_word_mixed:
            case TokenKind::k_key_word_void:
            case TokenKind::k_key_word_bool: {
                if (!is_static) t->next = tok;

                Token *n = tok->next;
                initType = n->kind;

                if (n) {    // 数组
                    switch (n->kind) {
                        case TokenKind::k_oper_mul:{
                            tok = n;
                            break;
                        }
                        case TokenKind::k_identity:{
                            tok = n;
                            break;
                        }
                    }
                    goto start;
                }
            }

            case TokenKind::k_key_word_class: {
                exp = parse_class_decl(tok, t);
                break;
            }
            case TokenKind::k_key_word_import: {
                exp = parse_import(tok, t);
                break;
            }

            case TokenKind::k_key_word_static:
            case TokenKind::k_key_word_private: {
                is_static = true;
                t->next = tok;
                Token *n = tok->next;
                if (n) {
                    switch (n->kind) {
                        case TokenKind::k_key_word_int: 
                        case TokenKind::k_key_word_float: 
                        case TokenKind::k_key_word_object: 
                        case TokenKind::k_key_word_mapping: 
                        case TokenKind::k_key_word_string: 
                        case TokenKind::k_key_word_mixed:
                        case TokenKind::k_key_word_bool: {
                            tok = n;
                            goto pri_kw;
                        }
                    }
                } else {
                    error();
                }
                break;
            }
        }
    } else if (k == TokenKind::k_identity) {
        Token *n = tok->next;
        if (n) {
            if (n->kind == TokenKind::k_symbol_qs1) {               // (    func
                exp = parse_function_decl(t->next, t);
            } else if (n->kind == TokenKind::k_symbol_qm1) {        // [    index
                exp = parse_index(t->next, t);
            }
        }

        if (!exp) {
            if (initType == TokenKind::k_none) {
                ValueExpression *id = new ValueExpression;
                id->valType = 4;
                id->val.sval = tok;
                exp = id;
            } else {
                exp = parse_var_decl(t->next, t);
            }
        }
    } else if (k == TokenKind::k_integer) {
        ValueExpression *iVal = new ValueExpression;
        iVal->valType = 0;
        iVal->val.ival = tok->ival;
        exp = iVal;

        require_expect(tok->next, t, ";");
        tok = t->next;
    } else if (k == TokenKind::k_number) {
        ValueExpression *dVal = new ValueExpression;
        dVal->valType = 1;
        dVal->val.ival = tok->dval;
        exp = dVal;

        require_expect(tok->next, t, ";");
        tok = t->next;
    } else if (k == TokenKind::k_string) {
        ValueExpression *sVal = new ValueExpression;
        sVal->valType = 2;
        sVal->val.sval = tok;
        exp = sVal;

        require_expect(tok->next, t, ";");
        tok = t->next;
    } else if (k == TokenKind::k_symbol_qm1 || k == TokenKind::k_symbol_qg1) {
        // 数组字面量
        ConstructExpression *con = new ConstructExpression;
        con->type = k == TokenKind::k_symbol_qg1 ? 1 : 0;
        TokenKind endKind = (TokenKind)((int)k + 1);

        Token *cur = tok->next;
        while (cur) {
            if (cur->kind == endKind) {
                break;
            }

            if (cur->kind == TokenKind::k_symbol_sep) {
                // consume
                cur = cur->next;
                continue;
            }

            AbstractExpression *element = parse_primary(cur, t);
            con->body.push_back(element);

            cur = cur->next;
        }
        
        cur = cur->next;
        require_expect(cur, t, ";");
        tok = t->next;
    } else {
        error();
    }

    t->next = tok->next;
    return exp;
}

static AbstractExpression * parse_unary(Token *tok, Token *t)
{
    static set<TokenKind> unary_oper = {
        TokenKind::k_oper_minus, TokenKind::k_oper_plus_plus, TokenKind::k_oper_sub_sub,
    };

    TokenKind k = tok->kind;
    if (unary_oper.count(k)) {
        t->next = tok->next;
        AbstractExpression *exp = parse_primary(tok->next, t);
        UnaryExpression *uExp = new UnaryExpression;
        uExp->op = k;
        uExp->exp = exp;

        return uExp;
    } else {
        AbstractExpression *exp = parse_primary(tok, t);
        tok = t->next;
        k = tok->kind;
        if (k == TokenKind::k_oper_plus_plus || k == TokenKind::k_oper_sub_sub) {
            t->next = tok->next;
            UnaryExpression *uExp = new UnaryExpression;
            uExp->op = k;
            uExp->exp = exp;

            return uExp;
        } else {
            return exp;
        }
    }
}

static int get_pre(TokenKind k)
{
    if (priority_map.count(k)) return priority_map[k];
    else return -1;
}

static AbstractExpression * parse_binay(Token *tok, int pre, Token *cache)
{
    AbstractExpression *exp1 = parse_unary(tok, cache);
    tok = cache->next;
    int tprec = get_pre(tok->kind);
    while (tprec > pre) {
        tok = tok->next;
        AbstractExpression *exp2 = parse_binay(tok->next, tprec, cache);
        if (!exp2) {
            error();
        }

        BinaryExpression *op = new BinaryExpression;
        op->oper = tok->kind;
        op->l = exp1;
        op->r = exp2;

        exp1 = op;
        tok = cache->next;
        tprec = get_pre(tok->kind);
    }

    return exp1;
}

static AbstractExpression * do_parse(Token *tok)
{
    DocumentExpression *doc = new DocumentExpression;
    Token *cur = tok;
    Token t{};
    while (cur) {
        AbstractExpression *exp = parse_primary(cur, &t);
        doc->contents.push_back(exp);

        cur = t.next;
    }

    return doc;
}


// 常量折叠，分支处理，死代码删除，循环无关外提，

ExpressionVisitor * Parser::parse(const char *filename)
{
    Token *tok = parse_file(filename);
    Token *cur = preprocessing(tok);
    tok = cur;
    while (cur) {
        if (cur->kind == TokenKind::k_string) cout << '"';
        cout << cur->strval;
        if (cur->kind == TokenKind::k_string) cout << '"';

        if (cur->is_space) cout << ' ';
        if (cur->newline) cout << '\n';
        cur = cur->next;
    }

    cout << '\n';

    // 正式开始处理
    AbstractExpression *exp = do_parse(tok);
    if (!exp) {
        cout << "[error] parse " << filename << " failed!\n";
        exit(-1);
    }

    return exp;
}
