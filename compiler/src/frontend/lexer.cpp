#include "frontend/lexer.h"

#include <cctype>

namespace lpc {
namespace frontend {

std::vector<Token> Lexer::Tokenize(const SourceFile &source) {
    source_ = &source;
    offset_ = 0;
    line_ = 1;
    column_ = 1;
    if (source_->text.size() >= 3 &&
        static_cast<unsigned char>(source_->text[0]) == 0xEF &&
        static_cast<unsigned char>(source_->text[1]) == 0xBB &&
        static_cast<unsigned char>(source_->text[2]) == 0xBF) {
        offset_ = 3;
    }

    std::vector<Token> out;
    while (!IsAtEnd()) {
        SkipWhitespace();
        if (IsAtEnd()) {
            break;
        }
        out.push_back(ScanOne());
    }

    Token eof;
    eof.kind = TokenKind::EndOfFile;
    eof.lexeme = "";
    eof.span.line = line_;
    eof.span.column = column_;
    eof.span.length = 0;
    out.push_back(eof);
    return out;
}

Token Lexer::ScanOne() {
    const int start_line = line_;
    const int start_col = column_;
    const char c = Advance();

    if (std::isdigit(static_cast<unsigned char>(c))) {
        --offset_;
        --column_;
        return ScanNumber();
    }
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        --offset_;
        --column_;
        return ScanIdentifierOrKeyword();
    }

