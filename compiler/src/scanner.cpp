#include <cassert>
#include <iostream>
#include <unordered_map>
#include "scanner.h"

using namespace std;

static unordered_map<string, TokenKind> keywords = {
    {"int", TokenKind::k_key_word_int},
    {"float", TokenKind::k_key_word_float},
    {"mixed", TokenKind::k_key_word_mixed},
    {"mapping", TokenKind::k_key_word_mapping},
    {"string", TokenKind::k_key_word_string},
    {"object", TokenKind::k_key_word_object},
    {"void", TokenKind::k_key_word_void},
    {"static", TokenKind::k_key_word_static},
    {"private", TokenKind::k_key_word_private},
    {"include", TokenKind::k_key_word_include},
    {"define", TokenKind::k_key_word_define},
    {"undef", TokenKind::k_key_word_undef},
    {"if", TokenKind::k_key_word_if},
    {"else", TokenKind::k_key_word_else},
    {"for", TokenKind::k_key_word_for},
    {"foreach", TokenKind::k_key_word_foreach},
    {"do", TokenKind::k_key_word_do},
    {"while", TokenKind::k_key_word_while},
    {"in", TokenKind::k_key_word_in},
    {"or", TokenKind::k_key_word_or},
    {"switch", TokenKind::k_key_word_switch},
    {"case", TokenKind::k_key_word_case},
    {"default", TokenKind::k_key_word_default},
    {"return", TokenKind::k_key_word_return},
    {"break", TokenKind::k_key_word_break},
    {"continue", TokenKind::k_key_word_continue},
    {"varargs", TokenKind::k_key_word_varargs},
    {"import", TokenKind::k_key_word_import},
    {"bool", TokenKind::k_key_word_bool},
    {"true", TokenKind::k_key_word_true},
    {"false", TokenKind::k_key_word_false},
    {"class", TokenKind::k_key_word_class},
    {"fun", TokenKind::k_key_word_fun},
    {"new", TokenKind::k_key_word_new},
    {"inherit", TokenKind::k_key_word_inherit},
};

Scanner::Scanner(const char * file) : filename(file)
{
    init(); 
}  

void Scanner::set_file(const char *file)
{
    this->filename = file;
    init();
}

void Scanner::init()
{
    if (!this->filename) {
        cout << "null point file path" << endl;
        exit(0);
        return;
    }

    this->input.open(this->filename);
    if (!this->input.good()) {
        cout << "can not open file: " << this->filename << endl;
        exit(0);
        return;
    }

    read_more();
    if (one == 0xef && two == 0xbb) {
        use = 0;
        read_more();
        use = 1;
        read_more();
    }
    eof = false;
}

bool Scanner::is_eof()
{
    if (use) return false;
    return this->eof;
}

void Scanner::back()
{
    ++use;
}

void Scanner::read_more()
{
    if (use == 2 || eof) return;

    if (!input.is_open()) {
        cout << "input stream not open" << endl;
        exit(-1);
        return;
    }

    lint8_t c = 0;

    if (use == 1) {
        one = two;
        two = (c = input.get()) == EOF ? 0 : c;
        if (two != 0) ++use;
    }
    else {
        one = (c = input.get()) == EOF ? 0 : c;
        if (one != 0) ++use;
        two = (c = input.get()) == EOF ? 0 : c;
        if (two != 0) ++use;
    }

    if (!input.good()) {
        eof = true;
        return;
    }
}

luint8_t Scanner::peek()
{
    if (eof && use == 1) return one;
    if (use == 2) return one;
    else if (use == 1) return two;
    return 0;
}

luint8_t Scanner::peek1()
{
    if (use < 2) {
        read_more();
    }
    return two;
}

luint8_t Scanner::read()
{
    if (use) --use;
    if (eof) return 0;
    if (use == 0) read_more();
    luint8_t res = 0;
    if (use == 1) res = two;
    if (use == 2) res = one;
    return res;
}


