#include <iostream>
#include <set>

#include "ast.h"
#include "parser.h"
#include "token.h"

static void error()
{
    cout << "error found!\n";
    exit(-1);
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
        if (end) end->ival++;
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
    Token *last = nullptr;

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
                if (!pre || i != macro->psize) {
                    error();
                }

                Token *h = copy_macro(macro->body, &args);
                pre->next = h;
                h->origin->next = cur->next;
            }
        }
        else if (pre && pre->kind == TokenKind::k_symbol_no && cur->kind == TokenKind::k_key_word_define) {
                // read params and body and insert new macro
                Token *tok1 = pre;
                int state = 1;
                while (tok1) {
                    if (state == 0) break;

                    if (state == 2) {
                        Token *macroName = nullptr;
                        if (tok1->kind != TokenKind::k_identity) {
                            error();
                        }

                        macroName = tok1;
                        if (macros.count(macroName->strval)) {
                            cout << "redefine macro: " << macroName->strval << "\n";
                            exit(-1);
                        }

                        if (!tok1->is_space) tok1 = tok1->next;

                        Macro *m = new Macro;
                        m->handler = nullptr;

                        Token tmp{};
                        MacroParam *params = read_macro_params(tok1, &tmp);
                        m->psize = tmp.ival;
                        if (!params) {  // object like
                            m->isobj_like = true;
                        }

                        tok1 = tmp.next;
                        
                        Token *from = nullptr;
                        Token *c = nullptr;

                        while (tok1) {
                            Token *t = new Token;
                            *t = *tok1;
                            t->next = nullptr;

                            if (!c) {
                                from = t;
                                c = t;
                            }
                            else {
                                c->next = t;
                                c = t;
                            }

                            if (tok1->newline) {
                                if (tok1->kind == TokenKind::k_symbol_next) {
                                    tok1 = tok1->next;
                                    continue;
                                }

                                c->newline = false;
                                tok1->newline = true;
                                break;
                            }

                            tok1 = tok1->next;
                        }

                        m->body = from;
                        m->params = params;
                        macros[macroName->strval] = m;

                        state = 0;
                        tok1 = tok1->next;

                        if (!tok1) break;
                    }
                    
                    if (tok1->kind == TokenKind::k_symbol_no) {
                        state = 1;
                    } 

                    if (tok1->kind == TokenKind::k_key_word_define) {
                        if (state != 1) {
                            error();
                        }

                        state = 2;
                    }

                    if (state) tok1 = tok1->next;
                }

                if (pre1) {
                    pre1->next = tok1;
                } else {
                    h = tok1;
                }
                
                cur = tok1;
                continue;
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

#define CHECK_TYPE bool is_static = false; \
    if (tok->kind == TokenKind::k_key_word_static || tok->kind == TokenKind::k_key_word_private) { \
        is_static = true; \
        tok = tok->next; \
    } \
    if (!types.count(tok->kind)) { \
        error(); \
    } \
    DeclType retType = types[tok->kind]; \
    Token *user_define_type = retType == DeclType::user_define_ ? tok : nullptr; \
    tok = tok->next; \
    bool is_arr = false; \
    if (tok->kind == TokenKind::k_oper_mul) { \
        is_arr = true; \
        tok = tok->next; \
    } \
    if (tok->kind != TokenKind::k_identity) { \
        error(); \
    } \
    const string &name = tok->strval; \
    if (name.empty()) { \
        error(); \
    } \
    Token *nameToken = tok; \

static bool parse_multi_decl(vector<AbstractExpression *> &contents, AbstractExpression *exp, Token *t);

#define READ_BODY(exp, tok) tok = tok->next; t->next = tok;\
            while (tok) { \
                if (tok->kind == TokenKind::k_symbol_qg2) { \
                    break; \
                } \
                \
                AbstractExpression *element = parse_binay(tok, -1, t); \
                if (!element) { \
                    error(); \
                } \
                tok = t->next; \
                if (element->get_type() == ExpressionType::oper_ || element->get_type() == ExpressionType::var_decl_) { \
                    bool detect = parse_multi_decl(exp->body, element, t); \
                    if (detect) { \
                        tok = t->next->next; \
                        continue; \
                    } \
                } \
                if (element->get_type() >= ExpressionType::var_decl_ && element->get_type() <= ExpressionType::new_) { \
                    require_expect(t->next, t, ";"); \
                    tok = t->next; \
                }\
                exp->body.push_back(element); \
            }

// 用于跳过作用域，尽可能发现更多错误
static void skip(Token *tok, Token *t)
{

}

static void require_expect(Token *tok, Token *t, string exp)
{
    Token *cur = tok;
    while (cur) {
        if (cur->strval != exp) {
            break;
        }

        cur = cur->next;
    }

    // must be match at least one token
    if (cur == tok) {
        error();
    }

    // consume
    t->next = cur;
}

static AbstractExpression * parse_binay(Token *tok, int pre, Token *cache);

static AbstractExpression * parse_call(Token *tok, Token *t)
{
    if (tok->kind != TokenKind::k_symbol_qs1) {
        error();
    }

    CallExpression *call = new CallExpression;
    //call->callee = callee;

    tok = tok->next;
    while (tok) {
        if (tok->kind == TokenKind::k_symbol_qs2) {
            break;
        }

        if (tok->kind == TokenKind::k_symbol_sep) {
            tok = tok->next;
            continue;
        }
    
        AbstractExpression *param = parse_binay(tok, -1, t);
        if (!param) {
            error();
        }

        // TODO check is call, value, iden, index, binary, unary, triple exp
        call->params.push_back(param);
        tok = t->next;
    }

    t->next = tok->next;
    return call;
}

static AbstractExpression * parse_index(Token *tok, Token *t)
{
    if (tok->kind != TokenKind::k_symbol_qm1) {
        error();
    }

    tok = tok->next;
    AbstractExpression *param = parse_binay(tok, -1, t);
    if (!param) {
        error();
    }

    tok = t->next;
    if (tok->kind != TokenKind::k_symbol_qm2) {
        error();
    }

    IndexExpression *idx = new IndexExpression;
    //idx->l = indexer;
    idx->idx = param;

    t->next = tok->next;
    return idx;
}

static AbstractExpression * parse_return(Token *tok, Token *t)
{
    if (tok->kind != TokenKind::k_key_word_return) {
        error();
    }

    AbstractExpression *r = nullptr;
    if (tok->next) {
        if (tok->next->kind != TokenKind::k_symbol_co) {
            r = parse_binay(tok->next, -1, t);
            if (!r) {
                error();
            }

            tok = t->next;
        } else {
            tok = tok->next;
        }
    } else {
        error();
    }

    ReturnExpression *ret = new ReturnExpression;
    ret->ret = r;
    require_expect(tok, t, ";");
    return ret;
}

static AbstractExpression * parse_triple(Token *tok, Token *t)
{
    if (tok->kind != TokenKind::k_cmp_quetion) {
        error();
    }
    tok = tok->next;

    AbstractExpression *first = parse_binay(tok, -1, t);
    if (!first) {
        error();
    }

    tok = t->next;
    if (!tok || tok->kind != TokenKind::k_symbol_show) {
        error();
    }

    tok = tok->next;
    AbstractExpression *second = parse_binay(tok, -1, t);
    if (!second) {
        error();
    }

    TripleExpression *triple = new TripleExpression;
    triple->first = first;
    triple->second = second;

    return triple;
}

static AbstractExpression * parse_case(Token *tok, Token *t)
{
    return nullptr;
}

static AbstractExpression * parse_switch_case(Token *tok, Token *t)
{
    if (tok->kind != TokenKind::k_key_word_switch) {
        error();
    }

    tok = tok->next;
    if (tok->kind != TokenKind::k_symbol_qg1) {
        error();
    }

    tok = tok->next;
    while (tok) {
        if (tok->kind == TokenKind::k_symbol_qg2 || tok->kind == TokenKind::k_key_word_default) {
            break;
        }



        tok = t->next;
    }

    return nullptr;
}

static void parse_parameter(vector<AbstractExpression *> &params, Token *tok, Token *t)
{
    while (tok) {
        if (tok->kind == TokenKind::k_symbol_qs2) {
            break;
        }

        if (t->ival == -1 && tok->kind == TokenKind::k_key_word_in) {
            break;
        }

        if (tok->kind == TokenKind::k_symbol_sep) {
            tok = tok->next;
            continue;
        }

        AbstractExpression *param = parse_binay(tok, -1, t);
        if (!param) {
            error();
        }

        VarDeclExpression *var = dynamic_cast<VarDeclExpression *>(param);
        if (!var) {
            error();
        }

        if (var->is_static) {
            error();
        }

        params.push_back(param);
        tok = t->next;
    }

    if (t->ival != -1) t->next = tok->next;
    else t->next = tok;
}

static AbstractExpression * parse_foreach(Token *tok, Token *t)
{
    if (tok->kind != TokenKind::k_key_word_foreach) {
        error();
    }

    tok = tok->next;
    if (tok->kind != TokenKind::k_symbol_qs1) {
        error();
    }

    tok = tok->next;
    // foreach (xxx, xxx in xx)
    ForeachExpression *foreach = new ForeachExpression;
    t->ival = -1;
    parse_parameter(foreach->decls, tok, t);
    tok = t->next;
    t->ival = 0;

    if (tok->kind != TokenKind::k_key_word_in) {
        error();
    }

    tok = tok->next;
    AbstractExpression *container = parse_binay(tok, -1, t);
    if (!container) {
        error();
    }

    tok = t->next;
    if (tok->kind != TokenKind::k_symbol_qs2) {
        error();
    }

    tok = tok->next;
    if (tok->kind != TokenKind::k_symbol_qg1) {
        AbstractExpression *op = parse_binay(tok, -1, t);
        if (!op) {
            error();
        }

        if (op->get_type() >= ExpressionType::var_decl_ && op->get_type() <= ExpressionType::new_) {
            require_expect(t->next, t, ";");
            tok = t->next; 
        }
        // TODO check is op
        foreach->body.push_back(op);
    } else {
        READ_BODY(foreach, tok)
    }

    tok = t->next;
    t->next = tok->next;
    return foreach;
}

static AbstractExpression * parse_while(Token *tok, Token *t)
{
    if (tok->kind != TokenKind::k_key_word_while) {
        error();
    }

    tok = tok->next;
    if (!tok || tok->kind != TokenKind::k_symbol_qs1) {
        error();
    }

    AbstractExpression *exp = parse_binay(tok->next, -1, t);
    if (!exp) {
        error();
    }

    tok = t->next;
    if (!tok || tok->next->kind != TokenKind::k_symbol_qs2) {
        error();
    }

    if (!tok->next) {
        error();
    }

    bool skip = false;
    whileExpression *w = new whileExpression;
    if (tok->next->kind != TokenKind::k_symbol_qg1) {
        AbstractExpression *op = parse_binay(tok, -1, t);
        if (!op) {
            error();
        }

        if (op->get_type() >= ExpressionType::var_decl_ && op->get_type() <= ExpressionType::new_) {
            require_expect(t->next, t, ";");
            tok = t->next; 
        }
        w->body.push_back(op);
    } else {
        READ_BODY(w, tok)
        skip = true;
    }

    if (skip) t->next = tok->next;
    else t->next = tok;

    return w;
}

static AbstractExpression * parse_do_while(Token *tok, Token *t)
{
    if (!tok || tok->kind != TokenKind::k_key_word_do) {
        error();
    }

    tok = tok->next;
    if (!tok || tok->kind != TokenKind::k_symbol_qg1) {
        error();
    }

    DoWhileExpression *dw = new DoWhileExpression;

    READ_BODY(dw, tok)

    if (!tok || tok->kind != TokenKind::k_symbol_qg2) {
        error();
    }

    tok = tok->next;
    if (!tok || tok->kind != TokenKind::k_symbol_qs1) {
        error();
    }

    AbstractExpression *cond = parse_binay(tok, -1, t);
    if (!cond) {
        error();
    }

    require_expect(t->next, t, ";");

    return dw;
}

static void for_part(Token *tok, Token *t, ForNormalExpression *for_, int i)
{
    while (tok) {
        if (i == 2 && tok->kind == TokenKind::k_symbol_qs2) {
            break;
        }

        if (tok->kind == TokenKind::k_symbol_co) {
            break;
        }
        
        if (tok->kind == TokenKind::k_symbol_sep) {
            tok = tok->next;
            continue;
        }

        AbstractExpression *init = parse_binay(tok, -1, t);
        if (!init) {
            error();
        }
        // TODO  check op, decl

        if (i == 0) for_->inits.push_back(init);
        else if (i == 1) for_->conditions.push_back(init);
        else if (i == 2) for_->operations.push_back(init);
        else error();

        tok = t->next;
    }

    t->next = tok->next;
}

static AbstractExpression * parse_for(Token *tok, Token *t)
{
    if (tok->kind != TokenKind::k_key_word_for) {
        error();
    }

    tok = tok->next;
    if (tok->kind != TokenKind::k_symbol_qs1) {
        error();
    }

    ForNormalExpression *for_ = new ForNormalExpression;
    tok = tok->next;
    for_part(tok, t, for_, 0);
    for_part(t->next, t, for_, 1);
    for_part(t->next, t, for_, 2);

    bool skip = false;
    tok = t->next;
    if (tok->kind != TokenKind::k_symbol_qg1) {
        AbstractExpression *op = parse_binay(tok, -1, t);
        if (!op) {
            error();
        }

        if (op->get_type() >= ExpressionType::var_decl_ && op->get_type() <= ExpressionType::new_) {
            require_expect(t->next, t, ";");
            tok = t->next; 
        }
        // TODO check is op
        for_->body.push_back(op);
    } else {
        READ_BODY(for_, tok)
        skip = true;
    }

    if (skip) t->next = tok->next;
    else t->next = tok;
    
    return for_;
}

static AbstractExpression * parse_if_exp(Token *tok, Token *t)
{
    if (tok->kind != TokenKind::k_key_word_if) {
        error();
    }

    IfExpression *ifExp = new IfExpression;
    int type = 0;
    bool hasCond = false;
    bool skip = false;
    while (tok) {
        if (tok->kind == TokenKind::k_key_word_if) {
            tok = tok->next;
            if (!tok || tok->kind != TokenKind::k_symbol_qs1) {
                error();
            }

            tok = tok->next;
            hasCond = true;
        }

        if (tok->kind == TokenKind::k_key_word_else) {
            Token *n = tok->next;
            if (n) {
                if (n->kind == TokenKind::k_key_word_if) {
                    type = 1;
                    tok = n;
                    continue;
                } else {
                    type = 2;
                }
            } else {
                error();
            }
        }

        AbstractExpression *cond = nullptr;
        if (hasCond) {
            cond = parse_binay(tok, -1, t);
            if (!cond) {
                error();
            }

            tok = t->next;
            if (tok->kind != TokenKind::k_symbol_qs2) {
                error();
            }
        }
        
        IfExpression::If if_;
        if_.cond = cond;
        if_.type = type;

        tok = tok->next;
        if (tok->kind != TokenKind::k_symbol_qg1) {
            AbstractExpression *op = parse_binay(tok, -1, t);
            if (!op) {
                error();
            }

            if (op->get_type() >= ExpressionType::var_decl_ && op->get_type() <= ExpressionType::new_) {
                require_expect(t->next, t, ";");
            }

            if_.body.push_back(op);
        } else {
            IfExpression::If *p = &if_;
            READ_BODY(p, tok)
            skip = true;
        }

        tok = t->next;
        ifExp->exps.push_back(if_);
        hasCond = false;

        if (tok->kind == TokenKind::k_symbol_qg2) {
            if (tok->next && tok->next->kind != TokenKind::k_key_word_else) {
                break;
            }
        } else if (tok->kind != TokenKind::k_key_word_else) {
            break;
        }

        if (skip) {
            tok = tok->next;
        }
    }

    if (skip) t->next = tok->next;
    else t->next = tok;

    return ifExp;
}

static unordered_map<TokenKind, DeclType> types = {
    {TokenKind::k_key_word_void, DeclType::void_},
    {TokenKind::k_key_word_int, DeclType::int_},
    {TokenKind::k_key_word_float, DeclType::float_},
    {TokenKind::k_key_word_bool, DeclType::bool_},
    {TokenKind::k_key_word_string, DeclType::string_},
    {TokenKind::k_key_word_object, DeclType::object_},
    {TokenKind::k_key_word_mapping, DeclType::mapping_},
    {TokenKind::k_key_word_mixed, DeclType::mixed_},
    {TokenKind::k_key_word_fun, DeclType::func_},
    {TokenKind::k_identity, DeclType::user_define_},
};

static AbstractExpression * parse_function_decl(Token *tok, Token *t)
{
    CHECK_TYPE

    tok = tok->next;
    if (tok->kind != TokenKind::k_symbol_qs1) {
        error();
    }

    FunctionDeclExpression *func = new FunctionDeclExpression;
    func->returnType = retType;
    func->name = nameToken; 
    func->is_static = is_static;
    func->dtype = DeclType::func_;
    func->user_define_type = user_define_type;
    tok = tok->next;
    parse_parameter(func->params, tok, t);

    Token *cur = t->next;
    if (cur->kind == TokenKind::k_symbol_qg1) {
        READ_BODY(func, cur)
    } else {
        // 前置声明
        if (cur->kind != TokenKind::k_symbol_co) {
            error();
        }
    }

    cur = t->next;
    if (cur) t->next = cur->next;
    return func;
}

static AbstractExpression * parse_class_decl(Token *tok, Token *t)
{
    if (tok->kind != TokenKind::k_key_word_class) {
        error();
    }

    tok = tok->next;
    if (!tok || tok->kind != TokenKind::k_identity) {
        error();
    }

    ClassExpression *clazz = new ClassExpression;
    clazz->className = tok;

    tok = tok->next;
    if (!tok || tok->kind != TokenKind::k_symbol_qg1) {
        error();
    }

    tok = tok->next;
    while (tok) {
        if (tok->kind == TokenKind::k_symbol_qg2) {
            break;
        }

        AbstractExpression *item = parse_binay(tok, -1, t);
        if (!item) {
            error();
        }

        require_expect(t->next, t, ";");

        clazz->fields.push_back(item);
        tok = t->next;
    }

    t->next = tok->next;
    return clazz;
}

static AbstractExpression * parse_var_decl(Token *tok, Token *t)
{
    CHECK_TYPE

    VarDeclExpression *decl = new VarDeclExpression;
    decl->dtype = retType;
    decl->name = nameToken;
    decl->is_static = is_static;
    decl->is_arr = is_arr;
    decl->user_define_type = user_define_type;

    t->next = tok;
    return decl;
}

static AbstractExpression * parse_import(Token *tok, Token *t)
{
    return nullptr;
}

static AbstractExpression* parse_follow(AbstractExpression *exp, Token *tok, Token *t);

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

    {TokenKind::k_key_word_or, 14},
    {TokenKind::k_oper_pointer, 14},
};

