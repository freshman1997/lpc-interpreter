#include <iostream>
#include <set>
#include <ctime>
#include <cstring>

#ifdef WIN32
#include <io.h>
#else 
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif

#include "ast.h"
#include "parser.h"
#include "token.h"

static void print_space(int n)
{
    for (int i = 0; i < n; ++i) cerr << ' ';
}

void error(Token *tok)
{
    if (!tok) {
        cerr << "unexpected in file end!\n";
        throw "\t An error occured!\n";
    }

    cerr << "\n\t error found near line "<< tok->lineno << ", file: " << tok->filename << "\n";
    Token *cur = tok;
    int len = 10;
    print_space(9);
    while (cur && len) {
        if (cur->kind == TokenKind::k_string) cerr << '"';
        cerr << cur->strval;
        if (cur->kind == TokenKind::k_string) cerr << '"';

        if (cur->is_space || cur->newline) cerr << ' ';
        cur = cur->next;
        --len;
    }
    cerr << '\n';
    print_space(5);
    for (int i = 0; i < 4; ++i) cerr << '~';
    cerr << "^\n";
    throw "\t An error occured!\n";
}

void Parser::init_base_include(const vector<string> &includes)
{
    for (auto &it : includes) {
        incs[it] = parse_file(it.c_str());
    }
}


void Parser::enter_dir()
{
    
}

void Parser::exit_dir()
{

}

void Parser::set_compile_file(const char *filename)
{
    this->cur_file = filename;
}

static void proc_file(Token *pre, Token *tok, Token *temp)
{
    Token *t = new Token;
    t->kind = TokenKind::k_string;
    t->lineno = tok->lineno;
    t->strval = tok->filename ? tok->filename : "unkown";
    if (pre) pre->next = t;
    else temp->next = t;
    t->next = tok->next;
    delete tok;
}

static void proc_line(Token *pre, Token *tok, Token *temp)
{
    Token *t = new Token;
    t->kind = TokenKind::k_integer;
    t->ival = tok->lineno;
    t->lineno = tok->lineno;
    if (pre) pre->next = t;
    else temp->next = t;
    t->next = tok->next;
    delete tok;
}

static std::string GetFormatTime()
{
#ifdef _WIN32
    time_t currentTime;
	currentTime = time(NULL);
    struct tm time_sct;
    localtime_s(&time_sct,&currentTime);
    struct tm *t_tm = &time_sct;
#else
    time_t currentTime;
	time(&currentTime);
	tm* t_tm = localtime(&currentTime);
#endif
	char formatTime[64] = {0};
	snprintf(formatTime, 64, "%04d-%02d-%02d %02d:%02d:%02d", 
							t_tm->tm_year + 1900,
							t_tm->tm_mon + 1,
							t_tm->tm_mday,
							t_tm->tm_hour,
							t_tm->tm_min,
							t_tm->tm_sec);
	return std::string(formatTime);
}

static std::string GetFormatDate()
{
	time_t currentTime;
	time(&currentTime);
	tm* t_tm = localtime(&currentTime);

	char formatDate[64] = {0};
	snprintf(formatDate, 64, "%04d-%02d-%02d", 
							t_tm->tm_year + 1900,
							t_tm->tm_mon + 1,
							t_tm->tm_mday);
	return std::string(formatDate);
}

static void proc_time(Token *pre, Token *tok, Token *temp)
{
    Token *t = new Token;
    t->kind = TokenKind::k_string;
    t->strval = GetFormatTime();
    t->lineno = tok->lineno;
    if (pre) pre->next = t;
    else temp->next = t;
    t->next = tok->next;
    delete tok;
}

static void proc_date(Token *pre, Token *tok, Token *temp)
{
    Token *t = new Token;
    t->kind = TokenKind::k_string;
    t->strval = GetFormatDate();
    t->lineno = tok->lineno;
    if (pre) pre->next = t;
    else temp->next = t;
    t->next = tok->next;
    delete tok;
}

static void proc_func(Token *pre, Token *tok, Token *t)
{

}

