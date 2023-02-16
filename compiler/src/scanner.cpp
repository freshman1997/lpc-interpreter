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
        assert(0);
        return;
    }

    this->input.open(this->filename);
    if (!this->input.good()) {
        cout << "can not open file: " << this->filename << endl;
        assert(0);
    }

    eof = false;
}

bool Scanner::is_eof()
{
    return this->eof;
}

void Scanner::read_more()
{
    if (!input.is_open()) {
        cout << "input stream not open" << endl;
        assert(0);
        return;
    }
    one = input.good() ? input.get() : 0;
    two = input.good() ? input.get() : 0;

    if (!input.good()) {
        eof = true;
        return;
    }

    use = 2;
}

luint8_t Scanner::peek()
{
    if (use == 0) read_more();
    if (use == 2) return one;
    return two;
}

luint8_t Scanner::peek1()
{
    if (use == 0) {
        read_more();
        return one;
    }
    return two;
}

luint8_t Scanner::read()
{
    luint8_t res = 0;
    if (use == 0) read_more();
    --use;
    return peek();
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

static void skip_space(Scanner *scanner, int *line)
{
    luint8_t ch = scanner->peek();
    while (is_blank(ch)) {
        ch = scanner->read();
        // TODO
        if (ch == '\n') ++(*line);
    }
}

Token * TokenReader::next()
{
    luint8_t ch = scanner->peek();
    while (is_blank(ch)) {
        ch = scanner->read();
        // TODO
        if (ch == '\n') ++line;
    }

    Token * t = new Token;
    t->strval.push_back(ch);
    
    if (is_digit(ch)) {
        bool dot = false;
        scanner->read();
        while (true) {
            luint8_t c = scanner->peek();
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
        scanner->read();
        while ((is_alpha(scanner->peek()) || is_digit(scanner->peek())) && !is_eof()) {
            luint8_t c = scanner->peek();
            t->strval.push_back(c);
            scanner->read();
        }

        if (t->strval == "r1") {
            cout << "hello";
        }

        if (keywords.count(t->strval)) 
            t->kind = keywords[t->strval];
        else 
            t->kind = TokenKind::k_identity;
        
    }
    else {
        switch (ch) {
            case '"': {
                scanner->read();
                t->strval.pop_back();
                while (scanner->peek() != '"' && !is_eof()) {
                    luint8_t c = scanner->peek();
                    t->strval.push_back(c);
                    scanner->read();
                }
                
                t->kind = TokenKind::k_string;
                break;
            }
            case '+': {
                scanner->read();
                luint8_t c = scanner->peek();
                if (c == '=') {
                    t->strval.push_back(c);
                    t->kind = TokenKind::k_oper_plus_assign;
                    scanner->read();
                }
                else t->kind = TokenKind::k_oper_plus;
                break;
            }
            case '-':{
                scanner->read();
                luint8_t c = scanner->peek();
                if (c == '-') {
                    t->kind = TokenKind::k_oper_sub_sub;
                    scanner->read();
                }
                else if (c == '=') {
                    t->kind = TokenKind::k_oper_minus_assign;
                    scanner->read();
                }
                else t->kind = TokenKind::k_oper_minus;
                break;
            }
            case '*':{
                scanner->read();
                luint8_t c = scanner->peek();
                if (c == '/') {
                    t->kind = TokenKind::k_symbol_comment1;
                    scanner->read();
                }
                else if (c == '=') {
                    t->kind = TokenKind::k_oper_mul_assign;
                    scanner->read();
                }
                else t->kind = TokenKind::k_oper_mul;
                break;
            }
            case '/':{
                scanner->read();
                luint8_t c = scanner->peek();
                
                if (c == '/') {
                    t->kind = TokenKind::k_oper_sub_sub;
                    scanner->read();
                }
                else if (c == '*') {
                    t->kind = TokenKind::k_symbol_comment2;
                    scanner->read();
                }
                else if (c == '=') {
                    t->kind = TokenKind::k_oper_div_assign;
                    scanner->read();
                }
                else t->kind = TokenKind::k_oper_div;
                break;
            }
            case '%': {
                scanner->read();
                luint8_t c = scanner->peek();
                if (c == '=') {
                    t->kind = TokenKind::k_oper_mod_assign;
                    scanner->read();
                }
                else t->kind = TokenKind::k_oper_mod;
                break;
            }
            case '=': {
                scanner->read();
                luint8_t c = scanner->peek();
                if (c == '=') {
                    t->kind = TokenKind::k_cmp_eq;
                    scanner->read();
                }
                else if (c == '=') {
                    t->kind = TokenKind::k_oper_plus_assign;
                    scanner->read();
                }
                else t->kind = TokenKind::k_oper_assign;
                break;
            }
            case '>': {
                scanner->read();
                luint8_t c = scanner->peek();
                if (c == '=') {
                    t->kind = TokenKind::k_cmp_gte;
                    scanner->read();
                }
                else t->kind = TokenKind::k_cmp_gt;
                break;
            }
            case '<': {
                scanner->read();
                luint8_t c = scanner->peek();
                if (c == '=') {
                    t->kind = TokenKind::k_cmp_lte;
                    scanner->read();
                }
                else t->kind = TokenKind::k_cmp_lt;
                break;
            }
            case '!': {
                scanner->read();
                luint8_t c = scanner->peek();
                if (c == '=') {
                    t->kind = TokenKind::k_cmp_neq;
                    scanner->read();
                }
                else t->kind = TokenKind::k_cmp_not;
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
            default: {
                delete t;
                return NULL;
            }
        }

        t->newline = scanner->peek() == '\n';
        t->is_space = is_blank(scanner->peek());
        scanner->read();
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

