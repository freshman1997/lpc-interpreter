#ifndef LPC_FRONTEND_PARSER_H
#define LPC_FRONTEND_PARSER_H

#include <memory>
#include <vector>

#include "frontend/ast.h"
#include "frontend/diagnostic.h"
#include "frontend/token.h"

namespace lpc {
namespace frontend {

class Parser {
public:
    explicit Parser(DiagnosticSink *diag) : diag_(diag) {}

    std::unique_ptr<Module> Parse(const std::vector<Token> &tokens);

private:
    std::unique_ptr<Stmt> ParseDeclOrStmt();
    std::unique_ptr<Stmt> ParseClassDecl();
    std::unique_ptr<Stmt> ParseInheritDecl();
    std::unique_ptr<Stmt> ParseFunctionDecl(bool lambda);
    std::unique_ptr<Stmt> ParseStatement();
    std::unique_ptr<Stmt> ParseIf();
    std::unique_ptr<Stmt> ParseWhile();
    std::unique_ptr<Stmt> ParseDoWhile();
    std::unique_ptr<Stmt> ParseFor();
    std::unique_ptr<Stmt> ParseForeach();
    std::unique_ptr<Stmt> ParseSwitch();
    std::unique_ptr<Stmt> ParseBreak();
    std::unique_ptr<Stmt> ParseContinue();
    std::unique_ptr<Stmt> ParseCatch();
    std::unique_ptr<Stmt> ParseReturn();
    std::unique_ptr<Stmt> ParseVarDecl();
    std::unique_ptr<Stmt> ParseVarDeclNoSemi();
    std::unique_ptr<Stmt> ParseBlock();
    std::unique_ptr<Stmt> ParseExprStmt();
    std::unique_ptr<Stmt> ParseTypedVarDeclWithPrefix(
        const std::vector<std::string> &decorators,
        const std::string &decl_type,
        bool is_pointer,
        bool need_semi);
    std::unique_ptr<Stmt> ParseTypedFunctionDeclWithPrefix(
        const std::vector<std::string> &decorators,
        const std::string &ret_type,
        bool ret_is_pointer);

    std::unique_ptr<Expr> ParseExpr();
    std::unique_ptr<Expr> ParseAssignment();
    std::unique_ptr<Expr> ParseTernary();
    std::unique_ptr<Expr> ParseLogicalOr();
    std::unique_ptr<Expr> ParseLogicalAnd();
    std::unique_ptr<Expr> ParseBitOr();
    std::unique_ptr<Expr> ParseBitXor();
    std::unique_ptr<Expr> ParseBitAnd();
    std::unique_ptr<Expr> ParseEquality();
    std::unique_ptr<Expr> ParseRelational();
    std::unique_ptr<Expr> ParseShift();
    std::unique_ptr<Expr> ParseAdditive();
    std::unique_ptr<Expr> ParseMultiplicative();
    std::unique_ptr<Expr> ParseUnary();
    std::unique_ptr<Expr> ParsePostfix();
    std::unique_ptr<Expr> ParsePrimary();
    std::unique_ptr<Expr> ParseLambdaExpr();
    std::unique_ptr<Expr> ParseArrayLiteralExpr();
    std::unique_ptr<Expr> ParseMappingLiteralExpr();
    std::unique_ptr<Expr> ParseBraceLiteralExpr();

    bool IsDecoratorToken(TokenKind kind) const;
    bool IsTypeStartToken(TokenKind kind) const;
    std::string TypeNameFromToken(const Token &tok) const;
    bool ParseOptionalType(std::string *decl_type, bool *is_pointer);
    std::vector<std::string> ParseDecorators();

    bool Match(TokenKind kind);
    bool Check(TokenKind kind) const;
    const Token &Advance();
    const Token &Peek() const;
    const Token &Previous() const;
    bool IsAtEnd() const;
    bool Expect(TokenKind kind, const char *message);

    std::vector<Token> tokens_;
    size_t current_ = 0;
    DiagnosticSink *diag_ = nullptr;
};

} // namespace frontend
} // namespace lpc

#endif