static AbstractExpression * parse_primary(Token *tok, Token *t)
{
    AbstractExpression *exp = nullptr;
    bool is_static = false;
    TokenKind initType = TokenKind::k_none;

start:
    TokenKind k = tok->kind;
    if (k >= TokenKind::k_key_word_int && k <= TokenKind::k_key_word_fun) {
        switch (k) {
            case TokenKind::k_key_word_true: {
                ValueExpression *falseVal = new ValueExpression;
                falseVal->valType = 3;
                falseVal->val.ival = 1;
                exp = falseVal;
                break;
            }
            case TokenKind::k_key_word_false: {
                ValueExpression *falseVal = new ValueExpression;
                falseVal->valType = 3;
                falseVal->val.ival = 0;
                exp = falseVal;
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

                initType = tok->kind;
                Token *n = tok->next;
                if (n) {    // 数组
                    if (n->kind == TokenKind::k_oper_mul) {
                        tok = n->next;
                    } else if (n->kind == TokenKind::k_identity) {
                        tok = n;
                    } else {
                        error();
                    }
                    goto start;
                }
            }

            case TokenKind::k_key_word_class: {
                exp = parse_class_decl(tok, t);
                tok = nullptr;
                break;
            }
            case TokenKind::k_key_word_import: {
                exp = parse_import(tok, t);
                tok = nullptr;
                break;
            }

            case TokenKind::k_key_word_if: {
                exp = parse_if_exp(tok, t);
                tok = nullptr;
                break;
            }
            case TokenKind::k_key_word_for: {
                exp = parse_for(tok, t);
                tok = nullptr;
                break;
            }
            case TokenKind::k_key_word_foreach: {
                exp = parse_foreach(tok, t);
                tok = nullptr;
                break;
            }

            case TokenKind::k_key_word_while: {
                exp = parse_while(tok, t);
                tok = nullptr;
                break;
            }

            case TokenKind::k_key_word_do: {
                exp = parse_do_while(tok, t);
                tok = nullptr;
                break;
            }

            case TokenKind::k_key_word_new: {
                if (!tok->next || tok->next->kind != TokenKind::k_identity) {
                    error();
                }

                tok = tok->next;
                ValueExpression *id = new ValueExpression;
                id->valType = 4;
                id->val.sval = tok;

                NewExpression *new_ = new NewExpression;
                new_->id = id;
                exp = new_;
                break;
            }

            case TokenKind::k_key_word_return: {
                exp = parse_return(tok, t);
                tok = nullptr;
                break;
            }

            case TokenKind::k_key_word_break: {
                BreakExpression *b = new BreakExpression;
                if (!tok->next || tok->next->kind != TokenKind::k_symbol_co) {
                    error();
                }

                exp = b;
                require_expect(tok->next, t, ";");
                tok = nullptr;
                break;
            }

            case TokenKind::k_key_word_continue: {
                ContinueExpression *c = new ContinueExpression;
                if (!tok->next || tok->next->kind != TokenKind::k_symbol_co) {
                    error();
                }

                exp = c;
                require_expect(tok->next, t, ";");
                tok = nullptr;
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
                        case TokenKind::k_key_word_void:
                        case TokenKind::k_key_word_bool: {
                            tok = n;
                            goto pri_kw;
                        }
                        case TokenKind::k_identity: {
                            initType = tok->kind;
                            if (n->next && n->next->kind == TokenKind::k_oper_mul) {
                                tok = n->next;
                            } else {
                                tok = n;
                            }
                            goto start;
                        }
                    }
                } else {
                    error();
                }
                break;
            }
            default: {
                error();
                break;
            }
        }
    } else if (k == TokenKind::k_identity) {
        Token *n = tok->next;
        if (n && n->kind == TokenKind::k_oper_mul) {
            t->next = tok;
            initType = tok->kind;
            tok = n->next;
        } else {
            if (n->kind == TokenKind::k_identity) {                                 // 变量声明
                initType = tok->kind;
                t->next = tok;

                // hello_t hell()
                if (n->next && n->next->kind == TokenKind::k_symbol_qs1) {
                    tok = n;
                }
            }
        }

        if (initType == TokenKind::k_none) {
            ValueExpression *id = new ValueExpression;
            id->valType = 4;
            id->val.sval = tok;
            
            exp = parse_follow(id, tok->next, t);

            // if (t->next && t->next->kind == TokenKind::k_symbol_co) tok = t->next;
            if (exp != id) tok = nullptr;
        } else {
            if (tok->next && tok->next->kind == TokenKind::k_symbol_qs1) {
                exp = parse_function_decl(t->next, t);
                tok = nullptr;
            } else {
                exp = parse_var_decl(t->next, t);
                tok = t->next;
            }
            //tok = t->next;
        }
    } else if (k == TokenKind::k_integer) {
        ValueExpression *iVal = new ValueExpression;
        iVal->valType = 0;
        iVal->val.ival = tok->ival;
        exp = iVal;
    } else if (k == TokenKind::k_number) {
        ValueExpression *dVal = new ValueExpression;
        dVal->valType = 1;
        dVal->val.ival = tok->dval;
        exp = dVal;
    } else if (k == TokenKind::k_string) {
        ValueExpression *sVal = new ValueExpression;
        sVal->valType = 2;
        sVal->val.sval = tok;
        exp = sVal;
    } else if (k == TokenKind::k_symbol_qm1 || k == TokenKind::k_symbol_qg1) {
        // 数组字面量
        ConstructExpression *con = new ConstructExpression;
        con->type = k == TokenKind::k_symbol_qg1 ? 1 : 0;
        TokenKind endKind = (TokenKind)((int)k + 1);

        tok = tok->next;
        while (tok) {
            if (tok->kind == endKind) {
                break;
            }

            if (tok->kind == TokenKind::k_symbol_sep || tok->kind == TokenKind::k_symbol_show) {
                // consume
                tok = tok->next;
                continue;
            }

            AbstractExpression *element = parse_binay(tok, -1, t);
            if (!element) {
                error();
            }

            con->body.push_back(element);
            tok = t->next;
        }

        exp = con;
    } else if (k == TokenKind::k_symbol_qs1) {
        // (xxx)
        tok = tok->next;
        AbstractExpression *element = parse_binay(tok, -1, t);
        if (!element) {
            error();
        }

        if (t->next->kind != TokenKind::k_symbol_qs2) {
            error();
        }
        
        exp = element;
        tok = t->next;
    } else {
        error();
    }

    if (tok) t->next = tok->next;
    return exp;
}

