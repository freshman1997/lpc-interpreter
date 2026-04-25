#ifndef LPC_FRONTEND_LEXER_H
#define LPC_FRONTEND_LEXER_H

#include <vector>

#include "frontend/diagnostic.h"
#include "frontend/token.h"

namespace lpc {
namespace frontend {

class Lexer {
public:
    explicit Lexer(DiagnosticSink *diag) : diag_(diag) {}

    std::vector<Token> Tokenize(const SourceFile &source);

private:
    Token ScanOne();
    void SkipWhitespace();
    Token ScanNumber();
    Token ScanIdentifierOrKeyword();
    Token ScanString();
    Token MakeToken(TokenKind kind, int start_line, int start_col, int length);
    bool Match(char ch);
    char Peek() const;
    char Advance();
    bool IsAtEnd() const;

    const SourceFile *source_ = nullptr;
    DiagnosticSink *diag_ = nullptr;
    int offset_ = 0;
    int line_ = 1;
    int column_ = 1;
};

} // namespace frontend
} // namespace lpc

#endif