    switch (c) {
    case '(':
        if (Match('{')) return MakeToken(TokenKind::LArrInit, start_line, start_col, 2);
        if (Match('[')) return MakeToken(TokenKind::LMapInit, start_line, start_col, 2);
        return MakeToken(TokenKind::LParen, start_line, start_col, 1);
    case ')': return MakeToken(TokenKind::RParen, start_line, start_col, 1);
    case '{': return MakeToken(TokenKind::LBrace, start_line, start_col, 1);
    case '}':
        if (Match(')')) return MakeToken(TokenKind::RArrInit, start_line, start_col, 2);
        return MakeToken(TokenKind::RBrace, start_line, start_col, 1);
    case '[':
        return MakeToken(TokenKind::LBracket, start_line, start_col, 1);
    case ']':
        return MakeToken(TokenKind::RBracket, start_line, start_col, 1);
    case ',': return MakeToken(TokenKind::Comma, start_line, start_col, 1);
    case ':': return MakeToken(TokenKind::Colon, start_line, start_col, 1);
    case ';': return MakeToken(TokenKind::Semicolon, start_line, start_col, 1);
    case '"': return ScanString();
    case '+':
        if (Match('+')) return MakeToken(TokenKind::PlusPlus, start_line, start_col, 2);
        if (Match('=')) return MakeToken(TokenKind::PlusAssign, start_line, start_col, 2);
        return MakeToken(TokenKind::Plus, start_line, start_col, 1);
    case '-':
        if (Match('-')) return MakeToken(TokenKind::MinusMinus, start_line, start_col, 2);
        if (Match('=')) return MakeToken(TokenKind::MinusAssign, start_line, start_col, 2);
        if (Match('>')) return MakeToken(TokenKind::Arrow, start_line, start_col, 2);
        return MakeToken(TokenKind::Minus, start_line, start_col, 1);
    case '*':
        if (Match('=')) return MakeToken(TokenKind::StarAssign, start_line, start_col, 2);
        return MakeToken(TokenKind::Star, start_line, start_col, 1);
    case '/':
        if (Match('=')) return MakeToken(TokenKind::SlashAssign, start_line, start_col, 2);
        return MakeToken(TokenKind::Slash, start_line, start_col, 1);
    case '%':
        if (Match('=')) return MakeToken(TokenKind::PercentAssign, start_line, start_col, 2);
        return MakeToken(TokenKind::Percent, start_line, start_col, 1);
    case '&':
        if (Match('&')) return MakeToken(TokenKind::LogicAnd, start_line, start_col, 2);
        if (Match('=')) return MakeToken(TokenKind::AmpAssign, start_line, start_col, 2);
        return MakeToken(TokenKind::Amp, start_line, start_col, 1);
    case '|':
        if (Match('|')) return MakeToken(TokenKind::LogicOr, start_line, start_col, 2);
        if (Match('=')) return MakeToken(TokenKind::PipeAssign, start_line, start_col, 2);
        return MakeToken(TokenKind::Pipe, start_line, start_col, 1);
    case '^':
        if (Match('=')) return MakeToken(TokenKind::CaretAssign, start_line, start_col, 2);
        return MakeToken(TokenKind::Caret, start_line, start_col, 1);
    case '~':
        return MakeToken(TokenKind::Tilde, start_line, start_col, 1);
    case '!':
        if (Match('=')) return MakeToken(TokenKind::NotEq, start_line, start_col, 2);
        return MakeToken(TokenKind::Bang, start_line, start_col, 1);
    case '=':
        if (Match('=')) return MakeToken(TokenKind::EqEq, start_line, start_col, 2);
        return MakeToken(TokenKind::Assign, start_line, start_col, 1);
    case '<':
        if (Match('<')) {
            if (Match('=')) return MakeToken(TokenKind::ShiftLeftAssign, start_line, start_col, 3);
            return MakeToken(TokenKind::ShiftLeft, start_line, start_col, 2);
        }
        if (Match('=')) return MakeToken(TokenKind::Lte, start_line, start_col, 2);
        return MakeToken(TokenKind::Lt, start_line, start_col, 1);
    case '>':
        if (Match('>')) {
            if (Match('=')) return MakeToken(TokenKind::ShiftRightAssign, start_line, start_col, 3);
            return MakeToken(TokenKind::ShiftRight, start_line, start_col, 2);
        }
        if (Match('=')) return MakeToken(TokenKind::Gte, start_line, start_col, 2);
        return MakeToken(TokenKind::Gt, start_line, start_col, 1);
    case '?':
        return MakeToken(TokenKind::Question, start_line, start_col, 1);
    case '.':
        if (Match('.')) return MakeToken(TokenKind::DotDot, start_line, start_col, 2);
        return MakeToken(TokenKind::Unknown, start_line, start_col, 1);
    default:
        if (diag_) {
            SourceSpan span;
            span.line = start_line;
            span.column = start_col;
            span.length = 1;
            diag_->Add(DiagnosticLevel::Error, span, "Unexpected character");
        }
        return MakeToken(TokenKind::Unknown, start_line, start_col, 1);
    }
}

void Lexer::SkipWhitespace() {
    while (!IsAtEnd()) {
        const char c = Peek();
        if (c == ' ' || c == '\t' || c == '\r') {
            Advance();
            continue;
        }
        if (c == '\n') {
            Advance();
            continue;
        }
        if (c == '/' && (offset_ + 1) < static_cast<int>(source_->text.size()) && source_->text[offset_ + 1] == '/') {
            while (!IsAtEnd() && Peek() != '\n') {
                Advance();
            }
            continue;
        }
        if (c == '/' && (offset_ + 1) < static_cast<int>(source_->text.size()) && source_->text[offset_ + 1] == '*') {
            Advance();
            Advance();
            while (!IsAtEnd()) {
                if (Peek() == '*' && (offset_ + 1) < static_cast<int>(source_->text.size()) && source_->text[offset_ + 1] == '/') {
                    Advance();
                    Advance();
                    break;
                }
                Advance();
            }
            continue;
        }
        break;
    }
}

Token Lexer::ScanNumber() {
    const int start_line = line_;
    const int start_col = column_;
    const int start_offset = offset_;
    if (Peek() == '0' && !IsAtEnd()) {
        Advance();
        if (!IsAtEnd() && (Peek() == 'x' || Peek() == 'X')) {
            Advance();
            while (!IsAtEnd() && std::isxdigit(static_cast<unsigned char>(Peek()))) {
                Advance();
            }
            return MakeToken(TokenKind::Number, start_line, start_col, offset_ - start_offset);
        }
    }
    while (!IsAtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) {
        Advance();
    }
    if (!IsAtEnd() && Peek() == '.') {
        int dot_pos = offset_;
        Advance();
        if (!IsAtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) {
            while (!IsAtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) {
                Advance();
            }
            return MakeToken(TokenKind::Float, start_line, start_col, offset_ - start_offset);
        } else {
            offset_ = dot_pos;
            column_ = start_col + (dot_pos - start_offset);
        }
    }
    return MakeToken(TokenKind::Number, start_line, start_col, offset_ - start_offset);
}

Token Lexer::ScanIdentifierOrKeyword() {
    const int start_line = line_;
    const int start_col = column_;
    const int start_offset = offset_;
    while (!IsAtEnd()) {
        const char c = Peek();
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            Advance();
            continue;
        }
        break;
    }
    Token tok = MakeToken(TokenKind::Identifier, start_line, start_col, offset_ - start_offset);
    if (tok.lexeme == "fun") tok.kind = TokenKind::KeywordFun;
    else if (tok.lexeme == "var") tok.kind = TokenKind::KeywordVar;
    else if (tok.lexeme == "return") tok.kind = TokenKind::KeywordReturn;
    else if (tok.lexeme == "if") tok.kind = TokenKind::KeywordIf;
    else if (tok.lexeme == "else") tok.kind = TokenKind::KeywordElse;
    else if (tok.lexeme == "while") tok.kind = TokenKind::KeywordWhile;
    else if (tok.lexeme == "for") tok.kind = TokenKind::KeywordFor;
    else if (tok.lexeme == "switch") tok.kind = TokenKind::KeywordSwitch;
    else if (tok.lexeme == "case") tok.kind = TokenKind::KeywordCase;
    else if (tok.lexeme == "default") tok.kind = TokenKind::KeywordDefault;
    else if (tok.lexeme == "break") tok.kind = TokenKind::KeywordBreak;
    else if (tok.lexeme == "continue") tok.kind = TokenKind::KeywordContinue;
    else if (tok.lexeme == "catch") tok.kind = TokenKind::KeywordCatch;
    else if (tok.lexeme == "foreach") tok.kind = TokenKind::KeywordForeach;
    else if (tok.lexeme == "in") tok.kind = TokenKind::KeywordIn;
    else if (tok.lexeme == "class") tok.kind = TokenKind::KeywordClass;
    else if (tok.lexeme == "new") tok.kind = TokenKind::KeywordNew;
    else if (tok.lexeme == "inherit") tok.kind = TokenKind::KeywordInherit;
    else if (tok.lexeme == "void") tok.kind = TokenKind::KeywordVoid;
    else if (tok.lexeme == "int") tok.kind = TokenKind::KeywordInt;
    else if (tok.lexeme == "float") tok.kind = TokenKind::KeywordFloat;
    else if (tok.lexeme == "string") tok.kind = TokenKind::KeywordStringType;
    else if (tok.lexeme == "object") tok.kind = TokenKind::KeywordObject;
    else if (tok.lexeme == "mapping") tok.kind = TokenKind::KeywordMapping;
    else if (tok.lexeme == "mixed") tok.kind = TokenKind::KeywordMixed;
    else if (tok.lexeme == "static") tok.kind = TokenKind::KeywordStatic;
    else if (tok.lexeme == "private") tok.kind = TokenKind::KeywordPrivate;
    else if (tok.lexeme == "public") tok.kind = TokenKind::KeywordPublic;
    else if (tok.lexeme == "nomask") tok.kind = TokenKind::KeywordNomask;
    else if (tok.lexeme == "true") tok.kind = TokenKind::KeywordTrue;
    else if (tok.lexeme == "false") tok.kind = TokenKind::KeywordFalse;
    else if (tok.lexeme == "do") tok.kind = TokenKind::KeywordDo;
    return tok;
}