void Parser::add_built_in_macro()
{
    static Macro file = {"", true, nullptr, 0, nullptr, nullptr, proc_file};
    builtInMacro["__FILE__"] = &file;

    static Macro line = {"", true, nullptr, 0, nullptr, nullptr, proc_line};
    builtInMacro["__LINE__"] = &line;

    static Macro time = {"", true, nullptr, 0, nullptr, nullptr, proc_time};
    builtInMacro["__TIME__"] = &time;

    static Macro date = {"", true, nullptr, 0, nullptr, nullptr, proc_date};
    builtInMacro["__DATE__"] = &date;

    static Macro func = {"", true, nullptr, 0, nullptr, nullptr, proc_func};
    builtInMacro["__FUNC__"] = &func;
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
static Token * copy_macro(Token *mt, unordered_map<string, MacroArg*> *args, int lineno)
{
    Token *ap = NULL;
    Token *head = NULL;

    // try copy
    while (mt) {
        Token *toCopy = nullptr;
        Token *tmp = nullptr;
        // TODO
        if (mt->kind == TokenKind::k_identity && args && args->count(mt->strval)) {
            tmp = copy_macro((*args)[mt->strval]->tok, nullptr, lineno);
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
        if (lineno) t->lineno = lineno;

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

static void free_macro(Macro *m)
{
    MacroParam *p = m->params;
    while (p) {
        MacroParam *p1 = p;
        p = p->next;
        delete p1;
    }

    Token *cur = m->body;
    while (cur) {
        Token *t = cur;
        cur = cur->next;
        delete t;
    }
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

pair<bool, Macro *> Parser::find_macro(const string &id)
{
    if (macros.count(id)) return {true, macros[id]};
    else if (builtInMacro.count(id)) return {true, builtInMacro[id]};
    else return {false, nullptr};
}

// TODO #if #endif #ifndef #ifdef

Token * Parser::preprocessing(Token *tok)
{
    Token *h = nullptr;
    Token *cur = tok;
    Token *pre = nullptr;
    Token *pre1 = nullptr;

    while (cur) {
        if (cur->kind == TokenKind::k_identity) {
            // 替换原来的Token，copy macro 然后链接起来
            pair<bool, Macro *> p = find_macro(cur->strval);
            if (p.first) {
                Macro *macro = p.second;
                // built in macro
                if (macro->handler) {
                    Token temp{};
                    macro->handler(pre, cur, &temp);
                    if (!pre) {
                        h = temp.next;
                        cur = h;
                    } else {
                        cur = pre->next;
                    }
                    goto proc;
                }

                unordered_map<string, MacroArg*> args;
                int i = 0, lineno = cur->lineno + 1;
                // 入参
                if (cur->next && cur->next->kind == TokenKind::k_symbol_qs1) {
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
                    error(pre);
                }

                Token *h1 = copy_macro(macro->body, &args, lineno);
                if (pre) pre->next = h1;
                h1->origin->next = cur->next;
            }
        } else if (pre && pre->kind == TokenKind::k_symbol_no && cur->kind == TokenKind::k_key_word_define) {
            // read params and body and insert new macro
            Token *tok1 = pre;
            int state = 1;
            while (tok1) {
                if (state == 0) break;

                if (state == 2) {
                    Token *macroName = nullptr;
                    if (tok1->kind != TokenKind::k_identity) {
                        error(tok1);
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
                        if (tok1->kind == TokenKind::k_symbol_next) {
                            tok1 = tok1->next;
                            continue;
                        }
                        
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
                            c->newline = false;
                            t->newline = true;
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
                        error(tok1);
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
        } else if (pre && pre->kind == TokenKind::k_symbol_no && cur->kind == TokenKind::k_key_word_undef) {
            Token *tok1 = cur->next;
            if (!tok1  || tok1->kind != TokenKind::k_identity || !pre1) {
                error(tok1);
            }

            if (!macros.count(tok1->strval)) {
                error(tok1);
            }

            Token *t = cur;
            Token *t1 = pre;                
            free_macro(macros[tok1->strval]);
            macros.erase(tok1->strval);

            cur = tok1->next;
            pre1->next = cur;
            pre = cur;
            cur = cur->next;

            delete t1;
            delete tok1;
            delete t;

            continue;
        } else if (cur->kind == TokenKind::k_key_word_include) {
            if (!pre || pre->kind != TokenKind::k_symbol_no) {
                error(pre);
            }

            string name;
            bool is_sys = false;
            Token *sysApt = nullptr;
            if (cur->next) {
                if (cur->next->kind == TokenKind::k_string) {
                    name = cur->next->strval;
                } else if (cur->next->kind == TokenKind::k_cmp_lt) {
                    Token *n = cur->next->next;
                    if (!n || n->kind != TokenKind::k_identity) {
                        error(n);
                    }

                    while (n) {
                        if (n->kind == TokenKind::k_cmp_gt) {
                            break;
                        }

                        name += n->strval;
                        n = n->next;
                    }

                    is_sys = true;
                    sysApt = n->next;
                }
            } 

            Token *ap = nullptr;
            if (!incs.count(name)) {
                Token *t = nullptr;
                if (is_sys) {
                    t = parse_file((get_cwd() + "/" + sys_dir + "/" + name).c_str());
                } else {
                    t = parse_file((get_cwd()  + (cur_compile_dir.empty() ? "" : "/" + cur_compile_dir ) + "/" + name).c_str());
                }

                if (!t) {
                    abort();
                }

                t = preprocessing(t);
                if (!t) {
                    abort();
                }

                incs[name] = t;
            }

            ap = incs[name];

            Token *head = copy_macro(ap, nullptr, 0);
            Token *end = head->origin;
            Token *apt = nullptr;

            if (is_sys) {
                apt = sysApt;
                Token *temp = pre;
                while (temp != apt) {
                    Token *t1 = temp;
                    temp = t1->next;
                    delete t1;
                }
            } else {
                apt = cur->next->next;
                delete pre;
                delete cur->next;
                delete cur;
            }

            if (pre1) {
                pre1->next = head;
            }
            else {
                h = head;
            }

            end->next = apt;
            cur = end;
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
    parsed_files.push_back(filename);

    Scanner *sc = new Scanner;
    sc->set_file(parsed_files.back().c_str());
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

            std::cout << "\nunexpected!!!\n";
            abort();
            break;
        }
    }

    Token *res = reader->get_head();
    if (reader->get_cur()) {
        reader->get_cur()->next = nullptr;
    }
    
    delete sc;
    delete reader;

    return res;
}

#define CHECK_TYPE bool is_static = false; bool is_var_args = false;\
    if (tok->kind == TokenKind::k_key_word_varargs) { \
        is_var_args = true; \
        tok = tok->next; \
    } \
    if (tok->kind == TokenKind::k_key_word_static || tok->kind == TokenKind::k_key_word_private) { \
        is_static = true; \
        tok = tok->next; \
    } \
    if (tok->kind == TokenKind::k_key_word_varargs) { \
        is_var_args = true; \
        tok = tok->next; \
    } \
    if (!types.count(tok->kind)) { \
        error(tok); \
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
        error(tok); \
    } \
    const string &name = tok->strval; \
    if (name.empty()) { \
        error(tok); \
    } \
    Token *nameToken = tok; \

static bool parse_multi_decl(vector<AbstractExpression *> &contents, AbstractExpression *exp, Token *t);

#define REQUIRE_CO(exp) if ((exp->get_type() >= ExpressionType::var_decl_ && exp->get_type() <= ExpressionType::new_) || (exp->get_type() >= ExpressionType::break_ && exp->get_type() <= ExpressionType::return_)) 

#define READ_BODY(con, tok) t->origin = tok; tok = tok->next; t->next = tok;\
            while (tok) { \
                if (tok->kind == TokenKind::k_symbol_qg2) { \
                    break; \
                } \
                \
                t->origin = tok; \
                AbstractExpression *element = parse_binay(tok, -1, t); \
                if (!element || element->get_type() == ExpressionType::func_decl_) { \
                    if (element) { \
                        FunctionDeclExpression *f = dynamic_cast<FunctionDeclExpression *>(element); \
                        if (!f && !f->lambda) { \
                            error(tok); \
                        } \
                    } else {\
                        error(tok); \
                    } \
                } \
                tok = t->next; \
                if (element->get_type() == ExpressionType::oper_ || element->get_type() == ExpressionType::var_decl_) { \
                    if (element->get_type() == ExpressionType::oper_) { \
                        BinaryExpression *bin = dynamic_cast<BinaryExpression *>(element); \
                        if (!bin) { \
                            error(t->origin); \
                        } \
                        if (bin->l->get_type() == ExpressionType::var_decl_) { \
                            VarDeclExpression *var = dynamic_cast<VarDeclExpression *>(bin->l); \
                            if (!var || var->is_static) { \
                                error(tok); \
                            }\
                        } \
                    } else {\
                        VarDeclExpression *var = dynamic_cast<VarDeclExpression *>(element); \
                        if (!var || var->is_static) { \
                            error(t->origin); \
                        } \
                    } \
                    bool detect = parse_multi_decl(con, element, t); \
                    if (detect) { \
                        tok = t->next->next; \
                        continue; \
                    } \
                } \
                t->origin = tok; \
                REQUIRE_CO(element) { \
                    require_expect(tok, t, ";"); \
                    tok = t->next; \
                }\
                con.push_back(element); \
            } \
            if (!tok) { \
                t->origin->lineno++; \
                error(t->origin); \
            } \
            if (tok->kind != TokenKind::k_symbol_qg2) { \
                error(tok); \
            }

// 用于跳过作用域，尽可能发现更多错误
static void skip(Token *tok, Token *t)
{
    while (tok) {
        if (tok->kind == TokenKind::k_symbol_qg2) {
            break;
        }

        tok = tok->next;
    }

    t = tok->next;
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
        error(tok);
    }

    // consume
    t->next = cur;
}

static AbstractExpression * parse_binay(Token *tok, int pre, Token *cache);

static AbstractExpression * parse_call(Token *tok, Token *t)
{
    if (tok->kind != TokenKind::k_symbol_qs1) {
        error(tok);
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
            error(tok);
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
        error(tok);
    }

    tok = tok->next;
    AbstractExpression *param = parse_binay(tok, -1, t);
    if (!param) {
        error(tok);
    }

    tok = t->next;
    AbstractExpression *end = nullptr;
    bool toEnd = false;
    if (tok->kind == TokenKind::k_symbol_sub_arr) {
        tok = tok->next;
        if (tok->kind == TokenKind::k_cmp_lt) {
            tok = tok->next;
            toEnd = true;
        } 
        end = parse_binay(tok, -1, t);
        if (!end) {
            error(tok);
        }

        tok = t->next;
    }

    if (tok->kind != TokenKind::k_symbol_qm2) {
        error(tok);
    }

    IndexExpression *idx = new IndexExpression;
    //idx->l = indexer;
    idx->idx = param;
    idx->idx1 = end;
    idx->toend = toEnd;

    t->next = tok->next;
    return idx;
}

static AbstractExpression* parse_follow(AbstractExpression *exp, Token *tok, Token *t);

static AbstractExpression * parse_return(Token *tok, Token *t)
{
    if (tok->kind != TokenKind::k_key_word_return) {
        error(tok);
    }

    AbstractExpression *r = nullptr;
    if (tok->next) {
        if (tok->next->kind != TokenKind::k_symbol_co) {
            r = parse_binay(tok->next, -1, t);
            if (!r || r->get_type() < ExpressionType::value_ || (r->get_type() > ExpressionType::triple_ && r->get_type() != ExpressionType::construct_)) {
                error(tok);
            }

            if (r->get_type() == ExpressionType::oper_) {
                BinaryExpression *bin = dynamic_cast<BinaryExpression *>(r);
                if (!bin || bin->l->get_type() == ExpressionType::var_decl_) {
                    cout << "type: " << (int)bin->l->get_type() << endl;
                    error(tok);
                }
            }

            tok = t->next;
            r = parse_follow(r, tok, t);
            tok = t->next;
        } else {
            t->next = tok->next;
        }
    } else {
        error(tok);
    }

    ReturnExpression *ret = new ReturnExpression;
    ret->ret = r;
    return ret;
}

static AbstractExpression * parse_triple(Token *tok, Token *t)
{
    if (tok->kind != TokenKind::k_cmp_quetion) {
        error(tok);
    }
    tok = tok->next;

    AbstractExpression *first = parse_binay(tok, -1, t);
    if (!first) {
        error(tok);
    }

    tok = t->next;
    if (!tok || tok->kind != TokenKind::k_symbol_show) {
        error(tok);
    }

    tok = tok->next;
    AbstractExpression *second = parse_binay(tok, -1, t);
    if (!second) {
        error(tok);
    }

    TripleExpression *triple = new TripleExpression;
    triple->first = first;
    triple->second = second;

    return triple;
}

static AbstractExpression * parse_case_or_default(Token *tok, Token *t)
{
    int type = 0;
    if (tok->kind == TokenKind::k_key_word_default) {
        type = 1;
    }

    AbstractExpression *caser = nullptr;
    tok = tok->next;
    if (type == 0) {
        caser = parse_binay(tok, -1, t);
        if (!caser) {
            error(tok);
        }

        tok = t->next;
    }

    if (tok->kind != TokenKind::k_symbol_show) {
        error(tok);
    }

    AbstractExpression *exp = nullptr;
    vector<AbstractExpression *> *bodys = nullptr;
    if (type == 0) {
        CaseExpression *ca = new CaseExpression;
        bodys = &ca->bodys;
        ca->caser = caser;
        exp = ca;
    } else {
        DefaultExpression *def = new DefaultExpression;
        bodys = &def->bodys;
        exp = def;
    }

    tok = tok->next;
    t->next = tok;
    bool new_block = true;
    while (tok) {
        if (tok->kind == TokenKind::k_symbol_qg1) {
            READ_BODY((*bodys), tok)
            tok = t->next->next;
            new_block = true;
        }

        if (tok->kind == TokenKind::k_key_word_case || tok->kind == TokenKind::k_key_word_default || tok->kind == TokenKind::k_symbol_qg2) {
            break;
        }

        if (new_block) {
            new_block = false;
        }

        AbstractExpression *element = parse_binay(tok, -1, t);
        if (!element) {
            error(tok);
        }

        tok = t->next;
        REQUIRE_CO(element) {
            require_expect(t->next, t, ";");
            tok = t->next;
        }

        bodys->push_back(element);
    }

    t->next = tok;
    return exp;
}

static AbstractExpression * parse_switch_case(Token *tok, Token *t)
{
    if (tok->kind != TokenKind::k_key_word_switch) {
        error(tok);
    }

    tok = tok->next;
    if (tok->kind != TokenKind::k_symbol_qs1) {
        error(tok);
    }

    tok = tok->next;
    AbstractExpression *select = parse_binay(tok, -1, t);
    if (!select) {
        error(tok);
    }

    tok = t->next;
    if (tok->kind != TokenKind::k_symbol_qs2) {
        error(tok);
    }

    tok = tok->next;
    if (tok->kind != TokenKind::k_symbol_qg1) {
        error(tok);
    }

    tok = tok->next;
    if (tok->kind != TokenKind::k_key_word_case && tok->kind != TokenKind::k_key_word_default) {
        error(tok);
    }

    SwitchCaseExpression *sce = new SwitchCaseExpression;
    sce->selector = select;

    while (tok) {
        if (tok->kind == TokenKind::k_symbol_qg2) {
            break;
        }

        AbstractExpression *exp = parse_case_or_default(tok, t);
        sce->cases.push_back(exp);
        tok = t->next;
    }

    t->next = tok->next;
    return sce;
}

static void parse_parameter(vector<AbstractExpression *> &params, Token *tok, Token *t)
{
    while (tok) {
        if (tok->kind == TokenKind::k_symbol_qs2) {
            break;
        }

        if (tok->kind == TokenKind::k_symbol_qg1) {
            error(tok);
        }

        if (t->ival == -1 && tok->kind == TokenKind::k_key_word_in) {
            break;
        }

        if (tok->kind == TokenKind::k_symbol_sep) {
            tok = tok->next;
        }

        AbstractExpression *param = parse_binay(tok, -1, t);
        if (!param) {
            error(tok);
        }

        VarDeclExpression *var = dynamic_cast<VarDeclExpression *>(param);
        if (!var) {
            cout << "type: " << (int)param->get_type() << endl;
            error(tok);
        }

        if (var->is_static) {
            error(tok);
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
        error(tok);
    }

    tok = tok->next;
    if (tok->kind != TokenKind::k_symbol_qs1) {
        error(tok);
    }

    tok = tok->next;
    // foreach (xxx, xxx in xx)
    ForeachExpression *foreach = new ForeachExpression;
    t->ival = -1;
    parse_parameter(foreach->decls, tok, t);
    tok = t->next;
    t->ival = 0;

    if (tok->kind != TokenKind::k_key_word_in) {
        error(tok);
    }

    tok = tok->next;
    AbstractExpression *container = parse_binay(tok, -1, t);
    if (!container) {
        error(tok);
    }
    
    foreach->container = container;

    tok = t->next;
    if (tok->kind != TokenKind::k_symbol_qs2) {
        error(tok);
    }

    tok = tok->next;
    if (tok->kind != TokenKind::k_symbol_qg1) {
        AbstractExpression *op = parse_binay(tok, -1, t);
        if (!op) {
            error(tok);
        }

        REQUIRE_CO(op) {
            require_expect(t->next, t, ";");
        }
        // TODO check is op
        foreach->body.push_back(op);
    } else {
        READ_BODY(foreach->body, tok)
        t->next = tok->next;
    }

    return foreach;
}

static AbstractExpression * parse_while(Token *tok, Token *t)
{
    if (tok->kind != TokenKind::k_key_word_while) {
        error(tok);
    }

    tok = tok->next;
    if (!tok || tok->kind != TokenKind::k_symbol_qs1) {
        error(tok);
    }

    AbstractExpression *exp = parse_binay(tok->next, -1, t);
    if (!exp) {
        error(tok);
    }

    tok = t->next;
    if (!tok || tok->kind != TokenKind::k_symbol_qs2) {
        error(tok);
    }

    if (!tok->next) {
        error(tok);
    }

    bool skip = false;
    whileExpression *w = new whileExpression;
    w->cond = exp;

    tok = tok->next;
    if (tok->kind != TokenKind::k_symbol_qg1) {
        AbstractExpression *op = parse_binay(tok, -1, t);
        if (!op) {
            error(tok);
        }

        if (op->get_type() >= ExpressionType::var_decl_ && op->get_type() <= ExpressionType::new_) {
            require_expect(t->next, t, ";");
            tok = t->next; 
        }
        w->body.push_back(op);
    } else {
        READ_BODY(w->body, tok)
        skip = true;
    }

    if (skip) t->next = tok->next;
    else t->next = tok;

    return w;
}

static AbstractExpression * parse_do_while(Token *tok, Token *t)
{
    if (!tok || tok->kind != TokenKind::k_key_word_do) {
        error(tok);
    }

    tok = tok->next;
    if (!tok || tok->kind != TokenKind::k_symbol_qg1) {
        error(tok);
    }

    DoWhileExpression *dw = new DoWhileExpression;

    READ_BODY(dw->body, tok)

    if (!tok || tok->kind != TokenKind::k_symbol_qg2) {
        error(tok);
    }

    tok = tok->next;
    if (!tok || tok->kind != TokenKind::k_symbol_qs1) {
        error(tok);
    }

    AbstractExpression *cond = parse_binay(tok, -1, t);
    if (!cond) {
        error(tok);
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
            error(tok);
        }
        // TODO  check op, decl

        if (i == 0) for_->inits.push_back(init);
        else if (i == 1) for_->conditions.push_back(init);
        else if (i == 2) for_->operations.push_back(init);
        else error(tok);

        tok = t->next;
    }

    t->next = tok->next;
}

static AbstractExpression * parse_for(Token *tok, Token *t)
{
    if (tok->kind != TokenKind::k_key_word_for) {
        error(tok);
    }

    tok = tok->next;
    if (tok->kind != TokenKind::k_symbol_qs1) {
        error(tok);
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
            error(tok);
        }

        REQUIRE_CO(op) {
            require_expect(t->next, t, ";");
            tok = t->next; 
        }
        // TODO check is op
        for_->body.push_back(op);
    } else {
        READ_BODY(for_->body, tok)
        skip = true;
    }

    if (skip) t->next = tok->next;
    else t->next = tok;
    
    return for_;
}

static AbstractExpression * parse_if_exp(Token *tok, Token *t)
{
    if (tok->kind != TokenKind::k_key_word_if) {
        error(tok);
    }

    IfExpression *ifExp = new IfExpression;
    int type = 0;
    bool hasCond = false;
    bool skip = false;
    while (tok) {
        if (tok->kind == TokenKind::k_key_word_if) {
            tok = tok->next;
            if (!tok || tok->kind != TokenKind::k_symbol_qs1) {
                error(tok);
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
                error(tok);
            }
        }

        AbstractExpression *cond = nullptr;
        if (hasCond) {
            cond = parse_binay(tok, -1, t);
            if (!cond) {
                error(tok);
            }

            tok = t->next;
            if (tok->kind != TokenKind::k_symbol_qs2) {
                error(tok);
            }
        }
        
        IfExpression::If if_;
        if_.cond = cond;
        if_.type = type;

        tok = tok->next;
        if (tok->kind != TokenKind::k_symbol_qg1) {
            AbstractExpression *op = parse_binay(tok, -1, t);
            if (!op) {
                error(tok);
            }

            REQUIRE_CO(op) {
                require_expect(t->next, t, ";");
            }

            if_.body.push_back(op);

            if (type == 2) {
                skip = false;
                tok = t->next;
                ifExp->exps.push_back(if_);
                break;
            }

            if (t->next && t->next->kind != TokenKind::k_key_word_else) {
                tok = t->next;
                skip = false;
                ifExp->exps.push_back(if_);
                break;
            }
        } else {
            IfExpression::If *p = &if_;
            READ_BODY(p->body, tok)
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

static AbstractExpression * parse_lambda(Token *tok, Token *t)
{
    if (tok->kind != TokenKind::k_key_word_fun) {
        error(tok);
    }

    Token *pre = tok;
    tok = tok->next;
    if (!tok || tok->kind != TokenKind::k_symbol_qs1) {
        error(pre);
    }

    pre = tok;
    tok = tok->next;
    FunctionDeclExpression *func = new FunctionDeclExpression;
    func->lambda = true;

    parse_parameter(func->params, tok, t);

    tok = t->next;
    if (tok->kind == TokenKind::k_symbol_qg1) {
        READ_BODY(func->body, tok)
        t->next = tok->next;
    } else {
        if (!tok) {
            error(tok);
        }

        if (tok->kind == TokenKind::k_oper_pointer) {
            tok = tok->next;
            AbstractExpression *exp = parse_binay(tok, -1, t);
            if (!exp) {
                error(tok);
            }
            
            tok = t->next;
            func->body.push_back(exp);
        } else {
            error(tok);
        }
    }

    return func;
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
        error(tok);
    }

    FunctionDeclExpression *func = new FunctionDeclExpression;
    func->returnType = retType;
    func->is_arr = is_arr;
    func->name = nameToken; 
    func->is_static = is_static;
    func->dtype = DeclType::func_;
    func->is_varargs = is_var_args;
    func->user_define_type = user_define_type;
    tok = tok->next;
    parse_parameter(func->params, tok, t);

    Token *cur = t->next;
    if (cur->kind == TokenKind::k_symbol_qg1) {
        READ_BODY(func->body, cur)
    } else {
        // 前置声明
        if (cur->kind != TokenKind::k_symbol_co) {
            error(tok);
        }
    }

    cur = t->next;
    if (cur) t->next = cur->next;
    return func;
}

static AbstractExpression * parse_class_decl(Token *tok, Token *t)
{
    bool is_static = false;
    if (tok->kind == TokenKind::k_key_word_static) {
        is_static = true;
        tok = tok->next;
    }

    if (tok->kind != TokenKind::k_key_word_class) {
        error(tok);
    }

    tok = tok->next;
    if (!tok || tok->kind != TokenKind::k_identity) {
        error(tok);
    }

    ClassExpression *clazz = new ClassExpression;
    clazz->className = tok;
    clazz->is_static = is_static;

    tok = tok->next;
    if (!tok || tok->kind != TokenKind::k_symbol_qg1) {
        error(tok);
    }

    tok = tok->next;
    while (tok) {
        if (tok->kind == TokenKind::k_symbol_qg2) {
            break;
        }

        AbstractExpression *item = parse_binay(tok, -1, t);
        if (!item) {
            error(tok);
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

    if (is_var_args) {
        error(tok);
    }

    if (tok->next && tok->next->kind == TokenKind::k_symbol_var_arg) {
        is_var_args = true;
        tok = tok->next;
    }

    VarDeclExpression *decl = new VarDeclExpression;
    decl->dtype = retType;
    decl->name = nameToken;
    decl->is_static = is_static;
    decl->is_arr = is_arr;
    decl->varargs = is_var_args;
    decl->user_define_type = user_define_type;

    t->next = tok;
    return decl;
}

static AbstractExpression * parse_import(Token *tok, Token *t)
{
    if (tok->kind != TokenKind::k_symbol_no) {
        error(tok);
    }

    tok = tok->next;
    if (!tok || tok->kind != TokenKind::k_key_word_import) {
        error(tok);
    }

    ImportExpression *imp = new ImportExpression;
    tok = tok->next;
    int state = 0;
    while (tok) {
        if (tok->kind != TokenKind::k_identity) {
            error(tok);
        }

        if (state == 0) {
            imp->path.push_back(tok);

            tok = tok->next;
            if (tok->kind == TokenKind::k_symbol_dot) {
                tok = tok->next;
            } else if (tok->kind == TokenKind::k_identity && tok->strval == "as") {
                tok = tok->next;
                state = 1;
            } else {
                error(tok);
            }
        } else {
            imp->asName = tok;
            break;
        }
    }

    t->next = tok->next;
    return imp;
}

static unordered_map<TokenKind, int> priority_map = {
    {TokenKind::k_oper_assign, 2},
    {TokenKind::k_oper_plus_assign, 2},
    {TokenKind::k_oper_minus_assign, 2},
    {TokenKind::k_oper_mul_assign, 2},
    {TokenKind::k_oper_div_assign, 2},
    {TokenKind::k_oper_mod_assign, 2},
    
    {TokenKind::k_cmp_or, 4},
    {TokenKind::k_cmp_and, 4},

    {TokenKind::k_oper_bin_or, 5},
    {TokenKind::k_oper_bin_and, 5},
    {TokenKind::k_oper_bin_lm, 5},
    {TokenKind::k_oper_bin_rm, 5},

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

static AbstractExpression * parse_primary(Token *tok, Token *t, bool fromOp)
{
    AbstractExpression *exp = nullptr;
    bool is_static = false;
    bool is_varargs = false;
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
            case TokenKind::k_key_word_fun:
            case TokenKind::k_key_word_bool: {
                if (k == TokenKind::k_key_word_fun) {
                    Token *next1 = tok->next;
                    if (next1 && next1->kind == TokenKind::k_symbol_qs1) {
                        exp = parse_lambda(tok, t);
                        tok = nullptr;
                        break;
                    }
                }
                if (!is_static) t->next = tok;

                initType = tok->kind;
                Token *n = tok->next;
                if (n) {    // 数组
                    if (n->kind == TokenKind::k_oper_mul) {
                        tok = n->next;
                    } else if (n->kind == TokenKind::k_identity) {
                        tok = n;
                    } else {
                        error(tok);
                    }
                    goto start;
                }
            }
clazz:
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
            case TokenKind::k_key_word_switch: {
                exp = parse_switch_case(tok, t);
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
                    error(tok);
                }

                tok = tok->next;
                ValueExpression *id = new ValueExpression;
                id->valType = 4;
                id->val.sval = tok;

                NewExpression *new_ = new NewExpression;
                new_->id = id;

                Token *n = tok->next;
                if (n && n->kind == TokenKind::k_symbol_qs1) {
                    Token *pre = n;
                    n = n->next;
                    bool start = false;
                    while (n) {
                        if (n->kind == TokenKind::k_symbol_qs2) {
                            break;
                        }

                        if (start) {
                            if (n->kind != TokenKind::k_symbol_sep) {
                                error(n);
                            }
                        }

                        if (n->kind == TokenKind::k_symbol_sep) {
                            n = n->next;
                            if (!n || n->kind == TokenKind::k_symbol_qs2) {
                                error(pre);
                            }

                            start = false;
                            continue;
                        }

                        if (n->kind != TokenKind::k_identity) {
                            error(n);
                        }
                        
                        ValueExpression *key = new ValueExpression;
                        key->valType = 4;
                        key->val.sval = n;
                        new_->inits.push_back(key);

                        pre = tok;
                        n = n->next;
                        if (!n || n->kind != TokenKind::k_symbol_show) {
                            error(pre);
                        }

                        pre = n;
                        n = n->next;
                        if (!n) {
                            error(pre);
                        }

                        AbstractExpression *val = parse_binay(n, -1, t);
                        if (!val) {
                            error(n);
                        }

                        new_->inits.push_back(val);
                        n = t->next;
                        pre = n;
                        start = true;
                    }

                    tok = n->next;
                } else {
                    tok = n;
                }

                t->next = tok;
                exp = new_;
                tok = nullptr;
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
                    error(tok);
                }
                
                exp = b;
                break;
            }

            case TokenKind::k_key_word_continue: {
                ContinueExpression *c = new ContinueExpression;
                if (!tok->next || tok->next->kind != TokenKind::k_symbol_co) {
                    error(tok);
                }

                exp = c;
                break;
            }
            case TokenKind::k_key_word_varargs: 
                is_varargs = true;
                t->next = tok;
                tok = tok->next;
                if (tok->kind == TokenKind::k_identity) {
                    goto start;
                }
            case TokenKind::k_key_word_static:
            case TokenKind::k_key_word_private: {
                is_static = true;
                if (!is_varargs) t->next = tok;
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
                        case TokenKind::k_key_word_fun:
                        case TokenKind::k_key_word_bool: {
                            tok = n;
                            goto pri_kw;
                        }
                        case TokenKind::k_identity: {
                            initType = n->kind;
                            if (n->next && n->next->kind == TokenKind::k_oper_mul) {
                                tok = n->next->next;
                            } else {
                                tok = n;
                            }
                            goto start;
                        }
                        case TokenKind::k_key_word_varargs:{
                            tok = n->next;
                            goto pri_kw;
                        }
                        case TokenKind::k_key_word_class:{
                            goto clazz;
                        }
                        default: {
                            error(tok);
                        }
                    }
                } else {
                    error(tok);
                }
                break;
            }
            default: {
                error(tok);
                break;
            }
        }
    } else if (k == TokenKind::k_identity) {
        Token *n = tok->next;
        if (n && !fromOp) {
            if (n->kind == TokenKind::k_oper_mul) {
                n = n->next;
                // hello_t * ac()
                if (n) {
                    if (n->kind == TokenKind::k_identity) {
                        if (n->next) {
                            if (n->next->kind == TokenKind::k_symbol_qs1) {
                                Token *n1 = n->next;
                                bool found = false;
                                while (n1) {
                                    if (found) {
                                        if (n1->kind == TokenKind::k_symbol_qg1) {
                                            t->next = tok;
                                            initType = tok->kind;
                                            tok = n;
                                        } else {
                                            break;
                                        }
                                    }

                                    if (n1->kind == TokenKind::k_symbol_qs2) {
                                        found = true;
                                    }
                                    
                                    n1 = n1->next;
                                }
                            } else if (n->next->kind == TokenKind::k_symbol_sep) {
                                initType = tok->kind;
                                t->next = tok;
                            } else if (n->next->kind == TokenKind::k_symbol_qs2) {
                                initType = tok->kind;
                                t->next = tok;
                            }
                        }
                    }
                } 
            } else {
                if (n->kind == TokenKind::k_identity) {                                 // 变量声明
                    // hello_t hell()
                    if (n->next) {
                        if (n->next->kind == TokenKind::k_symbol_qs1) {
                            initType = tok->kind;
                            t->next = tok;
                            tok = n;
                        } else if (n->next->kind == TokenKind::k_oper_assign || n->next->kind == TokenKind::k_symbol_co 
                            || n->next->kind == TokenKind::k_key_word_in || n->next->kind == TokenKind::k_symbol_qs2 || n->next->kind == TokenKind::k_symbol_sep) {
                            initType = tok->kind;
                            t->next = tok;
                        }
                    }
                }
            }
        }

        if (initType == TokenKind::k_none) {
            ValueExpression *id = new ValueExpression;
            id->valType = 4;
            id->val.sval = tok;
            if (n && n->kind == TokenKind::k_symbol_var_arg) {
                id->is_var_arg = true;
                tok = n;
            }
            
            exp = parse_follow(id, tok->next, t);

            if (exp != id) tok = nullptr;
        } else {
            if (tok->next && tok->next->kind == TokenKind::k_symbol_qs1) {
                exp = parse_function_decl(t->next, t);
                tok = nullptr;
            } else {
                exp = parse_var_decl(t->next, t);
                tok = t->next;
            }
        }

        initType = TokenKind::k_none;
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
                error(tok);
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
            error(tok);
        }

        if (t->next->kind != TokenKind::k_symbol_qs2) {
            error(tok);
        }
        
        exp = element;
        tok = t->next;
    } else if (k == TokenKind::k_symbol_no) {
        exp = parse_import(tok, t);
        tok = nullptr;
    } else if (k == TokenKind::k_cmp_quetion) {
        exp = parse_triple(tok, t);
        tok = nullptr;
    } else {
        error(tok);
    }

    if (tok) t->next = tok->next;
    return exp;
}

static AbstractExpression * parse_unary(Token *tok, Token *t, bool fromOp)
{
    static set<TokenKind> unary_oper = {
        TokenKind::k_oper_minus, TokenKind::k_oper_plus_plus, TokenKind::k_oper_sub_sub, TokenKind::k_cmp_not,
    };

    TokenKind k = tok->kind;
    if (unary_oper.count(k)) {
        AbstractExpression *exp = parse_primary(tok->next, t, fromOp);
        UnaryExpression *uExp = new UnaryExpression;
        uExp->op = k;
        uExp->exp = exp;

        return uExp;
    } else {
        AbstractExpression *exp = parse_primary(tok, t, fromOp);
        if (!exp) {
            error(tok);
        }

        if (exp->get_type() == ExpressionType::oper_ || exp->get_type() == ExpressionType::index_ || exp->get_type() == ExpressionType::call_ || exp->get_type() == ExpressionType::triple_ || exp->get_type() == ExpressionType::value_) {
            tok = t->next;
            if (tok) {
                k = tok->kind;
                if (k == TokenKind::k_oper_plus_plus || k == TokenKind::k_oper_sub_sub) {
                    if (exp->get_type() == ExpressionType::value_) {
                        ValueExpression *v = dynamic_cast<ValueExpression *>(exp);
                        if (!v || !t->next || v->valType != 4) {
                            error(t->next);
                        }
                    }

                    t->next = tok->next;
                    UnaryExpression *uExp = new UnaryExpression;
                    uExp->op = k;
                    uExp->exp = exp;

                    return uExp;
                }
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
                error(tok);
            }

            CallExpression *call = dynamic_cast<CallExpression *>(c);
            if (!call) {
                error(tok);
            }
            call->callee = last;
            last = call;
        } else if (tok->kind == TokenKind::k_symbol_qm1) {
            AbstractExpression *i = parse_index(tok, t);
            if (!i) {
                error(tok);
            }

            IndexExpression *idx = dynamic_cast<IndexExpression *>(i);
            if (!idx) {
                error(tok);
            }
            idx->l = last;
            last = idx;
        } else if (tok->kind == TokenKind::k_cmp_quetion) {
            AbstractExpression *triple = parse_triple(tok, t);
            if (!triple) {
                error(tok);
            }

            TripleExpression *tp = dynamic_cast<TripleExpression *>(triple);
            if (!tp || !exp) {
                error(tok);
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
    AbstractExpression *exp1 = parse_unary(tok, cache, false);
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
            error(tok);
        }

        exp2 = parse_follow(exp2, cache->next, cache);

        BinaryExpression *op = new BinaryExpression;
        op->oper = kind;
        op->l = exp1;
        op->r = exp2;

        exp1 = op;
        tok = cache->next;
        if (!tok) {
            break;
        }

        tprec = get_pre(tok->kind);
        kind = tok->kind;
    }

    return exp1;
}

static bool parse_multi_decl(vector<AbstractExpression *> &contents, AbstractExpression *exp, Token *t)
{
    if (!exp) return false;

    // 连续声明
    if (t->next && t->next->kind == TokenKind::k_symbol_sep) {
        
        contents.push_back(exp);

        Token *tok = t->next->next; // skip ','
        DeclType dtype = DeclType::none_;
        Token *user_def = nullptr;
        bool is_static = false, is_arr = false;
        if (exp->get_type() == ExpressionType::oper_) {
            BinaryExpression *bin = dynamic_cast<BinaryExpression *>(exp);
            VarDeclExpression *decl = dynamic_cast<VarDeclExpression *>(bin->l);
            if (!decl || decl->dtype == DeclType::none_) {
                error(tok);
            }

            dtype = decl->dtype;
            user_def = decl->user_define_type;
        } else {
            VarDeclExpression *decl = dynamic_cast<VarDeclExpression *>(exp);
            if (!decl || decl->dtype == DeclType::none_) {
                error(tok);
            }

            dtype = decl->dtype;
            user_def = decl->user_define_type;
        }

        while (tok) {
            if (tok->kind == TokenKind::k_symbol_co) {
                break;
            }

            if (tok->kind == TokenKind::k_symbol_sep) {
                tok = tok->next;
                continue;
            }

            if (tok->kind == TokenKind::k_oper_mul) {
                is_arr = true;
                tok = tok->next;
            }

            AbstractExpression *dexp = parse_binay(tok, -1, t);
            if (!dexp || (dexp->get_type() != ExpressionType::value_ && dexp->get_type() != ExpressionType::oper_)) {
                error(tok);
            }

            if (dexp->get_type() == ExpressionType::value_) {
                ValueExpression *val = dynamic_cast<ValueExpression *>(dexp);
                if (!val || val->valType != 4) {
                    error(tok);
                }

                VarDeclExpression *decl = new VarDeclExpression;
                decl->dtype = dtype;
                decl->name = val->val.sval;
                decl->is_static = is_static;
                decl->is_arr = is_arr;
                decl->user_define_type = user_def;

                AbstractExpression *t = dexp;
                dexp = decl;

                delete t;
            } else if (dexp->get_type() == ExpressionType::oper_){
                BinaryExpression *bin = dynamic_cast<BinaryExpression *>(dexp);
                if (!bin) {
                    error(tok);
                }

                ValueExpression *val = dynamic_cast<ValueExpression *>(bin->l);

                VarDeclExpression *decl = new VarDeclExpression;
                decl->dtype = dtype;
                decl->name = val->val.sval;
                decl->is_static = is_static;
                decl->is_arr = is_arr;
                decl->user_define_type = user_def;

                AbstractExpression *t = bin->l;
                bin->l = decl;

                delete t;
            } else {
                error(tok);
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
        if (cur->kind == TokenKind::k_key_word_inherit) {
            if (cur->next->kind != TokenKind::k_string) {
                error(cur);
            }

            doc->inherits.push_back(cur->next->strval);
            require_expect(cur->next->next, &t, ";");
            cur = t.next;
            continue;
        }

        AbstractExpression *exp = parse_binay(cur, -1, &t);
        if (!exp) {
            error(cur);
        }
        
        if (exp->get_type() != ExpressionType::oper_ && exp->get_type() != ExpressionType::func_decl_ && exp->get_type() != ExpressionType::var_decl_ && exp->get_type() != ExpressionType::import_ && exp->get_type() != ExpressionType::class_) {
            cout << "etype: " << (int) exp->get_type() << endl;
            error(cur);
        }

        if (exp->get_type() == ExpressionType::oper_) {
            BinaryExpression *bin = dynamic_cast<BinaryExpression *>(exp);
            if (!bin || bin->l->get_type() != ExpressionType::var_decl_) {
                cout << "type: " << (int)bin->l->get_type() << endl;
                error(cur);
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

static void on_compile_success(Parser *parser)
{
    unordered_map<string, Macro *> *macros = parser->get_macros();
    for (auto &it : *macros) {
        Macro *m = it.second;
        if (m) {
            Token *tok = m->body;
            while (tok) {
                Token *t = tok->next;
                delete tok;
                tok = t;
            }
        }
    }
}

unordered_map<string, Macro *> * Parser::get_macros()
{
    return &this->macros;
}


AbstractExpression * Parser::parse_one(const char *filename)
{
    cur_file = filename;
    Token *tok = parse_file(filename);
    Token *cur = preprocessing(tok);
    // 正式开始处理
    DocumentExpression *exp = dynamic_cast<DocumentExpression *>(do_parse(cur));
    exp->file_name = filename;
    

    return exp;
}

static void find_all_file(vector<string> &all, string dirName)
{
#ifdef WIN32
    if( (_access( "crt_ACCESS.C", 0 )) != -1 ) {
        all.push_back(dirName);
        return;
    }
    
    // 这里用 long 来保存会出现错误，原因是 long 不同平台长度是不一样的，然后 x64 平台下出现负数，故出错了，intptr_t 做了处理，各平台一样
    intptr_t hFile = 0;
	//文件信息    
	struct _finddata_t fileinfo;  //用来存储文件信息的结构体    
	string p;
	if ((hFile = _findfirst(p.assign(dirName).append("\\*").c_str(), &fileinfo)) != -1)  //第一次查找  
	{
		do {
			if ((fileinfo.attrib &  _A_SUBDIR)) {
				if (strcmp(fileinfo.name, ".") != 0 && strcmp(fileinfo.name, "..") != 0)  //进入文件夹查找  
				{
					find_all_file(all, p.assign(dirName).append("\\").append(fileinfo.name));
				}
			} else {
                const char *pFile = strrchr(fileinfo.name, '.');
                if (pFile != NULL && strcmp(pFile, ".txt") == 0) {
                    all.push_back(string().assign(dirName).append("\\").append(fileinfo.name));
                }
			}
		} while (_findnext(hFile, &fileinfo) == 0);

		_findclose(hFile); //结束查找  
	} else {
        cout << "error on finding files!!! \n";
        exit(-1);
    }

#else
    struct stat statbuf;
	int res = -1;
	res = lstat(dirName.c_str(), &statbuf);//获取linux操作系统下文件的属性
	//参数1是传入参数，填写文件的绝对路径 | 参数2是传出参数,一个struct stat类型的结构体指针

	if (0 != res) {
		printf("lstat failed");
        exit(1);
	}

	if (!S_ISDIR(statbuf.st_mode)) {
        const char *pFile = strrchr(dirName.c_str(), '.');
        if (pFile != NULL && strcmp(pFile, ".txt") == 0) {
            all.push_back(dirName);
        }
	} else {
        DIR *dir;
        struct dirent *pDir;
        if((dir = opendir(dirName.c_str())) == NULL){
            cout << "open dir Faild" << endl;
            exit(1);
        }

        while((pDir = readdir(dir)) != NULL) {
            if(strcmp(pDir->d_name,".") == 0 || strcmp(pDir->d_name,"..") == 0){
                continue;
            } else if(pDir->d_type == 8) { // 文件
                const char *pFile = strrchr(pDir->d_name, '.');
                if (pFile != NULL && strcmp(pFile, ".txt") == 0) {
                    all.push_back(dirName + "/" + pDir->d_name);
                }
            } else if(pDir->d_type == 10){
                continue;
            } else if(pDir->d_type == 4) { // 子目录
                string strNextdir = dirName + "/" + pDir->d_name;
                find_all_file(all, strNextdir);
            }
        }

        closedir(dir);
    }
#endif
}

vector<AbstractExpression *> * Parser::parse(const char *filename)
{
    // TODO Check filename must be directory
    add_built_in_macro();
    // 用这个 try catch 的好处是可以跳回到开始点
    vector<string> files;
    find_all_file(files, filename);
    for (auto &it : files) {
        try {
            AbstractExpression *exp = parse_one(it.c_str());
            DocumentExpression *doc = dynamic_cast<DocumentExpression *>(exp);
            if (doc->contents.empty()) {
                cout << "[error] parse " << doc->file_name << " failed!\n";
                continue;
            }
            parsed.push_back(exp);
        } catch(...) {
            // TODO
            cout << "[error] parse " << it << " failed!\n";
            continue;
        }
    }

    on_compile_success(this);
    
    return &parsed;
}