static AbstractExpression * parse_unary(Token *tok, Token *t)
{
    static set<TokenKind> unary_oper = {
        TokenKind::k_oper_minus, TokenKind::k_oper_plus_plus, TokenKind::k_oper_sub_sub, TokenKind::k_cmp_not,
    };

    TokenKind k = tok->kind;
    if (unary_oper.count(k)) {
        AbstractExpression *exp = parse_primary(tok->next, t);
        UnaryExpression *uExp = new UnaryExpression;
        uExp->op = k;
        uExp->exp = exp;

        return uExp;
    } else {
        AbstractExpression *exp = parse_primary(tok, t);
        tok = t->next;
        if (tok) {
            k = tok->kind;
            if (k == TokenKind::k_oper_plus_plus || k == TokenKind::k_oper_sub_sub) {
                t->next = tok->next;
                UnaryExpression *uExp = new UnaryExpression;
                uExp->op = k;
                uExp->exp = exp;

                return uExp;
            }
        }

        return exp;
    }
}

static int get_pre(TokenKind k)
{
    if (priority_map.count(k)) return priority_map[k];
    else return -1;
}

static AbstractExpression* parse_follow(AbstractExpression *exp, Token *tok, Token *t)
{
    AbstractExpression *last = exp;
    while (tok)
    {
        if (tok->kind == TokenKind::k_symbol_qs1) {
            AbstractExpression *c = parse_call(tok, t);
            if (!c) {
                error();
            }

            CallExpression *call = dynamic_cast<CallExpression *>(c);
            if (!call) {
                error();
            }
            call->callee = last;
            last = call;
        } else if (tok->kind == TokenKind::k_symbol_qm1) {
            AbstractExpression *i = parse_index(tok, t);
            if (!i) {
                error();
            }

            IndexExpression *idx = dynamic_cast<IndexExpression *>(i);
            if (!idx) {
                error();
            }
            idx->l = last;
            last = idx;
        } else if (tok->kind == TokenKind::k_cmp_quetion) {
            AbstractExpression *triple = parse_triple(tok, t);
            if (!triple) {
                error();
            }

            TripleExpression *tp = dynamic_cast<TripleExpression *>(triple);
            if (!tp || !exp) {
                error();
            }
            
            tp->cond = exp;
            last = tp;
        } else {
            break;
        }

        tok = t->next;
    }
    return last;
}

