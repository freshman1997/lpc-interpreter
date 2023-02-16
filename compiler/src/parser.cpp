#include <iostream>

#include "parser.h"

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
    if (!tok || tok->kind != TokenKind::k_symbol_qs1) {
        if (end) end->next = tok;
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
        if (mt->kind == TokenKind::k_identity && args && args->count(mt->strval)) {
            toCopy = (*args)[mt->strval]->tok;
        }
        else toCopy = mt;

        Token *t = new Token;
        *t = *toCopy;
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

static void read_macro_args(MacroArg *arg, Token *tok)
{
    if (tok) {
        Token *t = nullptr;
        if (tok->kind == TokenKind::k_string || tok->kind == TokenKind::k_integer || tok->kind == TokenKind::k_number || tok->kind == TokenKind::k_identity ) {
            t = new Token;
            *t = *tok;
        }

        if (!t) {
            abort();
        }

        t->next = nullptr;
        arg->tok = t;
    }
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
                if (cur->next->kind == TokenKind::k_symbol_qs1) {
                    // TODO 读取入参
                    Token *at = cur;
                    at = at->next;
                    string q;
                    
                    while (at) {
                        // hello("", 100, 200, a, b)
                        if (at->kind == TokenKind::k_symbol_qs1) {
                            q.push_back('(');
                            at = at->next;
                        }
                        
                        MacroArg *arg = new MacroArg;
                        arg->tok = nullptr;
                        read_macro_args(arg, at);

                        at = at->next;

                        if ((at->kind == TokenKind::k_symbol_sep || arg->tok) && q.size() == 1) {
                            MacroParam *p = macro->params;
                            for (int c = i; c > 0; --c) {
                                p = p->next;
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
        else if (cur->kind == TokenKind::k_key_word_define) {
                // read params and body and insert new macro

                // 名称
                cur = cur->next;
                Token *macroName = nullptr;
                if (cur->kind != TokenKind::k_identity) {
                    abort();
                }

                macroName = cur;
                cur = cur->next;

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
                    if (cur->newline) {
                        if (cur->kind == TokenKind::k_symbol_next) {
                            cur = cur->next;
                            continue;
                        }
                    }

                    Token *t = new Token;
                    *t = *cur;
                    if (cur->newline) {
                        t->next = nullptr;
                        c->next = t;
                        break;
                    }

                    if (!c) {
                        from = t;
                        c = t;
                    }
                    else {
                        c->next = t;
                        c = t;
                    }

                    tmp.newline = cur;
                    cur = cur->next;
                }

                m->body = from;
                m->params = params;
                macros[macroName->strval] = m;
            }
            else if (cur->kind == TokenKind::k_key_word_include) {
                // TODO
                if (pre->kind != TokenKind::k_symbol_no || !(cur->next && cur->next->kind == TokenKind::k_string)) abort();

                Token *ap = nullptr;
                if (!incs.count(cur->strval)) {
                    Token *t = parse_file(build_include_path(cur->next->strval).c_str());
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
            std::cout << "unexpected\n";
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

// 常量折叠，分支处理，死代码删除，循环无关外提，

ExpressionVisitor * Parser::parse(const char *filename)
{
    Token *tok = parse_file(filename);
    Token *cur = preprocessing(tok);
    while (cur) {
        cout << cur->strval;
        if (cur->is_space) cout << ' ';
        if (cur->newline) cout << '\n';
        cur = cur->next;
    }
    return NULL;
}
