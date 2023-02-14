#include <cassert>
#include <iostream>
#include "scanner.h"

using namespace std;

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
    if (use == 2) return two;
    if (use == 1) return one;
    return 0;
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
    if (use == 2) res = two;
    if (use == 1) res = one;
    --use;
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
    return this->scanner->peek() == 0;
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
    return ch == ' ' || ch == '\t' || ch == '\n';
}

Token * TokenReader::next()
{
    luint8_t ch = scanner->peek();
    while (is_blank(ch)) {
        ch = scanner->read();
    }

    Token * t = new Token;
    if (is_digit(ch)) {
        t->strval.push_back(ch);
        bool dot = false;
        while (true) {
            luint8_t c = scanner->peek();
            if (dot && c == '.') {
                // TODO error
                break;
            }

            if (c == '.') {
                dot = true;
            }
            else if (is_digit(c)) t->strval.push_back(c);
            else break;
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
        t->strval.push_back(ch);
        while (is_alpha(scanner->peek()) && !is_eof()) {
            luint8_t c = scanner->peek();
            t->strval.push_back(c);
            scanner->read();
        }
        t->kind = TokenKind::k_identity;
    }
    else {
        switch (ch) {
            case '"': {
                scanner->read();
                while (scanner->peek() != '"' && !is_eof()) {
                    luint8_t c = scanner->peek();
                    t->strval.push_back(c);
                    scanner->read();
                }
                t->kind = TokenKind::k_string;
                break;
            }
            case '+': {
                luint8_t c = scanner->peek1();
                if (c == '=') {
                    t->kind = TokenKind::k_oper_plus_assign;
                    scanner->read();
                }
                else t->kind = TokenKind::k_oper_plus;
                break;
            }
            case '-':{
                luint8_t c = scanner->peek1();
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
                luint8_t c = scanner->peek1();
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
                luint8_t c = scanner->peek1();
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
                luint8_t c = scanner->peek1();
                if (c == '=') {
                    t->kind = TokenKind::k_oper_mod_assign;
                    scanner->read();
                }
                else t->kind = TokenKind::k_oper_mod;
                break;
            }
            case '=': {
                luint8_t c = scanner->peek1();
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

            case '!': {
                luint8_t c = scanner->peek1();
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
            default: {
                delete t;
                return NULL;
            }
        }

        scanner->read();
    }

    return t;
}