static AbstractExpression * parse_binay(Token *tok, int pre, Token *cache)
{
    AbstractExpression *exp1 = parse_unary(tok, cache);
    tok = cache->next;
    if (!tok) {
        return exp1;
    }

    int tprec = get_pre(tok->kind);
    TokenKind kind = tok->kind;
    while (tprec > pre) {
        tok = tok->next;
        AbstractExpression *exp2 = parse_binay(tok, tprec, cache);
        if (!exp2) {
            error();
        }

        exp2 = parse_follow(exp2, cache->next, cache);

        BinaryExpression *op = new BinaryExpression;
        op->oper = kind;
        op->l = exp1;
        op->r = exp2;

        exp1 = op;
        tok = cache->next;
        tprec = get_pre(tok->kind);
    }

    return exp1;
}

static AbstractExpression * transfer_2_decl(AbstractExpression *dexp, bool is_static, DeclType dtype)
{
    ValueExpression *val = dynamic_cast<ValueExpression *>(dexp);
    if (!val || val->valType != 4) {
        error();
    }

    VarDeclExpression *decl = new VarDeclExpression;
    decl->dtype = dtype;
    decl->name = val->val.sval;
    decl->is_static = is_static;
    decl->is_arr = val->is_arr;

    delete val;

    return decl;
}

