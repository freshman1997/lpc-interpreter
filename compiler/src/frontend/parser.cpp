#include "frontend/parser.h"

namespace lpc {
namespace frontend {

std::unique_ptr<Module> Parser::Parse(const std::vector<Token> &tokens) {
    tokens_ = tokens;
    current_ = 0;

    std::unique_ptr<Module> mod(new Module());
    while (!IsAtEnd()) {
        const size_t before = current_;
        std::unique_ptr<Stmt> node = ParseDeclOrStmt();
        if (node) {
            mod->decls.push_back(std::move(node));
            if (current_ == before) {
                Advance();
            }
            continue;
        }
        if (current_ == before) {
            Advance();
        }
    }
    return mod;
}

std::unique_ptr<Stmt> Parser::ParseDeclOrStmt() {
    if (Check(TokenKind::KeywordInherit)) {
        return ParseInheritDecl();
    }
    if (Check(TokenKind::KeywordClass)) {
        return ParseClassDecl();
    }
    if (Check(TokenKind::KeywordFun)) {
        return ParseFunctionDecl(false);
    }
    if (Check(TokenKind::KeywordVar)) {
        return ParseVarDecl();
    }

    const size_t saved = current_;
    std::vector<std::string> decorators = ParseDecorators();
    std::string decl_type;
    bool is_pointer = false;
    if (ParseOptionalType(&decl_type, &is_pointer) && Check(TokenKind::Identifier)) {
        const bool has_next = (current_ + 1) < tokens_.size();
        const TokenKind next_kind = has_next ? tokens_[current_ + 1].kind : TokenKind::EndOfFile;
        if (next_kind == TokenKind::LParen) {
            return ParseTypedFunctionDeclWithPrefix(decorators, decl_type, is_pointer);
        }
        if (next_kind == TokenKind::Assign || next_kind == TokenKind::Semicolon) {
            return ParseTypedVarDeclWithPrefix(decorators, decl_type, is_pointer, true);
        }
    }
    current_ = saved;

    return ParseStatement();
}

std::unique_ptr<Stmt> Parser::ParseInheritDecl() {
    Expect(TokenKind::KeywordInherit, "expect 'inherit'");
    std::unique_ptr<InheritDecl> inh(new InheritDecl());
    inh->span = Previous().span;
    if (Match(TokenKind::Identifier) || Match(TokenKind::String)) {
        inh->parent_name = Previous().lexeme;
    } else {
        if (diag_) {
            diag_->Add(DiagnosticLevel::Error, Peek().span, "expect inherit parent object");
        }
        return nullptr;
    }
    Expect(TokenKind::Semicolon, "expect ';' after inherit");
    return std::unique_ptr<Stmt>(inh.release());
}

std::unique_ptr<Stmt> Parser::ParseClassDecl() {
    Expect(TokenKind::KeywordClass, "expect 'class'");
    if (!Expect(TokenKind::Identifier, "expect class name")) {
        return nullptr;
    }

    std::unique_ptr<ClassDecl> cl(new ClassDecl());
    cl->name = Previous().lexeme;
    cl->span = Previous().span;

    if (Match(TokenKind::Colon)) {
        if (!Expect(TokenKind::Identifier, "expect parent class name after ':'")) {
            return nullptr;
        }
        cl->parent_name = Previous().lexeme;
    }

    if (!Expect(TokenKind::LBrace, "expect '{' after class name")) {
        return nullptr;
    }
    while (!Check(TokenKind::RBrace) && !IsAtEnd()) {
        const size_t before = current_;
        if (Check(TokenKind::KeywordVar)) {
            std::unique_ptr<Stmt> vd = ParseVarDecl();
            if (vd && vd->kind == NodeKind::VarDecl) {
                VarDeclStmt *v = static_cast<VarDeclStmt *>(vd.get());
                cl->fields.push_back(v->name);
            }
        } else {
            const size_t saved = current_;
            std::vector<std::string> decorators = ParseDecorators();
            std::string decl_type;
            bool is_pointer = false;
            if (ParseOptionalType(&decl_type, &is_pointer) && Check(TokenKind::Identifier)) {
                const bool has_next = (current_ + 1) < tokens_.size();
                const TokenKind next_kind = has_next ? tokens_[current_ + 1].kind : TokenKind::EndOfFile;
                if (next_kind == TokenKind::Assign || next_kind == TokenKind::Semicolon) {
                    std::unique_ptr<Stmt> vd = ParseTypedVarDeclWithPrefix(decorators, decl_type, is_pointer, true);
                    if (vd && vd->kind == NodeKind::VarDecl) {
                        VarDeclStmt *v = static_cast<VarDeclStmt *>(vd.get());
                        cl->fields.push_back(v->name);
                    }
                } else {
                    current_ = saved;
                    Advance();
                }
            } else {
                current_ = saved;
                Advance();
            }
        }
        if (current_ == before) {
            Advance();
        }
    }
    Expect(TokenKind::RBrace, "expect '}' after class body");
    return std::unique_ptr<Stmt>(cl.release());
}

std::unique_ptr<Stmt> Parser::ParseFunctionDecl(bool lambda) {
    if (!Expect(TokenKind::KeywordFun, "expect 'fun'")) {
        return nullptr;
    }

    std::unique_ptr<FunctionDecl> fn(new FunctionDecl());
    fn->is_lambda = lambda;
    fn->span = Previous().span;

    if (!lambda) {
        if (!Expect(TokenKind::Identifier, "expect function name")) {
            return nullptr;
        }
        fn->name = Previous().lexeme;
        fn->span = Previous().span;
    }

    if (!Expect(TokenKind::LParen, "expect '('") ) {
        return nullptr;
    }
    if (!Check(TokenKind::RParen)) {
        do {
            std::string param_type;
            bool param_ptr = false;
            ParseOptionalType(&param_type, &param_ptr);
            if (!Expect(TokenKind::Identifier, "expect parameter name")) {
                return nullptr;
            }
            fn->params.push_back(Previous().lexeme);
            fn->param_spans.push_back(Previous().span);
            fn->param_types.push_back(param_ptr ? (param_type + "*") : param_type);
            fn->param_is_pointer.push_back(param_ptr);
        } while (Match(TokenKind::Comma));
    }
    if (!Expect(TokenKind::RParen, "expect ')'") ) {
        return nullptr;
    }

    if (Match(TokenKind::Arrow)) {
        std::unique_ptr<ReturnStmt> ret(new ReturnStmt());
        ret->value = ParseExpr();
        if (!ret->value) {
            return nullptr;
        }
        fn->body.push_back(std::move(ret));
    } else {
        std::unique_ptr<Stmt> body = ParseBlock();
        BlockStmt *block = body ? static_cast<BlockStmt *>(body.release()) : nullptr;
        if (!block) {
            return nullptr;
        }
        fn->body.swap(block->stmts);
        delete block;
    }

    return std::unique_ptr<Stmt>(fn.release());
}

std::unique_ptr<Stmt> Parser::ParseStatement() {
    if (Check(TokenKind::KeywordIf)) {
        return ParseIf();
    }
    if (Check(TokenKind::KeywordDo)) {
        return ParseDoWhile();
    }
    if (Check(TokenKind::KeywordWhile)) {
        return ParseWhile();
    }
    if (Check(TokenKind::KeywordFor)) {
        return ParseFor();
    }
    if (Check(TokenKind::KeywordForeach)) {
        return ParseForeach();
    }
    if (Check(TokenKind::KeywordSwitch)) {
        return ParseSwitch();
    }
    if (Check(TokenKind::KeywordBreak)) {
        return ParseBreak();
    }
    if (Check(TokenKind::KeywordContinue)) {
        return ParseContinue();
    }
    if (Check(TokenKind::KeywordCatch)) {
        return ParseCatch();
    }
    if (Check(TokenKind::KeywordReturn)) {
        return ParseReturn();
    }
    if (Check(TokenKind::LBrace)) {
        return ParseBlock();
    }

    if (Check(TokenKind::KeywordVar)) {
        return ParseVarDecl();
    }

    {
        const size_t saved = current_;
        std::vector<std::string> decorators = ParseDecorators();
        std::string decl_type;
        bool is_pointer = false;
        if (ParseOptionalType(&decl_type, &is_pointer) && Check(TokenKind::Identifier)) {
            const bool has_next = (current_ + 1) < tokens_.size();
            const TokenKind next_kind = has_next ? tokens_[current_ + 1].kind : TokenKind::EndOfFile;
            if (next_kind == TokenKind::Assign || next_kind == TokenKind::Semicolon) {
                return ParseTypedVarDeclWithPrefix(decorators, decl_type, is_pointer, true);
            }
            if (next_kind == TokenKind::LParen) {
                if (diag_) {
                    diag_->Add(DiagnosticLevel::Error, Peek().span, "function declaration is not allowed in statement context");
                }
                return ParseTypedFunctionDeclWithPrefix(decorators, decl_type, is_pointer);
            }
        }
        current_ = saved;
    }

    return ParseExprStmt();
}

std::unique_ptr<Stmt> Parser::ParseFor() {
    Expect(TokenKind::KeywordFor, "expect 'for'");
    if (!Expect(TokenKind::LParen, "expect '(' after for")) {
        return nullptr;
    }

    std::unique_ptr<ForStmt> st(new ForStmt());
    st->span = Previous().span;
    if (!Check(TokenKind::Semicolon)) {
        if (Check(TokenKind::KeywordVar)) {
            st->init = ParseVarDeclNoSemi();
        } else {
            const size_t saved = current_;
            std::vector<std::string> decorators = ParseDecorators();
            std::string decl_type;
            bool is_pointer = false;
            if (ParseOptionalType(&decl_type, &is_pointer) && Check(TokenKind::Identifier)) {
                const bool has_next = (current_ + 1) < tokens_.size();
                const TokenKind next_kind = has_next ? tokens_[current_ + 1].kind : TokenKind::EndOfFile;
                if (next_kind == TokenKind::Assign || next_kind == TokenKind::Semicolon) {
                    st->init = ParseTypedVarDeclWithPrefix(decorators, decl_type, is_pointer, false);
                }
            }
            if (!st->init) {
                current_ = saved;
                std::unique_ptr<ExprStmt> es(new ExprStmt());
                es->expr = ParseExpr();
                st->init = std::unique_ptr<Stmt>(es.release());
            }
        }
    }
    if (!Expect(TokenKind::Semicolon, "expect ';' after for init")) {
        return nullptr;
    }

    if (!Check(TokenKind::Semicolon)) {
        st->cond = ParseExpr();
    }
    if (!Expect(TokenKind::Semicolon, "expect ';' after for condition")) {
        return nullptr;
    }

    if (!Check(TokenKind::RParen)) {
        st->post = ParseExpr();
    }
    if (!Expect(TokenKind::RParen, "expect ')' after for clauses")) {
        return nullptr;
    }

    st->body = ParseStatement();
    return std::unique_ptr<Stmt>(st.release());
}

std::unique_ptr<Stmt> Parser::ParseForeach() {
    Expect(TokenKind::KeywordForeach, "expect 'foreach'");
    if (!Expect(TokenKind::LParen, "expect '(' after foreach")) {
        return nullptr;
    }

    std::unique_ptr<ForeachStmt> st(new ForeachStmt());
    st->span = Previous().span;
    if (!Expect(TokenKind::Identifier, "expect foreach variable")) {
        return nullptr;
    }
    st->first_name = Previous().lexeme;
    if (Match(TokenKind::Comma)) {
        if (!Expect(TokenKind::Identifier, "expect second foreach variable")) {
            return nullptr;
        }
        st->second_name = Previous().lexeme;
    }
    if (!Expect(TokenKind::KeywordIn, "expect 'in'")) {
        return nullptr;
    }

    st->container = ParseExpr();
    if (!Expect(TokenKind::RParen, "expect ')' after foreach")) {
        return nullptr;
    }

    st->body = ParseStatement();
    return std::unique_ptr<Stmt>(st.release());
}

std::unique_ptr<Stmt> Parser::ParseSwitch() {
    Expect(TokenKind::KeywordSwitch, "expect 'switch'");
    if (!Expect(TokenKind::LParen, "expect '(' after switch")) {
        return nullptr;
    }
    std::unique_ptr<Expr> selector = ParseExpr();
    if (!Expect(TokenKind::RParen, "expect ')' after switch selector")) {
        return nullptr;
    }
    if (!Expect(TokenKind::LBrace, "expect '{' for switch body")) {
        return nullptr;
    }

    std::unique_ptr<SwitchStmt> sw(new SwitchStmt());
    sw->span = Previous().span;
    sw->selector = std::move(selector);

    while (!Check(TokenKind::RBrace) && !IsAtEnd()) {
        if (!(Check(TokenKind::KeywordCase) || Check(TokenKind::KeywordDefault))) {
            if (diag_) {
                diag_->Add(DiagnosticLevel::Error, Peek().span, "expect 'case' or 'default' in switch");
            }
            Advance();
            continue;
        }

        std::unique_ptr<CaseStmt> cs(new CaseStmt());
        cs->span = Peek().span;
        if (Match(TokenKind::KeywordCase)) {
            cs->selector = ParseExpr();
            if (!Expect(TokenKind::Colon, "expect ':' after case expression")) {
                return nullptr;
            }
        } else {
            Expect(TokenKind::KeywordDefault, "expect 'default'");
            cs->is_default = true;
            if (!Expect(TokenKind::Colon, "expect ':' after default")) {
                return nullptr;
            }
        }

        while (!Check(TokenKind::RBrace) && !Check(TokenKind::KeywordCase) && !Check(TokenKind::KeywordDefault) && !IsAtEnd()) {
            const size_t before = current_;
            std::unique_ptr<Stmt> st = ParseDeclOrStmt();
            if (st) {
                cs->body.push_back(std::move(st));
                if (current_ == before) {
                    Advance();
                }
            } else {
                Advance();
            }
        }

        sw->cases.push_back(std::move(cs));
    }

    Expect(TokenKind::RBrace, "expect '}' after switch body");
    return std::unique_ptr<Stmt>(sw.release());
}

std::unique_ptr<Stmt> Parser::ParseBreak() {
    Expect(TokenKind::KeywordBreak, "expect 'break'");
    Expect(TokenKind::Semicolon, "expect ';' after break");
    std::unique_ptr<BreakStmt> st(new BreakStmt());
    st->span = Previous().span;
    return std::unique_ptr<Stmt>(st.release());
}

std::unique_ptr<Stmt> Parser::ParseContinue() {
    Expect(TokenKind::KeywordContinue, "expect 'continue'");
    Expect(TokenKind::Semicolon, "expect ';' after continue");
    std::unique_ptr<ContinueStmt> st(new ContinueStmt());
    st->span = Previous().span;
    return std::unique_ptr<Stmt>(st.release());
}

std::unique_ptr<Stmt> Parser::ParseCatch() {
    Expect(TokenKind::KeywordCatch, "expect 'catch'");
    std::unique_ptr<CatchStmt> st(new CatchStmt());
    st->span = Previous().span;
    st->body = ParseStatement();
    return std::unique_ptr<Stmt>(st.release());
}

std::unique_ptr<Stmt> Parser::ParseIf() {
    Expect(TokenKind::KeywordIf, "expect 'if'");
    if (!Expect(TokenKind::LParen, "expect '(' after if")) {
        return nullptr;
    }
    std::unique_ptr<Expr> cond = ParseExpr();
    if (!Expect(TokenKind::RParen, "expect ')' after condition")) {
        return nullptr;
    }

    std::unique_ptr<IfStmt> st(new IfStmt());
    st->span = Previous().span;
    st->cond = std::move(cond);
    st->then_branch = ParseStatement();
    if (Match(TokenKind::KeywordElse)) {
        st->else_branch = ParseStatement();
    }
    return std::unique_ptr<Stmt>(st.release());
}

std::unique_ptr<Stmt> Parser::ParseWhile() {
    Expect(TokenKind::KeywordWhile, "expect 'while'");
    if (!Expect(TokenKind::LParen, "expect '(' after while")) {
        return nullptr;
    }
    std::unique_ptr<Expr> cond = ParseExpr();
    if (!Expect(TokenKind::RParen, "expect ')' after condition")) {
        return nullptr;
    }

    std::unique_ptr<WhileStmt> st(new WhileStmt());
    st->span = Previous().span;
    st->cond = std::move(cond);
    st->body = ParseStatement();
    return std::unique_ptr<Stmt>(st.release());
}

std::unique_ptr<Stmt> Parser::ParseDoWhile() {
    Expect(TokenKind::KeywordDo, "expect 'do'");

    std::unique_ptr<DoWhileStmt> st(new DoWhileStmt());
    st->span = Previous().span;
    st->body = ParseStatement();
    if (!Expect(TokenKind::KeywordWhile, "expect 'while' after do-while body")) {
        return nullptr;
    }
    if (!Expect(TokenKind::LParen, "expect '(' after while")) {
        return nullptr;
    }
    st->cond = ParseExpr();
    if (!Expect(TokenKind::RParen, "expect ')' after condition")) {
        return nullptr;
    }
    Expect(TokenKind::Semicolon, "expect ';' after do-while");
    return std::unique_ptr<Stmt>(st.release());
}

std::unique_ptr<Stmt> Parser::ParseReturn() {
    Expect(TokenKind::KeywordReturn, "expect 'return'");
    std::unique_ptr<ReturnStmt> st(new ReturnStmt());
    st->span = Previous().span;
    if (!Check(TokenKind::Semicolon)) {
        st->value = ParseExpr();
    }
    Expect(TokenKind::Semicolon, "expect ';'");
    return std::unique_ptr<Stmt>(st.release());
}

std::unique_ptr<Stmt> Parser::ParseVarDecl() {
    Expect(TokenKind::KeywordVar, "expect 'var'");
    if (!Expect(TokenKind::Identifier, "expect variable name")) {
        return nullptr;
    }
    std::unique_ptr<VarDeclStmt> st(new VarDeclStmt());
    st->name = Previous().lexeme;
    st->declared_type = "mixed";
    st->span = Previous().span;
    if (Match(TokenKind::Assign)) {
        st->init = ParseExpr();
    }
    Expect(TokenKind::Semicolon, "expect ';'");
    return std::unique_ptr<Stmt>(st.release());
}

std::unique_ptr<Stmt> Parser::ParseVarDeclNoSemi() {
    Expect(TokenKind::KeywordVar, "expect 'var'");
    if (!Expect(TokenKind::Identifier, "expect variable name")) {
        return nullptr;
    }
    std::unique_ptr<VarDeclStmt> st(new VarDeclStmt());
    st->name = Previous().lexeme;
    st->declared_type = "mixed";
    st->span = Previous().span;
    if (Match(TokenKind::Assign)) {
        st->init = ParseExpr();
    }
    return std::unique_ptr<Stmt>(st.release());
}

std::unique_ptr<Stmt> Parser::ParseBlock() {
    if (!Expect(TokenKind::LBrace, "expect '{'")) {
        return nullptr;
    }
    std::unique_ptr<BlockStmt> block(new BlockStmt());
    block->span = Previous().span;
    while (!Check(TokenKind::RBrace) && !IsAtEnd()) {
        const size_t before = current_;
        std::unique_ptr<Stmt> st = ParseDeclOrStmt();
        if (st) {
            block->stmts.push_back(std::move(st));
            if (current_ == before) {
                Advance();
            }
        } else {
            Advance();
        }
    }
    Expect(TokenKind::RBrace, "expect '}'");
    return std::unique_ptr<Stmt>(block.release());
}

std::unique_ptr<Stmt> Parser::ParseExprStmt() {
    std::unique_ptr<ExprStmt> st(new ExprStmt());
    st->expr = ParseExpr();
    if (st->expr) {
        st->span = st->expr->span;
    }
    Expect(TokenKind::Semicolon, "expect ';'");
    return std::unique_ptr<Stmt>(st.release());
}

std::unique_ptr<Stmt> Parser::ParseTypedVarDeclWithPrefix(
    const std::vector<std::string> &decorators,
    const std::string &decl_type,
    bool is_pointer,
    bool need_semi) {
    if (!Expect(TokenKind::Identifier, "expect variable name")) {
        return nullptr;
    }
    std::unique_ptr<VarDeclStmt> st(new VarDeclStmt());
    st->name = Previous().lexeme;
    st->declared_type = is_pointer ? (decl_type + "*") : decl_type;
    st->is_pointer = is_pointer;
    st->decorators = decorators;
    st->span = Previous().span;
    if (Match(TokenKind::Assign)) {
        st->init = ParseExpr();
    }
    if (need_semi) {
        Expect(TokenKind::Semicolon, "expect ';'");
    }
    return std::unique_ptr<Stmt>(st.release());
}

std::unique_ptr<Stmt> Parser::ParseTypedFunctionDeclWithPrefix(
    const std::vector<std::string> &decorators,
    const std::string &ret_type,
    bool ret_is_pointer) {
    if (!Expect(TokenKind::Identifier, "expect function name")) {
        return nullptr;
    }

    std::unique_ptr<FunctionDecl> fn(new FunctionDecl());
    fn->name = Previous().lexeme;
    fn->return_type = ret_is_pointer ? (ret_type + "*") : ret_type;
    fn->return_is_pointer = ret_is_pointer;
    fn->decorators = decorators;
    fn->span = Previous().span;

    if (!Expect(TokenKind::LParen, "expect '('")) {
        return nullptr;
    }
    if (!Check(TokenKind::RParen)) {
        do {
            std::string param_type;
            bool param_ptr = false;
            ParseOptionalType(&param_type, &param_ptr);
            if (!Expect(TokenKind::Identifier, "expect parameter name")) {
                return nullptr;
            }
            fn->params.push_back(Previous().lexeme);
            fn->param_spans.push_back(Previous().span);
            fn->param_types.push_back(param_ptr ? (param_type + "*") : param_type);
            fn->param_is_pointer.push_back(param_ptr);
        } while (Match(TokenKind::Comma));
    }
    if (!Expect(TokenKind::RParen, "expect ')'")) {
        return nullptr;
    }

    std::unique_ptr<Stmt> body = ParseBlock();
    BlockStmt *block = body ? static_cast<BlockStmt *>(body.release()) : nullptr;
    if (!block) {
        return nullptr;
    }
    fn->body.swap(block->stmts);
    delete block;
    return std::unique_ptr<Stmt>(fn.release());
}

std::unique_ptr<Expr> Parser::ParseExpr() {
    return ParseAssignment();
}

std::unique_ptr<Expr> Parser::ParseAssignment() {
    std::unique_ptr<Expr> lhs = ParseTernary();
    if (!lhs) {
        return nullptr;
    }

    if (Match(TokenKind::Assign) || Match(TokenKind::PlusAssign) || Match(TokenKind::MinusAssign) || Match(TokenKind::StarAssign) || Match(TokenKind::SlashAssign) || Match(TokenKind::PercentAssign) || Match(TokenKind::AmpAssign) || Match(TokenKind::PipeAssign) || Match(TokenKind::CaretAssign) || Match(TokenKind::ShiftLeftAssign) || Match(TokenKind::ShiftRightAssign)) {
        Token op = Previous();
        std::unique_ptr<Expr> rhs = ParseAssignment();
        std::unique_ptr<BinaryExpr> bin(new BinaryExpr());
        bin->op = op.lexeme;
        bin->lhs = std::move(lhs);
        bin->rhs = std::move(rhs);
        bin->span = op.span;
        return std::unique_ptr<Expr>(bin.release());
    }
    return lhs;
}

std::unique_ptr<Expr> Parser::ParseTernary() {
    std::unique_ptr<Expr> expr = ParseLogicalOr();
    if (!expr) {
        return nullptr;
    }
    if (Match(TokenKind::Question)) {
        std::unique_ptr<TernaryExpr> tern(new TernaryExpr());
        tern->cond = std::move(expr);
        tern->then_expr = ParseTernary();
        if (!Expect(TokenKind::Colon, "expect ':' in ternary expression")) {
            return nullptr;
        }
        tern->else_expr = ParseTernary();
        tern->span = Previous().span;
        return std::unique_ptr<Expr>(tern.release());
    }
    return expr;
}

std::unique_ptr<Expr> Parser::ParseAdditive() {
    std::unique_ptr<Expr> expr = ParseMultiplicative();
    while (Match(TokenKind::Plus) || Match(TokenKind::Minus)) {
        Token op = Previous();
        std::unique_ptr<Expr> right = ParseMultiplicative();
        std::unique_ptr<BinaryExpr> bin(new BinaryExpr());
        bin->op = op.lexeme;
        bin->lhs = std::move(expr);
        bin->rhs = std::move(right);
        bin->span = op.span;
        expr = std::unique_ptr<Expr>(bin.release());
    }
    return expr;
}

std::unique_ptr<Expr> Parser::ParseLogicalOr() {
    std::unique_ptr<Expr> expr = ParseLogicalAnd();
    while (Match(TokenKind::LogicOr)) {
        Token op = Previous();
        std::unique_ptr<Expr> right = ParseLogicalAnd();
        std::unique_ptr<BinaryExpr> bin(new BinaryExpr());
        bin->op = op.lexeme;
        bin->lhs = std::move(expr);
        bin->rhs = std::move(right);
        bin->span = op.span;
        expr = std::unique_ptr<Expr>(bin.release());
    }
    return expr;
}

std::unique_ptr<Expr> Parser::ParseLogicalAnd() {
    std::unique_ptr<Expr> expr = ParseBitOr();
    while (Match(TokenKind::LogicAnd)) {
        Token op = Previous();
        std::unique_ptr<Expr> right = ParseBitOr();
        std::unique_ptr<BinaryExpr> bin(new BinaryExpr());
        bin->op = op.lexeme;
        bin->lhs = std::move(expr);
        bin->rhs = std::move(right);
        bin->span = op.span;
        expr = std::unique_ptr<Expr>(bin.release());
    }
    return expr;
}

std::unique_ptr<Expr> Parser::ParseBitOr() {
    std::unique_ptr<Expr> expr = ParseBitXor();
    while (Match(TokenKind::Pipe)) {
        Token op = Previous();
        std::unique_ptr<Expr> right = ParseBitXor();
        std::unique_ptr<BinaryExpr> bin(new BinaryExpr());
        bin->op = op.lexeme;
        bin->lhs = std::move(expr);
        bin->rhs = std::move(right);
        bin->span = op.span;
        expr = std::unique_ptr<Expr>(bin.release());
    }
    return expr;
}

std::unique_ptr<Expr> Parser::ParseBitXor() {
    std::unique_ptr<Expr> expr = ParseBitAnd();
    while (Match(TokenKind::Caret)) {
        Token op = Previous();
        std::unique_ptr<Expr> right = ParseBitAnd();
        std::unique_ptr<BinaryExpr> bin(new BinaryExpr());
        bin->op = op.lexeme;
        bin->lhs = std::move(expr);
        bin->rhs = std::move(right);
        bin->span = op.span;
        expr = std::unique_ptr<Expr>(bin.release());
    }
    return expr;
}

std::unique_ptr<Expr> Parser::ParseBitAnd() {
    std::unique_ptr<Expr> expr = ParseEquality();
    while (Match(TokenKind::Amp)) {
        Token op = Previous();
        std::unique_ptr<Expr> right = ParseEquality();
        std::unique_ptr<BinaryExpr> bin(new BinaryExpr());
        bin->op = op.lexeme;
        bin->lhs = std::move(expr);
        bin->rhs = std::move(right);
        bin->span = op.span;
        expr = std::unique_ptr<Expr>(bin.release());
    }
    return expr;
}

std::unique_ptr<Expr> Parser::ParseEquality() {
    std::unique_ptr<Expr> expr = ParseRelational();
    while (Match(TokenKind::EqEq) || Match(TokenKind::NotEq)) {
        Token op = Previous();
        std::unique_ptr<Expr> right = ParseRelational();
        std::unique_ptr<BinaryExpr> bin(new BinaryExpr());
        bin->op = op.lexeme;
        bin->lhs = std::move(expr);
        bin->rhs = std::move(right);
        bin->span = op.span;
        expr = std::unique_ptr<Expr>(bin.release());
    }
    return expr;
}

std::unique_ptr<Expr> Parser::ParseRelational() {
    std::unique_ptr<Expr> expr = ParseShift();
    while (Match(TokenKind::Lt) || Match(TokenKind::Lte) || Match(TokenKind::Gt) || Match(TokenKind::Gte)) {
        Token op = Previous();
        std::unique_ptr<Expr> right = ParseShift();
        std::unique_ptr<BinaryExpr> bin(new BinaryExpr());
        bin->op = op.lexeme;
        bin->lhs = std::move(expr);
        bin->rhs = std::move(right);
        bin->span = op.span;
        expr = std::unique_ptr<Expr>(bin.release());
    }
    return expr;
}

std::unique_ptr<Expr> Parser::ParseShift() {
    std::unique_ptr<Expr> expr = ParseAdditive();
    while (Match(TokenKind::ShiftLeft) || Match(TokenKind::ShiftRight)) {
        Token op = Previous();
        std::unique_ptr<Expr> right = ParseAdditive();
        std::unique_ptr<BinaryExpr> bin(new BinaryExpr());
        bin->op = op.lexeme;
        bin->lhs = std::move(expr);
        bin->rhs = std::move(right);
        expr = std::unique_ptr<Expr>(bin.release());
    }
    return expr;
}

std::unique_ptr<Expr> Parser::ParseMultiplicative() {
    std::unique_ptr<Expr> expr = ParseUnary();
    while (Match(TokenKind::Star) || Match(TokenKind::Slash) || Match(TokenKind::Percent)) {
        Token op = Previous();
        std::unique_ptr<Expr> right = ParseUnary();
        std::unique_ptr<BinaryExpr> bin(new BinaryExpr());
        bin->op = op.lexeme;
        bin->lhs = std::move(expr);
        bin->rhs = std::move(right);
        expr = std::unique_ptr<Expr>(bin.release());
    }
    return expr;
}

std::unique_ptr<Expr> Parser::ParseUnary() {
    if (Match(TokenKind::Bang) || Match(TokenKind::Minus) || Match(TokenKind::Tilde) ||
        Match(TokenKind::PlusPlus) || Match(TokenKind::MinusMinus)) {
        Token op = Previous();
        std::unique_ptr<UnaryExpr> un(new UnaryExpr());
        un->op = op.lexeme;
        un->operand = ParseUnary();
        un->span = op.span;
        return std::unique_ptr<Expr>(un.release());
    }
    return ParsePostfix();
}

std::unique_ptr<Expr> Parser::ParsePostfix() {
    std::unique_ptr<Expr> expr = ParsePrimary();
    while (true) {
        if (Match(TokenKind::LParen)) {
            std::unique_ptr<CallExpr> call(new CallExpr());
            call->span = expr ? expr->span : Previous().span;
            call->callee = std::move(expr);
            if (!Check(TokenKind::RParen)) {
                do {
                    call->args.push_back(ParseExpr());
                } while (Match(TokenKind::Comma));
            }
            Expect(TokenKind::RParen, "expect ')' after args");
            expr = std::unique_ptr<Expr>(call.release());
            continue;
        }

        if (Match(TokenKind::Arrow)) {
            if (!Expect(TokenKind::Identifier, "expect member identifier after '->'")) {
                return expr;
            }
            std::unique_ptr<MemberExpr> m(new MemberExpr());
            m->object = std::move(expr);
            m->member = Previous().lexeme;
            m->span = Previous().span;
            expr = std::unique_ptr<Expr>(m.release());
            continue;
        }

        if (Match(TokenKind::LBracket)) {
            std::unique_ptr<IndexExpr> idx(new IndexExpr());
            idx->object = std::move(expr);
            idx->index = ParseExpr();
            if (Match(TokenKind::DotDot)) {
                idx->is_range = true;
                idx->index_end = ParseExpr();
            }
            Expect(TokenKind::RBracket, "expect ']' after index");
            idx->span = Previous().span;
            expr = std::unique_ptr<Expr>(idx.release());
            continue;
        }

        if (Match(TokenKind::PlusPlus) || Match(TokenKind::MinusMinus)) {
            Token op = Previous();
            std::unique_ptr<UnaryExpr> un(new UnaryExpr());
            un->op = op.lexeme;
            un->is_postfix = true;
            un->operand = std::move(expr);
            un->span = op.span;
            expr = std::unique_ptr<Expr>(un.release());
            continue;
        }

        break;
    }
    return expr;
}

std::unique_ptr<Expr> Parser::ParsePrimary() {
    if (Check(TokenKind::LArrInit)) {
        return ParseArrayLiteralExpr();
    }
    if (Check(TokenKind::LMapInit)) {
        return ParseMappingLiteralExpr();
    }

    if (Match(TokenKind::Number)) {
        std::unique_ptr<NumberExpr> n(new NumberExpr());
        n->literal = Previous().lexeme;
        n->span = Previous().span;
        return std::unique_ptr<Expr>(n.release());
    }
    if (Match(TokenKind::Float)) {
        std::unique_ptr<FloatExpr> f(new FloatExpr());
        f->literal = Previous().lexeme;
        f->span = Previous().span;
        return std::unique_ptr<Expr>(f.release());
    }
    if (Match(TokenKind::String)) {
        std::unique_ptr<StringExpr> s(new StringExpr());
        s->literal = Previous().lexeme;
        s->span = Previous().span;
        return std::unique_ptr<Expr>(s.release());
    }
    if (Match(TokenKind::Identifier)) {
        std::unique_ptr<IdentifierExpr> id(new IdentifierExpr());
        id->name = Previous().lexeme;
        id->span = Previous().span;
        return std::unique_ptr<Expr>(id.release());
    }
    if (Match(TokenKind::KeywordTrue)) {
        std::unique_ptr<NumberExpr> n(new NumberExpr());
        n->literal = "1";
        n->span = Previous().span;
        return std::unique_ptr<Expr>(n.release());
    }
    if (Match(TokenKind::KeywordFalse)) {
        std::unique_ptr<NumberExpr> n(new NumberExpr());
        n->literal = "0";
        n->span = Previous().span;
        return std::unique_ptr<Expr>(n.release());
    }
    if (Match(TokenKind::KeywordNew)) {
        if (!Expect(TokenKind::Identifier, "expect class name after new")) {
            return nullptr;
        }
        std::unique_ptr<NewExpr> n(new NewExpr());
        n->class_name = Previous().lexeme;
        n->span = Previous().span;
        if (Match(TokenKind::LParen)) {
            while (!Check(TokenKind::RParen) && !IsAtEnd()) {
                ParseExpr();
                if (!Match(TokenKind::Comma)) {
                    break;
                }
            }
            Expect(TokenKind::RParen, "expect ')' after new args");
        }
        return std::unique_ptr<Expr>(n.release());
    }
    if (Check(TokenKind::KeywordFun)) {
        return ParseLambdaExpr();
    }
    if (Match(TokenKind::LParen)) {
        std::unique_ptr<Expr> inner = ParseExpr();
        Expect(TokenKind::RParen, "expect ')' after expression");
        return inner;
    }

    if (diag_) {
        diag_->Add(DiagnosticLevel::Error, Peek().span, "Unexpected token in expression");
    }
    return nullptr;
}

std::unique_ptr<Expr> Parser::ParseArrayLiteralExpr() {
    if (!Expect(TokenKind::LArrInit, "expect '({' for array literal")) {
        return nullptr;
    }

    std::unique_ptr<NewExpr> n(new NewExpr());
    n->is_array_literal = true;
    n->class_name = "array";
    n->span = Previous().span;

    if (!Check(TokenKind::RArrInit)) {
        do {
            n->array_items.push_back(ParseExpr());
        } while (Match(TokenKind::Comma));
    }
    Expect(TokenKind::RArrInit, "expect ')}' after array literal");
    return std::unique_ptr<Expr>(n.release());
}

std::unique_ptr<Expr> Parser::ParseMappingLiteralExpr() {
    if (!Expect(TokenKind::LMapInit, "expect '([' for mapping literal")) {
        return nullptr;
    }

    std::unique_ptr<MappingLiteralExpr> m(new MappingLiteralExpr());
    m->span = Previous().span;

    if (!Check(TokenKind::RBracket)) {
        do {
            std::unique_ptr<Expr> key = ParseExpr();
            if (!Expect(TokenKind::Colon, "expect ':' after mapping key")) {
                return nullptr;
            }
            std::unique_ptr<Expr> val = ParseExpr();
            m->pairs.push_back({std::move(key), std::move(val)});
        } while (Match(TokenKind::Comma));
    }
    Expect(TokenKind::RBracket, "expect ']' after mapping literal");
    Expect(TokenKind::RParen, "expect ')' after mapping literal");
    return std::unique_ptr<Expr>(m.release());
}

std::unique_ptr<Expr> Parser::ParseLambdaExpr() {
    if (!Check(TokenKind::KeywordFun)) {
        if (diag_) {
            diag_->Add(DiagnosticLevel::Error, Peek().span, "expect 'fun' for lambda");
        }
        return nullptr;
    }
    Advance();

    if (!Expect(TokenKind::LParen, "expect '('")) {
        return nullptr;
    }

    std::unique_ptr<LambdaExpr> lam(new LambdaExpr());
    lam->span = Previous().span;
    if (!Check(TokenKind::RParen)) {
        do {
            std::string param_type;
            bool param_ptr = false;
            ParseOptionalType(&param_type, &param_ptr);
            if (!Expect(TokenKind::Identifier, "expect parameter name")) {
                return nullptr;
            }
            lam->params.push_back(Previous().lexeme);
        } while (Match(TokenKind::Comma));
    }
    if (!Expect(TokenKind::RParen, "expect ')'")) {
        return nullptr;
    }

    if (Match(TokenKind::Arrow)) {
        std::unique_ptr<ReturnStmt> ret(new ReturnStmt());
        ret->value = ParseExpr();
        if (!ret->value) {
            return nullptr;
        }
        lam->body.push_back(std::move(ret));
    } else {
        std::unique_ptr<Stmt> body = ParseBlock();
        BlockStmt *block = body ? static_cast<BlockStmt *>(body.release()) : nullptr;
        if (!block) {
            return nullptr;
        }
        lam->body.swap(block->stmts);
        delete block;
    }

    return std::unique_ptr<Expr>(lam.release());
}

bool Parser::Match(TokenKind kind) {
    if (!Check(kind)) {
        return false;
    }
    Advance();
    return true;
}

bool Parser::Check(TokenKind kind) const {
    if (IsAtEnd()) {
        return kind == TokenKind::EndOfFile;
    }
    return Peek().kind == kind;
}

const Token &Parser::Advance() {
    if (!IsAtEnd()) {
        ++current_;
    }
    return Previous();
}

const Token &Parser::Peek() const {
    return tokens_[current_];
}

const Token &Parser::Previous() const {
    return tokens_[current_ - 1];
}

bool Parser::IsAtEnd() const {
    return current_ >= tokens_.size() || tokens_[current_].kind == TokenKind::EndOfFile;
}

bool Parser::Expect(TokenKind kind, const char *message) {
    if (Check(kind)) {
        Advance();
        return true;
    }
    if (diag_) {
        diag_->Add(DiagnosticLevel::Error, Peek().span, message);
    }
    return false;
}

bool Parser::IsDecoratorToken(TokenKind kind) const {
    return kind == TokenKind::KeywordStatic ||
        kind == TokenKind::KeywordPrivate ||
        kind == TokenKind::KeywordPublic ||
        kind == TokenKind::KeywordNomask;
}

bool Parser::IsTypeStartToken(TokenKind kind) const {
    return kind == TokenKind::KeywordVoid ||
        kind == TokenKind::KeywordInt ||
        kind == TokenKind::KeywordFloat ||
        kind == TokenKind::KeywordStringType ||
        kind == TokenKind::KeywordObject ||
        kind == TokenKind::KeywordMapping ||
        kind == TokenKind::KeywordMixed ||
        kind == TokenKind::Identifier;
}

std::string Parser::TypeNameFromToken(const Token &tok) const {
    switch (tok.kind) {
    case TokenKind::KeywordStringType:
        return "string";
    default:
        return tok.lexeme;
    }
}

bool Parser::ParseOptionalType(std::string *decl_type, bool *is_pointer) {
    if (!decl_type || !is_pointer) {
        return false;
    }
    if (IsAtEnd()) {
        return false;
    }
    if (!IsTypeStartToken(Peek().kind)) {
        return false;
    }
    if (Peek().kind == TokenKind::Identifier) {
        const bool has_next = (current_ + 1) < tokens_.size();
        const TokenKind next = has_next ? tokens_[current_ + 1].kind : TokenKind::EndOfFile;
        if (next != TokenKind::Identifier && next != TokenKind::Star) {
            return false;
        }
    }
    Advance();
    *decl_type = TypeNameFromToken(Previous());
    *is_pointer = Match(TokenKind::Star);
    return true;
}

std::vector<std::string> Parser::ParseDecorators() {
    std::vector<std::string> decorators;
    bool has_public = false;
    bool has_private = false;
    while (!IsAtEnd() && IsDecoratorToken(Peek().kind)) {
        Advance();
        const std::string key = Previous().lexeme;
        decorators.push_back(key);
        if (key == "public") {
            has_public = true;
        }
        if (key == "private") {
            has_private = true;
        }
    }
    if (has_public && has_private && diag_) {
        diag_->Add(DiagnosticLevel::Warning, Previous().span, "conflicting decorateKey: both public and private are present");
    }
    return decorators;
}

} // namespace frontend
} // namespace lpc