void TokenReader::set_scanner(Scanner *scanner)
{
    this->scanner = scanner;
}

Token * TokenReader::get_head()
{
    return this->head;
}

void TokenReader::set_head(Token *head)
{
    this->head = head;
}

Token * TokenReader::get_cur()
{
    return this->cur;
}

bool TokenReader::is_eof()
{
    return this->scanner->is_eof();
}

static bool is_alpha(luint8_t ch)
{
    return ch == '_' || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}

static bool is_digit(luint8_t ch)
{
    return ch >= '0' && ch <= '9';
}

static bool is_blank(luint8_t ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

static void read_hex(Scanner *sc, Token *t)
{
    luint8_t c = sc->peek();
    while (true) {
        if (!is_digit(c) && c != '_' && !(c >= 'a' && c <= 'f') && !(c >= 'A' && c <= 'F')) {
            break;
        }
        t->strval.push_back(c);
        c = sc->read();
    }

    int res = stoi(t->strval.c_str(), 0, 16);
    t->ival = res;
}

static int skip_comment(Scanner *sc, string end)
{
    luint8_t ch = sc->read();
    int c = 0, i = 0, line = 0;
    if (ch == '\n') ++line;
    while (!sc->is_eof()) {
        
        if (i == end.size()) break;

        if (ch == end[i]) ++i;
        else i = 0;
        ch = sc->read();
        if (ch == '\n') ++line;
    }
    return line;
}

Token * TokenReader::next()
{
start:
    luint8_t ch = scanner->peek();
    if (ch == '\n') ++line;
    while (ch != 0 && is_blank(ch) && !is_eof()) {
        ch = scanner->read();
        if (cur) cur->is_space = true;
        if (ch == '\n') {
            ++line;
            if (cur) cur->newline = true;
        }
    }

    ch = scanner->peek();
    if (is_eof() || ch == 0 || ch == '\n') {
        scanner->read();
        return nullptr;
    }

    Token *t = new Token;
    t->filename = scanner->filename;
    t->lineno = line;
    t->strval.push_back(ch);
    
    if (is_digit(ch)) {
        bool dot = false;
        scanner->read();
        while (true) {
            luint8_t c = scanner->peek();
            // 0x1212
            if (c == 'x' || c == 'X') {
                if (dot) {
                    cout << "invalid number!\n";
                    exit(-1);
                }

                t->strval.push_back(c);
                scanner->read();
                read_hex(scanner, t);
                break;
            }

            if (dot && c == '.') {
                // TODO error
                break;
            }

            if (c == '.') {
                dot = true;
                t->strval.push_back(c);
            }
            else if (is_digit(c)) t->strval.push_back(c);
            else break;

            scanner->read();
        }
        
        if (dot && scanner->peek() == '.') {
            if (!t->strval.empty()) {
                t->strval.pop_back();
                scanner->back();
                dot = false;
            }
        }

        if (dot) {
            t->dval = std::atof(t->strval.c_str());
            t->kind = TokenKind::k_number;
        }
        else {
            t->ival = std::atoi(t->strval.c_str());
            t->kind = TokenKind::k_integer;
        }
    }
    else if (is_alpha(ch)) {
        luint8_t c = scanner->read();
        while ((is_alpha(c) || is_digit(c)) && !is_eof()) {
            t->strval.push_back(c);
            c = scanner->read();
        }

        if (keywords.count(t->strval)) 
            t->kind = keywords[t->strval];
        else 
            t->kind = TokenKind::k_identity;

        //scanner->peek1();
        t->newline = scanner->peek() == '\n';
        t->is_space = is_blank(scanner->peek());
    }
    else {
        switch (ch) {
            case '"': {
                t->strval.pop_back();
                luint8_t c = scanner->read();
                while (c != '"' && !is_eof()) {
                    
                    if ((c & 0x80) == 0x00) {
                        t->strval.push_back(c);
                    } else if ((c & 0xE0) == 0xC0) { // 下面是正常读取中文字符的
                        t->strval.push_back(c);
                        t->strval.push_back(scanner->read());
                    } else if ((c & 0xF0) == 0xE0) {
                        t->strval.push_back(c);
                        t->strval.push_back(scanner->read());
                        t->strval.push_back(scanner->read());
                    } else if ((c & 0xF8) == 0xF0) {
                        t->strval.push_back(c);
                        t->strval.push_back(scanner->read());
                        t->strval.push_back(scanner->read());
                        t->strval.push_back(scanner->read());
                    }
                    
                    c = scanner->read();
                }
                scanner->peek1();
                t->kind = TokenKind::k_string;
                break;
            }
            case '+': {
                luint8_t c = scanner->read();
                if (c == '=') {
                    t->strval.push_back(c);
                    t->kind = TokenKind::k_oper_plus_assign;
                }
                else if (c == '+') {
                    t->strval.push_back(c);
                    t->kind = TokenKind::k_oper_plus_plus;
                }
                else {
                    t->kind = TokenKind::k_oper_plus;
                    scanner->back();
                }
                break;
            }
            case '-':{
                luint8_t c = scanner->read();
                if (c == '-') {
                    t->strval.push_back(c);
                    t->kind = TokenKind::k_oper_sub_sub;
                }
                else if (c == '=') {
                    t->strval.push_back(c);
                    t->kind = TokenKind::k_oper_minus_assign;
                }
                else if (c == '>') {
                    t->strval.push_back(c);
                    t->kind = TokenKind::k_oper_pointer;
                }
                else {
                    t->kind = TokenKind::k_oper_minus;
                    scanner->back();
                }
                break;
            }
            case '*':{
                luint8_t c = scanner->read();
                if (c == '/') {
                    t->strval.push_back(c);
                    t->kind = TokenKind::k_symbol_comment1;
                }
                else if (c == '=') {
                    t->strval.push_back(c);
                    t->kind = TokenKind::k_oper_mul_assign;
                }
                else {
                    t->kind = TokenKind::k_oper_mul;
                    scanner->back();
                }
                break;
            }
            case '/':{
                luint8_t c = scanner->read();
                if (c == '/') {
                    delete t;
                    skip_comment(scanner, "\n");
                    ++line;
                    if (cur) cur->newline = true;
                    
                    goto start;
                }
                else if (c == '*') {
                    delete t;
                    line += skip_comment(scanner, "*/");
                    scanner->read();
                    goto start;
                }
                else if (c == '=') {
                    t->strval.push_back(c);
                    t->kind = TokenKind::k_oper_div_assign;
                }
                else {
                    t->kind = TokenKind::k_oper_div;
                    scanner->back();
                }
                break;
            }
            case '%': {
                luint8_t c = scanner->read();
                if (c == '=') {
                    t->strval.push_back(c);
                    t->kind = TokenKind::k_oper_mod_assign;
                }
                else {
                    t->kind = TokenKind::k_oper_mod;
                    scanner->back();
                }
                break;
            }
            case '=': {
                luint8_t c = scanner->read();
                if (c == '=') {
                    t->strval.push_back(c);
                    t->kind = TokenKind::k_cmp_eq;
                }
                else if (c == '=') {
                    t->strval.push_back(c);
                    t->kind = TokenKind::k_oper_plus_assign;
                }
                else {
                    t->kind = TokenKind::k_oper_assign;
                    scanner->back();
                }
                break;
            }
            case '>': {
                luint8_t c = scanner->read();
                if (c == '=') {
                    t->strval.push_back(c);
                    t->kind = TokenKind::k_cmp_gte;
                }
                else if (c == '>') {
                    t->strval.push_back(c);
                    t->kind = TokenKind::k_oper_bin_rm;
                } else {
                    t->kind = TokenKind::k_cmp_gt;
                    scanner->back();
                }
                break;
            }
            case '<': {
                luint8_t c = scanner->read();
                if (c == '=') {
                    t->strval.push_back(c);
                    t->kind = TokenKind::k_cmp_lte;
                }
                else if (c == '<') {
                    t->strval.push_back(c);
                    t->kind = TokenKind::k_oper_bin_lm;
                } else {
                    t->kind = TokenKind::k_cmp_lt;
                    scanner->back();
                }
                break;
            }
            case '!': {
                luint8_t c = scanner->read();
                if (c == '=') {
                    t->strval.push_back(c);
                    t->kind = TokenKind::k_cmp_neq;
                }
                else {
                    t->kind = TokenKind::k_cmp_not;
                    scanner->back();
                }
                break;
            }
            case '&': {
                luint8_t c = scanner->read();
                if (c == '&') {
                    t->strval.push_back(c);
                    t->kind = TokenKind::k_cmp_and;
                }
                else {
                    t->kind = TokenKind::k_oper_bin_and;
                    scanner->back();
                }
                break;
            }
            case '|': {
                luint8_t c = scanner->read();
                if (c == '|') {
                    t->strval.push_back(c);
                    t->kind = TokenKind::k_cmp_or;
                }
                else {
                    t->kind = TokenKind::k_oper_bin_or;
                    scanner->back();
                }
                break;
            }
            case '(': {
                t->kind = TokenKind::k_symbol_qs1;
                break;
            }
            case ')':{
                t->kind = TokenKind::k_symbol_qs2;
                break;
            }
            case '[':{
                t->kind = TokenKind::k_symbol_qm1;
                break;
            }
            case ']':{
                t->kind = TokenKind::k_symbol_qm2;
                break;
            }
            case '{':{
                t->kind = TokenKind::k_symbol_qg1;
                break;
            }
            case '}':{
                t->kind = TokenKind::k_symbol_qg2;
                break;
            }
            case ',':{
                t->kind = TokenKind::k_symbol_sep;
                break;
            }
            case ':':{
                t->kind = TokenKind::k_symbol_show;
                break;
            }
            case '\\': {
                t->kind = TokenKind::k_symbol_next;
                break;
            }
            case '#': {
                t->kind = TokenKind::k_symbol_no;
                break;
            }
            case '?': {
                t->kind = TokenKind::k_cmp_quetion;
                break;
            }
            case ';':{
                t->kind = TokenKind::k_symbol_co;
                break;
            }
            case '.': {
                luint8_t c = scanner->read();
                if (c == '.') {
                    t->strval.push_back(c);
                    c = scanner->read();
                    if (c == '.') {
                        t->strval.push_back(c);
                        t->kind = TokenKind::k_symbol_var_arg;
                    } else {
                        t->kind = TokenKind::k_symbol_sub_arr;
                        scanner->back();
                    }
                }
                else {
                    t->kind = TokenKind::k_symbol_dot;
                    scanner->back();
                }
                break;
            }
            case '\'': {
                t->strval.clear();
                luint8_t c = scanner->read();
                t->strval.push_back(c);
                t->kind = TokenKind::k_identity;
                c = scanner->read();
                if (c != '\'') {
                    cout << "invalid character!\n";
                    exit(-1);
                }
                break;
            }
            
            default: {
                delete t;
                return nullptr;
            }
        }

        if (!scanner->is_eof()) {
            scanner->peek1();
            t->newline = scanner->peek() == '\n';
            t->is_space = is_blank(scanner->peek());
            scanner->read();
        }
    }

    if (!t->is_space) t->is_space = is_blank(scanner->peek());
    if (!t->newline) t->newline = scanner->peek() == '\n';

    if (!head) {
        head = t;
        cur = t;
    }
    else {
        cur->next = t;
        cur = t;
    }

    return t;
}