static bool parse_multi_decl(vector<AbstractExpression *> &contents, AbstractExpression *exp, Token *t)
{
    if (!exp) return false;

    // 连续声明
    if (t->next && t->next->kind == TokenKind::k_symbol_sep) {
        
        contents.push_back(exp);

        Token *tok = t->next->next; // skip ','
        DeclType dtype = DeclType::none_;
        bool is_static = false, is_arr = false;
        if (exp->get_type() == ExpressionType::oper_) {
            BinaryExpression *bin = dynamic_cast<BinaryExpression *>(exp);
            VarDeclExpression *decl = dynamic_cast<VarDeclExpression *>(bin->l);
            if (!decl || decl->dtype == DeclType::none_) {
                error();
            }

            dtype = decl->dtype;
        } else {
            VarDeclExpression *decl = dynamic_cast<VarDeclExpression *>(exp);
            if (!decl || decl->dtype == DeclType::none_) {
                error();
            }

            dtype = decl->dtype;
        }

        while (tok) {
            if (tok->kind == TokenKind::k_symbol_co) {
                break;
            }

            if (tok->kind == TokenKind::k_symbol_sep) {
                tok = tok->next;
                continue;
            }

            AbstractExpression *dexp = parse_binay(tok, -1, t);
            if (!dexp || (dexp->get_type() != ExpressionType::value_ && dexp->get_type() != ExpressionType::oper_)) {
                error();
            }

            if (dexp->get_type() == ExpressionType::value_) {
                ValueExpression *val = dynamic_cast<ValueExpression *>(dexp);
                if (!val || val->valType != 4) {
                    error();
                }

                VarDeclExpression *decl = new VarDeclExpression;
                decl->dtype = dtype;
                decl->name = val->val.sval;
                decl->is_static = is_static;
                decl->is_arr = val->is_arr;

                dexp = transfer_2_decl(dexp, is_static, dtype);
            } else if (dexp->get_type() == ExpressionType::oper_){
                BinaryExpression *bin = dynamic_cast<BinaryExpression *>(dexp);
                if (!bin) {
                    error();
                }

                bin->l = transfer_2_decl(bin->l, is_static, dtype);
            } else {
                error();
            }

            contents.push_back(dexp);
            tok = t->next;   
        }

        return true;
    }

    return false;
}