Token Lexer::ScanString() {
    const int start_line = line_;
    const int start_col = column_;
    const int begin = offset_ - 1;
    while (!IsAtEnd()) {
        if (Peek() == '\\') {
            Advance();
            if (!IsAtEnd()) Advance();
            continue;
        }
        if (Peek() == '"') break;
        Advance();
    }
    if (IsAtEnd()) {
        SourceSpan span;
        span.line = start_line;
        span.column = start_col;
        span.length = column_ - start_col;
        if (diag_) {
            diag_->Add(DiagnosticLevel::Error, span, "Unterminated string literal");
        }
        return MakeToken(TokenKind::String, start_line, start_col, offset_ - begin);
    }
    Advance();
    return MakeToken(TokenKind::String, start_line, start_col, offset_ - begin);
}

Token Lexer::MakeToken(TokenKind kind, int start_line, int start_col, int length) {
    Token tok;
    tok.kind = kind;
    tok.span.line = start_line;
    tok.span.column = start_col;
    tok.span.length = length;

    const int line_delta = line_ - start_line;
    (void)line_delta;
    int start = offset_ - length;
    if (start < 0) start = 0;
    if (start + length <= static_cast<int>(source_->text.size())) {
        tok.lexeme = source_->text.substr(start, length);
    }
    return tok;
}

bool Lexer::Match(char ch) {
    if (IsAtEnd() || Peek() != ch) {
        return false;
    }
    Advance();
    return true;
}

char Lexer::Peek() const {
    if (IsAtEnd()) {
        return '\0';
    }
    return source_->text[offset_];
}

char Lexer::Advance() {
    const char c = source_->text[offset_++];
    if (c == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return c;
}

bool Lexer::IsAtEnd() const {
    return offset_ >= static_cast<int>(source_->text.size());
}

} // namespace frontend
} // namespace lpc