static AbstractExpression * do_parse(Token *tok)
{
    DocumentExpression *doc = new DocumentExpression;
    Token *cur = tok;
    Token t{};
    while (cur) {
        AbstractExpression *exp = parse_binay(cur, -1, &t);
        if (!exp) {
            error();
        }
        
        if (exp->get_type() != ExpressionType::oper_ && exp->get_type() != ExpressionType::func_decl_ && exp->get_type() != ExpressionType::var_decl_) {
            cout << "etype: " << (int) exp->get_type() << endl;
            error();
        }

        if (exp->get_type() == ExpressionType::oper_) {
            BinaryExpression *bin = dynamic_cast<BinaryExpression *>(exp);
            if (!bin || bin->l->get_type() != ExpressionType::var_decl_) {
                cout << "type: " << (int)bin->l->get_type() << endl;
                error();
            }
        }

        if (exp->get_type() == ExpressionType::oper_ || exp->get_type() == ExpressionType::var_decl_) {
            bool detect = parse_multi_decl(doc->contents, exp, &t);
            if (detect) {
                cur = t.next->next;
                continue;
            }

            require_expect(t.next, &t, ";");
        }

        if (cur->kind == TokenKind::k_symbol_co) {
            t.next = cur->next;
        }

        cur = t.next;
        doc->contents.push_back(exp);
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
    DocumentExpression *exp = dynamic_cast<DocumentExpression *>(do_parse(tok));
    if (exp->contents.empty()) {
        cout << "[error] parse " << filename << " failed!\n";
        exit(-1);
    }

    return exp;
}
