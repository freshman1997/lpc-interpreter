#include "frontend/pipeline.h"

#include "frontend/lexer.h"
#include "frontend/parser.h"
#include "frontend/preprocessor.h"
#include "frontend/sema.h"
#include "frontend/verifier.h"
#include "frontend/mir_opt.h"

#include <unordered_map>
#include <vector>

namespace lpc {
namespace frontend {

static std::string ProcessStringEscapes(const std::string &raw) {
    std::string result;
    result.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\\' && i + 1 < raw.size()) {
            char next = raw[i + 1];
            switch (next) {
            case 'n': result += '\n'; ++i; break;
            case 't': result += '\t'; ++i; break;
            case 'r': result += '\r'; ++i; break;
            case '\\': result += '\\'; ++i; break;
            case '"': result += '"'; ++i; break;
            case '\'': result += '\''; ++i; break;
            case '0': result += '\0'; ++i; break;
            case 'a': result += '\a'; ++i; break;
            case 'b': result += '\b'; ++i; break;
            case 'f': result += '\f'; ++i; break;
            case 'v': result += '\v'; ++i; break;
            default: result += raw[i]; break;
            }
        } else {
            result += raw[i];
        }
    }
    return result;
}

static bool IsVoidEfun(int efun_index) {
    switch (efun_index) {
    case 1: case 2: case 3: case 13: case 15: case 31:
        return true;
    default:
        return false;
    }
}

class MirBuilder {
public:
    explicit MirBuilder(const SemanticModel &sema) : sema_(sema) {}

    MirModule Build(const Module &module) {
        MirModule out;
        out.class_order = sema_.class_order;
        out.class_fields = sema_.class_fields;
        out.class_field_defaults = sema_.class_field_defaults;
        out.class_parent = sema_.class_parent;

        function_index_by_name_.clear();
        lambda_index_by_expr_.clear();
        globals_.clear();
        int fidx = 0;
        for (const auto &decl : module.decls) {
            if (decl->kind == NodeKind::FunctionDecl) {
                const FunctionDecl *fn = static_cast<const FunctionDecl *>(decl.get());
                function_index_by_name_[fn->name] = fidx++;
            }
        }

        for (int i = 0; i < static_cast<int>(sema_.global_variables.size()); ++i) {
            globals_[sema_.global_variables[i]] = i;
        }
        out.global_variables = sema_.global_variables;

        CollectLambdas(module);

        for (const auto &decl : module.decls) {
            if (decl->kind == NodeKind::FunctionDecl) {
                const FunctionDecl *fn = static_cast<const FunctionDecl *>(decl.get());
                out.functions.push_back(BuildFunction(*fn));
            }
        }

        for (const auto &pair : lambdas_in_order_) {
            out.functions.push_back(BuildLambda(*pair));
        }

        out.init_function = BuildInitFunction(module);

        return out;
    }

private:
    struct LoopContext {
        std::vector<int> break_jumps;
        std::vector<int> continue_jumps;
        int continue_target = -1;
        bool is_switch = false;
        int foreach_depth = 0;
    };

    MirFunction BuildFunction(const FunctionDecl &fn) {
        MirFunction f;
        f.name = fn.name;
        f.nargs = static_cast<int>(fn.params.size());
        current_function_name_ = fn.name;
        current_nargs_ = f.nargs;
        current_params_ = fn.params;

        auto fit = sema_.functions.find(&fn);
        if (fit != sema_.functions.end()) {
            f.locals = fit->second.locals;
            f.upvalues = fit->second.captures;
            f.upvalue_source_kind = fit->second.capture_source_kind;
            f.upvalue_source_index = fit->second.capture_source_index;
        }

        locals_.clear();
        for (int i = 0; i < static_cast<int>(fn.params.size()); ++i) {
            locals_[fn.params[i]] = i;
        }
        int base = static_cast<int>(locals_.size());
        for (int i = 0; i < static_cast<int>(f.locals.size()); ++i) {
            locals_[f.locals[i]] = base + i;
        }

        upvalues_.clear();
        for (int i = 0; i < static_cast<int>(f.upvalues.size()); ++i) {
            upvalues_[f.upvalues[i]] = i;
        }

        for (const auto &st : fn.body) {
            EmitStmt(st.get(), f);
        }
        f.code.push_back({MirOp::Return, 0, 0});
        current_function_name_.clear();
        current_nargs_ = 0;
        current_params_.clear();
        return f;
    }

    bool TryEmitSelfTailReturn(const ReturnStmt *r, MirFunction &f) {
        if (!r || !r->value || current_function_name_.empty() || current_nargs_ <= 0) {
            return false;
        }
        if (r->value->kind != NodeKind::CallExpr) {
            return false;
        }

        const CallExpr *call = static_cast<const CallExpr *>(r->value.get());
        if (!call->callee || call->callee->kind != NodeKind::Identifier) {
            return false;
        }

        const IdentifierExpr *id = static_cast<const IdentifierExpr *>(call->callee.get());
        if (id->name != current_function_name_ || static_cast<int>(call->args.size()) != current_nargs_) {
            return false;
        }

        for (const auto &arg : call->args) {
            EmitExpr(arg.get(), f);
        }

        for (int i = current_nargs_ - 1; i >= 0; --i) {
            auto it = locals_.find(current_params_[i]);
            if (it == locals_.end()) {
                return false;
            }
            f.code.push_back({MirOp::StoreLocal, it->second, 0});
        }
        f.code.push_back({MirOp::Jump, 0, 0});
        return true;
    }

    void EmitStmt(const Stmt *stmt, MirFunction &f) {
        if (!stmt) {
            return;
        }

        const int begin = static_cast<int>(f.code.size());
        const int line = stmt->span.line;

        switch (stmt->kind) {
        case NodeKind::VarDecl: {
            const VarDeclStmt *vd = static_cast<const VarDeclStmt *>(stmt);
            if (vd->init) {
                EmitExpr(vd->init.get(), f);
            } else {
                f.code.push_back({MirOp::LoadConst, 0, 0});
            }
            if (locals_.count(vd->name)) {
                f.code.push_back({MirOp::StoreLocal, locals_[vd->name], 0});
            } else if (globals_.count(vd->name)) {
                f.code.push_back({MirOp::StoreGlobal, globals_[vd->name], 0});
            }
            break;
        }
        case NodeKind::ExprStmt: {
            const ExprStmt *es = static_cast<const ExprStmt *>(stmt);
            EmitExpr(es->expr.get(), f);
            f.code.push_back({MirOp::Pop, 0, 0});
            break;
        }
        case NodeKind::ReturnStmt: {
            const ReturnStmt *r = static_cast<const ReturnStmt *>(stmt);
            if (TryEmitSelfTailReturn(r, f)) {
                break;
            }
            if (r->value) {
                EmitExpr(r->value.get(), f);
            } else {
                f.code.push_back({MirOp::LoadConst, 0, 0});
            }
            f.code.push_back({MirOp::Return, 0, 0});
            break;
        }
        case NodeKind::Block: {
            const BlockStmt *b = static_cast<const BlockStmt *>(stmt);
            for (const auto &it : b->stmts) {
                EmitStmt(it.get(), f);
            }
            break;
        }
        case NodeKind::IfStmt: {
            const IfStmt *ifs = static_cast<const IfStmt *>(stmt);
            EmitExpr(ifs->cond.get(), f);
            const int jfalse_pos = static_cast<int>(f.code.size());
            f.code.push_back({MirOp::JumpIfFalse, -1, 0});
            EmitStmt(ifs->then_branch.get(), f);
            const int jmp_end_pos = static_cast<int>(f.code.size());
            f.code.push_back({MirOp::Jump, -1, 0});
            f.code[jfalse_pos].a = static_cast<int>(f.code.size());
            EmitStmt(ifs->else_branch.get(), f);
            f.code[jmp_end_pos].a = static_cast<int>(f.code.size());
            break;
        }
        case NodeKind::WhileStmt: {
            const WhileStmt *w = static_cast<const WhileStmt *>(stmt);
            const int loop_head = static_cast<int>(f.code.size());
            loop_stack_.push_back(LoopContext());
            loop_stack_.back().continue_target = loop_head;
            EmitExpr(w->cond.get(), f);
            const int jfalse_pos = static_cast<int>(f.code.size());
            f.code.push_back({MirOp::JumpIfFalse, -1, 0});
            EmitStmt(w->body.get(), f);
            f.code.push_back({MirOp::Jump, loop_head, 0});
            int end = static_cast<int>(f.code.size());
            f.code[jfalse_pos].a = end;
            for (int j : loop_stack_.back().break_jumps) {
                f.code[j].a = end;
            }
            for (int j : loop_stack_.back().continue_jumps) {
                f.code[j].a = loop_head;
            }
            loop_stack_.pop_back();
            break;
        }
        case NodeKind::ForStmt: {
            const ForStmt *fs = static_cast<const ForStmt *>(stmt);
            EmitStmt(fs->init.get(), f);
            const int loop_head = static_cast<int>(f.code.size());
            loop_stack_.push_back(LoopContext());
            if (fs->cond) {
                EmitExpr(fs->cond.get(), f);
                const int jfalse_pos = static_cast<int>(f.code.size());
                f.code.push_back({MirOp::JumpIfFalse, -1, 0});
                EmitStmt(fs->body.get(), f);
                loop_stack_.back().continue_target = static_cast<int>(f.code.size());
                if (fs->post) {
                    EmitExpr(fs->post.get(), f);
                    f.code.push_back({MirOp::Pop, 0, 0});
                }
                f.code.push_back({MirOp::Jump, loop_head, 0});
                int end = static_cast<int>(f.code.size());
                f.code[jfalse_pos].a = end;
                for (int j : loop_stack_.back().break_jumps) {
                    f.code[j].a = end;
                }
                for (int j : loop_stack_.back().continue_jumps) {
                    f.code[j].a = loop_stack_.back().continue_target;
                }
            } else {
                EmitStmt(fs->body.get(), f);
                loop_stack_.back().continue_target = static_cast<int>(f.code.size());
                if (fs->post) {
                    EmitExpr(fs->post.get(), f);
                    f.code.push_back({MirOp::Pop, 0, 0});
                }
                f.code.push_back({MirOp::Jump, loop_head, 0});
                int end = static_cast<int>(f.code.size());
                for (int j : loop_stack_.back().break_jumps) {
                    f.code[j].a = end;
                }
                for (int j : loop_stack_.back().continue_jumps) {
                    f.code[j].a = loop_stack_.back().continue_target;
                }
            }
            loop_stack_.pop_back();
            break;
        }
        case NodeKind::ForeachStmt: {
            const ForeachStmt *fe = static_cast<const ForeachStmt *>(stmt);
            EmitExpr(fe->container.get(), f);
            f.code.push_back({MirOp::ForeachInit, 0, 0});

            const int loop_head = static_cast<int>(f.code.size());
            loop_stack_.push_back(LoopContext());
            loop_stack_.back().continue_target = loop_head;
            loop_stack_.back().foreach_depth = 2;

            int pair_cnt = fe->second_name.empty() ? 1 : 2;
            f.code.push_back({MirOp::ForeachNext, pair_cnt, -1});

            if (!fe->first_name.empty() && locals_.count(fe->first_name)) {
                f.code.push_back({MirOp::StoreLocal, locals_[fe->first_name], 0});
            }
            if (!fe->second_name.empty() && locals_.count(fe->second_name)) {
                f.code.push_back({MirOp::StoreLocal, locals_[fe->second_name], 0});
            }

            EmitStmt(fe->body.get(), f);
            f.code.push_back({MirOp::Jump, loop_head, 0});

            int end = static_cast<int>(f.code.size());
            f.code[loop_head].b = end;
            for (int j : loop_stack_.back().break_jumps) {
                f.code[j].a = end;
            }
            for (int j : loop_stack_.back().continue_jumps) {
                f.code[j].a = loop_head;
            }
            loop_stack_.pop_back();
            break;
        }
        case NodeKind::DoWhileStmt: {
            const DoWhileStmt *dw = static_cast<const DoWhileStmt *>(stmt);
            const int loop_head = static_cast<int>(f.code.size());
            loop_stack_.push_back(LoopContext());
            loop_stack_.back().continue_target = loop_head;
            EmitStmt(dw->body.get(), f);
            loop_stack_.back().continue_target = static_cast<int>(f.code.size());
            EmitExpr(dw->cond.get(), f);
            const int jfalse_pos = static_cast<int>(f.code.size());
            f.code.push_back({MirOp::JumpIfFalse, -1, 0});
            f.code.push_back({MirOp::Jump, loop_head, 0});
            int end = static_cast<int>(f.code.size());
            f.code[jfalse_pos].a = end;
            for (int j : loop_stack_.back().break_jumps) {
                f.code[j].a = end;
            }
            for (int j : loop_stack_.back().continue_jumps) {
                f.code[j].a = loop_stack_.back().continue_target;
            }
            loop_stack_.pop_back();
            break;
        }
        case NodeKind::SwitchStmt: {
            const SwitchStmt *sw = static_cast<const SwitchStmt *>(stmt);
            EmitExpr(sw->selector.get(), f);
            int switch_temp = f.nargs + static_cast<int>(f.locals.size());
            std::string temp_name = "__switch_tmp_" + std::to_string(temp_counter_++);
            f.locals.push_back(temp_name);
            locals_[temp_name] = switch_temp;
            f.code.push_back({MirOp::StoreLocal, switch_temp, 0});

            loop_stack_.push_back(LoopContext());
            loop_stack_.back().is_switch = true;

            for (const auto &cs : sw->cases) {
                if (!cs->is_default) {
                    f.code.push_back({MirOp::LoadLocal, switch_temp, 0});
                    EmitExpr(cs->selector.get(), f);
                    f.code.push_back({MirOp::Eq, 0, 0});
                    const int not_case = static_cast<int>(f.code.size());
                    f.code.push_back({MirOp::JumpIfFalse, -1, 0});
                    for (const auto &st : cs->body) {
                        EmitStmt(st.get(), f);
                    }
                    int ej = static_cast<int>(f.code.size());
                    f.code.push_back({MirOp::Jump, -1, 0});
                    loop_stack_.back().break_jumps.push_back(ej);
                    f.code[not_case].a = static_cast<int>(f.code.size());
                } else {
                    for (const auto &st : cs->body) {
                        EmitStmt(st.get(), f);
                    }
                    int ej = static_cast<int>(f.code.size());
                    f.code.push_back({MirOp::Jump, -1, 0});
                    loop_stack_.back().break_jumps.push_back(ej);
                }
            }

            const int end_target = static_cast<int>(f.code.size());
            for (int j : loop_stack_.back().break_jumps) {
                f.code[j].a = end_target;
            }
            loop_stack_.pop_back();
            break;
        }
        case NodeKind::BreakStmt: {
            if (!loop_stack_.empty()) {
                int fd = loop_stack_.back().foreach_depth;
                for (int k = 0; k < fd; ++k) {
                    f.code.push_back({MirOp::Pop, 0, 0});
                }
                int j = static_cast<int>(f.code.size());
                f.code.push_back({MirOp::Jump, -1, 0});
                loop_stack_.back().break_jumps.push_back(j);
            }
            break;
        }
        case NodeKind::ClassDecl: {
            break;
        }
        case NodeKind::CatchStmt: {
            const CatchStmt *cs = static_cast<const CatchStmt *>(stmt);
            const int catch_pos = static_cast<int>(f.code.size());
            f.code.push_back({MirOp::Catch, -1, 0});
            EmitStmt(cs->body.get(), f);
            f.code[catch_pos].a = static_cast<int>(f.code.size());
            break;
        }
        case NodeKind::ContinueStmt: {
            if (!loop_stack_.empty() && !loop_stack_.back().is_switch) {
                int j = static_cast<int>(f.code.size());
                f.code.push_back({MirOp::Jump, -1, 0});
                loop_stack_.back().continue_jumps.push_back(j);
            }
            break;
        }
        default:
            break;
        }

        const int end = static_cast<int>(f.code.size());
        if (line > 0) {
            for (int i = begin; i < end; ++i) {
                if (f.code[i].line <= 0) {
                    f.code[i].line = line;
                }
            }
        }
    }

    void EmitExpr(const Expr *expr, MirFunction &f) {
        if (!expr) {
            return;
        }

        const int begin = static_cast<int>(f.code.size());
        const int line = expr->span.line;

        switch (expr->kind) {
        case NodeKind::Identifier: {
            const IdentifierExpr *id = static_cast<const IdentifierExpr *>(expr);
            if (locals_.count(id->name)) {
                f.code.push_back({MirOp::LoadLocal, locals_[id->name], 0});
            } else if (upvalues_.count(id->name)) {
                f.code.push_back({MirOp::LoadUpvalue, upvalues_[id->name], 0});
            } else if (globals_.count(id->name)) {
                f.code.push_back({MirOp::LoadGlobal, globals_[id->name], 0});
            } else {
                f.code.push_back({MirOp::LoadConst, 0, 0});
            }
            break;
        }
        case NodeKind::Number:
        case NodeKind::Float:
        case NodeKind::String: {
            if (expr->kind == NodeKind::Float) {
                const FloatExpr *fe = static_cast<const FloatExpr *>(expr);
                double v = 0.0;
                try {
                    v = std::stod(fe->literal);
                } catch (...) {
                    v = 0.0;
                }
                int idx = static_cast<int>(f.fconsts.size());
                f.fconsts.push_back(v);
                f.code.push_back({MirOp::LoadFConst, idx, 0});
            } else if (expr->kind == NodeKind::Number) {
                const NumberExpr *n = static_cast<const NumberExpr *>(expr);
                std::int64_t v = 0;
                try {
                    v = std::stoll(n->literal, nullptr, 0);
                } catch (...) {
                    v = 0;
                }
                int idx = static_cast<int>(f.iconsts.size());
                f.iconsts.push_back(v);
                f.code.push_back({MirOp::LoadConst, idx, 0});
            } else {
                const StringExpr *s = static_cast<const StringExpr *>(expr);
                std::string val = s->literal;
                if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
                    val = val.substr(1, val.size() - 2);
                }
                val = ProcessStringEscapes(val);
                int idx = static_cast<int>(f.sconsts.size());
                f.sconsts.push_back(val);
                f.code.push_back({MirOp::LoadSConst, idx, 0});
            }
            break;
        }
        case NodeKind::CallExpr: {
            const CallExpr *c = static_cast<const CallExpr *>(expr);
            if (c->callee && c->callee->kind == NodeKind::MemberExpr) {
                const MemberExpr *m = static_cast<const MemberExpr *>(c->callee.get());
                EmitExpr(m->object.get(), f);
                int midx = static_cast<int>(f.sconsts.size());
                f.sconsts.push_back(m->member);
                f.code.push_back({MirOp::LoadSConst, midx, 0});
                for (const auto &it : c->args) {
                    EmitExpr(it.get(), f);
                }
                f.code.push_back({MirOp::CallEfun, Sema::EfunIndex("call_other"), static_cast<int>(c->args.size()) + 2});
                break;
            }
            for (const auto &it : c->args) {
                EmitExpr(it.get(), f);
            }
            if (c->callee && c->callee->kind == NodeKind::Identifier) {
                const IdentifierExpr *id = static_cast<const IdentifierExpr *>(c->callee.get());
                int eidx = Sema::EfunIndex(id->name);
                if (eidx >= 0) {
                    f.code.push_back({MirOp::CallEfun, eidx, static_cast<int>(c->args.size())});
                    if (IsVoidEfun(eidx)) {
                        int zero_idx = static_cast<int>(f.iconsts.size());
                        f.iconsts.push_back(0);
                        f.code.push_back({MirOp::LoadConst, zero_idx, 0});
                    }
                    break;
                }
                int fidx = find_function_index(id->name);
                if (fidx >= 0) {
                    f.code.push_back({MirOp::LoadFunction, fidx, 0});
                } else {
                    EmitExpr(c->callee.get(), f);
                }
            } else {
                EmitExpr(c->callee.get(), f);
            }
            f.code.push_back({MirOp::Call, static_cast<int>(c->args.size()), 0});
            break;
        }
        case NodeKind::UnaryExpr: {
            const UnaryExpr *u = static_cast<const UnaryExpr *>(expr);
            const bool assign_incdec = (u->op == "++" || u->op == "--");
            if (assign_incdec && u->operand && u->operand->kind == NodeKind::Identifier) {
                const IdentifierExpr *id = static_cast<const IdentifierExpr *>(u->operand.get());
                const bool has_local = locals_.count(id->name) > 0;
                const bool has_upvalue = upvalues_.count(id->name) > 0;
                const bool has_global = globals_.count(id->name) > 0;
                if (!has_local && !has_upvalue && !has_global) {
                    f.code.push_back({MirOp::LoadConst, 0, 0});
                    break;
                }
                if (has_local) {
                    f.code.push_back({MirOp::LoadLocal, locals_[id->name], 0});
                } else if (has_upvalue) {
                    f.code.push_back({MirOp::LoadUpvalue, upvalues_[id->name], 0});
                } else {
                    f.code.push_back({MirOp::LoadGlobal, globals_[id->name], 0});
                }
                if (u->is_postfix) {
                    f.code.push_back({MirOp::Dup, 0, 0});
                }
                int idx = static_cast<int>(f.iconsts.size());
                f.iconsts.push_back(1);
                f.code.push_back({MirOp::LoadConst, idx, 0});
                f.code.push_back({u->op == "++" ? MirOp::Add : MirOp::Sub, 0, 0});
                if (has_local) {
                    f.code.push_back({MirOp::StoreLocal, locals_[id->name], 0});
                } else if (has_upvalue) {
                    f.code.push_back({MirOp::StoreUpvalue, upvalues_[id->name], 0});
                } else {
                    f.code.push_back({MirOp::StoreGlobal, globals_[id->name], 0});
                }
                if (!u->is_postfix) {
                    if (has_local) {
                        f.code.push_back({MirOp::LoadLocal, locals_[id->name], 0});
                    } else if (has_upvalue) {
                        f.code.push_back({MirOp::LoadUpvalue, upvalues_[id->name], 0});
                    } else {
                        f.code.push_back({MirOp::LoadGlobal, globals_[id->name], 0});
                    }
                }
                break;
            }

            if (assign_incdec && u->operand && u->operand->kind == NodeKind::IndexExpr) {
                const IndexExpr *idx = static_cast<const IndexExpr *>(u->operand.get());
                EmitExpr(idx->object.get(), f);
                EmitExpr(idx->index.get(), f);
                f.code.push_back({MirOp::Upset, u->op == "++" ? 1 : 2, u->is_postfix ? 0 : 1});
                break;
            }

            if (assign_incdec && u->operand && u->operand->kind == NodeKind::MemberExpr) {
                const MemberExpr *m = static_cast<const MemberExpr *>(u->operand.get());
                int field_idx = -1;
                auto mi = sema_.member_field_index.find(m);
                if (mi != sema_.member_field_index.end()) {
                    field_idx = mi->second;
                }
                if (field_idx < 0) {
                    f.code.push_back({MirOp::LoadConst, 0, 0});
                    break;
                }
                EmitExpr(m->object.get(), f);
                f.code.push_back({MirOp::LoadClassField, field_idx, 0});
                if (u->is_postfix) {
                    f.code.push_back({MirOp::Dup, 0, 0});
                }
                int one_idx = static_cast<int>(f.iconsts.size());
                f.iconsts.push_back(1);
                f.code.push_back({MirOp::LoadConst, one_idx, 0});
                f.code.push_back({u->op == "++" ? MirOp::Add : MirOp::Sub, 0, 0});
                EmitExpr(m->object.get(), f);
                f.code.push_back({MirOp::StoreClassField, field_idx, 0});
                if (!u->is_postfix) {
                    EmitExpr(m->object.get(), f);
                    f.code.push_back({MirOp::LoadClassField, field_idx, 0});
                }
                break;
            }

            EmitExpr(u->operand.get(), f);
            if (u->op == "!") {
                f.code.push_back({MirOp::LogicNot, 0, 0});
            } else if (u->op == "~") {
                f.code.push_back({MirOp::BitNot, 0, 0});
            } else if (u->op == "-") {
                f.code.push_back({MirOp::Neg, 0, 0});
            }
            break;
        }
        case NodeKind::MemberExpr: {
            const MemberExpr *m = static_cast<const MemberExpr *>(expr);
            EmitExpr(m->object.get(), f);
            auto it = sema_.member_field_index.find(expr);
            int field_idx = (it == sema_.member_field_index.end()) ? -1 : it->second;
            if (field_idx < 0) {
                f.code.push_back({MirOp::LoadConst, 0, 0});
                break;
            }
            f.code.push_back({MirOp::LoadClassField, field_idx, 0});
            break;
        }
        case NodeKind::IndexExpr: {
            const IndexExpr *idx = static_cast<const IndexExpr *>(expr);
            if (idx->is_range) {
                EmitExpr(idx->index.get(), f);
                EmitExpr(idx->object.get(), f);
                EmitExpr(idx->index_end.get(), f);
            } else {
                EmitExpr(idx->object.get(), f);
                EmitExpr(idx->index.get(), f);
            }
            f.code.push_back({MirOp::Index, idx->is_range ? 1 : 0, 0});
            break;
        }
        case NodeKind::BinaryExpr: {
            const BinaryExpr *b = static_cast<const BinaryExpr *>(expr);
            const bool assign = (b->op == "=" || b->op == "+=" || b->op == "-=" || b->op == "*=" || b->op == "/=" || b->op == "%=" || b->op == "&=" || b->op == "|=" || b->op == "^=" || b->op == "<<=" || b->op == ">>=");
            if (assign && b->lhs && b->lhs->kind == NodeKind::Identifier) {
                const IdentifierExpr *id = static_cast<const IdentifierExpr *>(b->lhs.get());
                const bool has_local = locals_.count(id->name) > 0;
                const bool has_upvalue = upvalues_.count(id->name) > 0;
                const bool has_global = globals_.count(id->name) > 0;
                if (b->op != "=") {
                    if (has_local) {
                        f.code.push_back({MirOp::LoadLocal, locals_[id->name], 0});
                    } else if (has_upvalue) {
                        f.code.push_back({MirOp::LoadUpvalue, upvalues_[id->name], 0});
                    } else if (has_global) {
                        f.code.push_back({MirOp::LoadGlobal, globals_[id->name], 0});
                    }
                }
                EmitExpr(b->rhs.get(), f);

                if (b->op == "+=") f.code.push_back({MirOp::Add, 0, 0});
                else if (b->op == "-=") f.code.push_back({MirOp::Sub, 0, 0});
                else if (b->op == "*=") f.code.push_back({MirOp::Mul, 0, 0});
                else if (b->op == "/=") f.code.push_back({MirOp::Div, 0, 0});
                else if (b->op == "%=") f.code.push_back({MirOp::Mod, 0, 0});
                else if (b->op == "&=") f.code.push_back({MirOp::BitAnd, 0, 0});
                else if (b->op == "|=") f.code.push_back({MirOp::BitOr, 0, 0});
                else if (b->op == "^=") f.code.push_back({MirOp::BitXor, 0, 0});
                else if (b->op == "<<=") f.code.push_back({MirOp::Shl, 0, 0});
                else if (b->op == ">>=") f.code.push_back({MirOp::Shr, 0, 0});

                if (has_local) {
                    f.code.push_back({MirOp::Dup, 0, 0});
                    f.code.push_back({MirOp::StoreLocal, locals_[id->name], 0});
                } else if (has_upvalue) {
                    f.code.push_back({MirOp::Dup, 0, 0});
                    f.code.push_back({MirOp::StoreUpvalue, upvalues_[id->name], 0});
                } else if (has_global) {
                    f.code.push_back({MirOp::Dup, 0, 0});
                    f.code.push_back({MirOp::StoreGlobal, globals_[id->name], 0});
                }
                break;
            }

            if (assign && b->lhs && b->lhs->kind == NodeKind::MemberExpr) {
                const MemberExpr *m = static_cast<const MemberExpr *>(b->lhs.get());
                int field_idx = -1;
                auto mi = sema_.member_field_index.find(m);
                if (mi != sema_.member_field_index.end()) {
                    field_idx = mi->second;
                }
                if (field_idx < 0) {
                    f.code.push_back({MirOp::LoadConst, 0, 0});
                    break;
                }

                if (b->op != "=") {
                    EmitExpr(m->object.get(), f);
                    f.code.push_back({MirOp::LoadClassField, field_idx, 0});
                }
                EmitExpr(b->rhs.get(), f);
                if (b->op == "+=") f.code.push_back({MirOp::Add, 0, 0});
                else if (b->op == "-=") f.code.push_back({MirOp::Sub, 0, 0});
                else if (b->op == "*=") f.code.push_back({MirOp::Mul, 0, 0});
                else if (b->op == "/=") f.code.push_back({MirOp::Div, 0, 0});
                else if (b->op == "%=") f.code.push_back({MirOp::Mod, 0, 0});
                else if (b->op == "&=") f.code.push_back({MirOp::BitAnd, 0, 0});
                else if (b->op == "|=") f.code.push_back({MirOp::BitOr, 0, 0});
                else if (b->op == "^=") f.code.push_back({MirOp::BitXor, 0, 0});
                else if (b->op == "<<=") f.code.push_back({MirOp::Shl, 0, 0});
                else if (b->op == ">>=") f.code.push_back({MirOp::Shr, 0, 0});

                EmitExpr(m->object.get(), f);
                f.code.push_back({MirOp::StoreClassField, field_idx, 0});
                EmitExpr(m->object.get(), f);
                f.code.push_back({MirOp::LoadClassField, field_idx, 0});
                break;
            }

            if (assign && b->lhs && b->lhs->kind == NodeKind::IndexExpr) {
                const IndexExpr *idx = static_cast<const IndexExpr *>(b->lhs.get());
                if (b->op != "=") {
                    EmitExpr(idx->object.get(), f);
                    EmitExpr(idx->index.get(), f);
                    f.code.push_back({MirOp::Index, 0, 0});
                }
                EmitExpr(b->rhs.get(), f);
                if (b->op == "+=") f.code.push_back({MirOp::Add, 0, 0});
                else if (b->op == "-=") f.code.push_back({MirOp::Sub, 0, 0});
                else if (b->op == "*=") f.code.push_back({MirOp::Mul, 0, 0});
                else if (b->op == "/=") f.code.push_back({MirOp::Div, 0, 0});
                else if (b->op == "%=") f.code.push_back({MirOp::Mod, 0, 0});
                else if (b->op == "&=") f.code.push_back({MirOp::BitAnd, 0, 0});
                else if (b->op == "|=") f.code.push_back({MirOp::BitOr, 0, 0});
                else if (b->op == "^=") f.code.push_back({MirOp::BitXor, 0, 0});
                else if (b->op == "<<=") f.code.push_back({MirOp::Shl, 0, 0});
                else if (b->op == ">>=") f.code.push_back({MirOp::Shr, 0, 0});

                EmitExpr(idx->object.get(), f);
                EmitExpr(idx->index.get(), f);
                f.code.push_back({MirOp::StoreIndex, 0, 0});
                break;
            }

            if (b->op == "&&") {
                EmitExpr(b->lhs.get(), f);
                f.code.push_back({MirOp::Dup, 0, 0});
                int jfalse_pos = static_cast<int>(f.code.size());
                f.code.push_back({MirOp::JumpIfFalse, -1, 0});
                f.code.push_back({MirOp::Pop, 0, 0});
                EmitExpr(b->rhs.get(), f);
                int jmp_end_pos = static_cast<int>(f.code.size());
                f.code.push_back({MirOp::Jump, -1, 0});
                f.code[jfalse_pos].a = static_cast<int>(f.code.size());
                f.code[jmp_end_pos].a = static_cast<int>(f.code.size());
            }
            else if (b->op == "||") {
                EmitExpr(b->lhs.get(), f);
                f.code.push_back({MirOp::Dup, 0, 0});
                int jtrue_pos = static_cast<int>(f.code.size());
                f.code.push_back({MirOp::JumpIfTrue, -1, 0});
                f.code.push_back({MirOp::Pop, 0, 0});
                EmitExpr(b->rhs.get(), f);
                int jmp_end_pos = static_cast<int>(f.code.size());
                f.code.push_back({MirOp::Jump, -1, 0});
                f.code[jtrue_pos].a = static_cast<int>(f.code.size());
                f.code[jmp_end_pos].a = static_cast<int>(f.code.size());
            }
            else {
                EmitExpr(b->lhs.get(), f);
                EmitExpr(b->rhs.get(), f);
                if (b->op == "+") f.code.push_back({MirOp::Add, 0, 0});
                else if (b->op == "-") f.code.push_back({MirOp::Sub, 0, 0});
                else if (b->op == "*") f.code.push_back({MirOp::Mul, 0, 0});
                else if (b->op == "/") f.code.push_back({MirOp::Div, 0, 0});
                else if (b->op == "%") f.code.push_back({MirOp::Mod, 0, 0});
                else if (b->op == "<<") f.code.push_back({MirOp::Shl, 0, 0});
                else if (b->op == ">>") f.code.push_back({MirOp::Shr, 0, 0});
                else if (b->op == "&") f.code.push_back({MirOp::BitAnd, 0, 0});
                else if (b->op == "|") f.code.push_back({MirOp::BitOr, 0, 0});
                else if (b->op == "^") f.code.push_back({MirOp::BitXor, 0, 0});
                else if (b->op == "==") f.code.push_back({MirOp::Eq, 0, 0});
                else if (b->op == "!=") f.code.push_back({MirOp::Neq, 0, 0});
                else if (b->op == "<") f.code.push_back({MirOp::Lt, 0, 0});
                else if (b->op == "<=") f.code.push_back({MirOp::Lte, 0, 0});
                else if (b->op == ">") f.code.push_back({MirOp::Gt, 0, 0});
                else if (b->op == ">=") f.code.push_back({MirOp::Gte, 0, 0});
                else f.code.push_back({MirOp::LoadConst, 0, 0});
            }
            break;
        }
        case NodeKind::LambdaExpr: {
            const LambdaExpr *le = static_cast<const LambdaExpr *>(expr);
            auto lit = lambda_index_by_expr_.find(le);
            if (lit != lambda_index_by_expr_.end()) {
                f.code.push_back({MirOp::LoadFunction, lit->second, 0});
            } else {
                f.code.push_back({MirOp::LoadConst, 0, 0});
            }
            break;
        }
        case NodeKind::NewExpr: {
            const NewExpr *ne = static_cast<const NewExpr *>(expr);
            if (ne->is_array_literal) {
                for (const auto &it : ne->array_items) {
                    EmitExpr(it.get(), f);
                }
                f.code.push_back({MirOp::NewArray, static_cast<int>(ne->array_items.size()), 0});
                break;
            }
            int class_index = -1;
            for (int i = 0; i < static_cast<int>(sema_.class_order.size()); ++i) {
                if (sema_.class_order[i] == ne->class_name) {
                    class_index = i;
                    break;
                }
            }
            if (class_index >= 0) {
                f.code.push_back({MirOp::NewClass, class_index, 0});
            } else {
                f.code.push_back({MirOp::LoadConst, 0, 0});
            }
            break;
        }
        case NodeKind::MappingLiteralExpr: {
            const MappingLiteralExpr *m = static_cast<const MappingLiteralExpr *>(expr);
            for (const auto &pair : m->pairs) {
                EmitExpr(pair.first.get(), f);
                EmitExpr(pair.second.get(), f);
            }
            f.code.push_back({MirOp::NewMapping, static_cast<int>(m->pairs.size()), 0});
            break;
        }
        case NodeKind::TernaryExpr: {
            const TernaryExpr *t = static_cast<const TernaryExpr *>(expr);
            EmitExpr(t->cond.get(), f);
            int jfalse_pos = static_cast<int>(f.code.size());
            f.code.push_back({MirOp::JumpIfFalse, -1, 0});
            EmitExpr(t->then_expr.get(), f);
            int jmp_end_pos = static_cast<int>(f.code.size());
            f.code.push_back({MirOp::Jump, -1, 0});
            f.code[jfalse_pos].a = static_cast<int>(f.code.size());
            EmitExpr(t->else_expr.get(), f);
            f.code[jmp_end_pos].a = static_cast<int>(f.code.size());
            break;
        }
        default:
            f.code.push_back({MirOp::LoadConst, 0, 0});
            break;
        }

        const int end = static_cast<int>(f.code.size());
        if (line > 0) {
            for (int i = begin; i < end; ++i) {
                if (f.code[i].line <= 0) {
                    f.code[i].line = line;
                }
            }
        }
    }

    void CollectLambdas(const Module &module) {
        for (const auto &decl : module.decls) {
            CollectLambdasInStmt(decl.get());
        }
    }

    void CollectLambdasInStmt(const Stmt *stmt) {
        if (!stmt) return;
        switch (stmt->kind) {
        case NodeKind::VarDecl: {
            const VarDeclStmt *vd = static_cast<const VarDeclStmt *>(stmt);
            CollectLambdasInExpr(vd->init.get());
            break;
        }
        case NodeKind::ExprStmt: {
            const ExprStmt *es = static_cast<const ExprStmt *>(stmt);
            CollectLambdasInExpr(es->expr.get());
            break;
        }
        case NodeKind::ReturnStmt: {
            const ReturnStmt *r = static_cast<const ReturnStmt *>(stmt);
            CollectLambdasInExpr(r->value.get());
            break;
        }
        case NodeKind::IfStmt: {
            const IfStmt *is = static_cast<const IfStmt *>(stmt);
            CollectLambdasInExpr(is->cond.get());
            CollectLambdasInStmt(is->then_branch.get());
            CollectLambdasInStmt(is->else_branch.get());
            break;
        }
        case NodeKind::WhileStmt: {
            const WhileStmt *ws = static_cast<const WhileStmt *>(stmt);
            CollectLambdasInExpr(ws->cond.get());
            CollectLambdasInStmt(ws->body.get());
            break;
        }
        case NodeKind::DoWhileStmt: {
            const DoWhileStmt *dw = static_cast<const DoWhileStmt *>(stmt);
            CollectLambdasInStmt(dw->body.get());
            CollectLambdasInExpr(dw->cond.get());
            break;
        }
        case NodeKind::ForStmt: {
            const ForStmt *fs = static_cast<const ForStmt *>(stmt);
            CollectLambdasInStmt(fs->init.get());
            CollectLambdasInExpr(fs->cond.get());
            CollectLambdasInExpr(fs->post.get());
            CollectLambdasInStmt(fs->body.get());
            break;
        }
        case NodeKind::ForeachStmt: {
            const ForeachStmt *fe = static_cast<const ForeachStmt *>(stmt);
            CollectLambdasInExpr(fe->container.get());
            CollectLambdasInStmt(fe->body.get());
            break;
        }
        case NodeKind::Block: {
            const BlockStmt *b = static_cast<const BlockStmt *>(stmt);
            for (const auto &st : b->stmts) {
                CollectLambdasInStmt(st.get());
            }
            break;
        }
        case NodeKind::FunctionDecl: {
            const FunctionDecl *fn = static_cast<const FunctionDecl *>(stmt);
            for (const auto &st : fn->body) {
                CollectLambdasInStmt(st.get());
            }
            break;
        }
        default:
            break;
        }
    }

    void CollectLambdasInExpr(const Expr *expr) {
        if (!expr) return;
        switch (expr->kind) {
        case NodeKind::LambdaExpr: {
            const LambdaExpr *lambda = static_cast<const LambdaExpr *>(expr);
            int idx = static_cast<int>(function_index_by_name_.size() + lambdas_in_order_.size());
            lambda_index_by_expr_[lambda] = idx;
            lambdas_in_order_.push_back(lambda);
            for (const auto &st : lambda->body) {
                CollectLambdasInStmt(st.get());
            }
            break;
        }
        case NodeKind::BinaryExpr: {
            const BinaryExpr *b = static_cast<const BinaryExpr *>(expr);
            CollectLambdasInExpr(b->lhs.get());
            CollectLambdasInExpr(b->rhs.get());
            break;
        }
        case NodeKind::UnaryExpr: {
            const UnaryExpr *u = static_cast<const UnaryExpr *>(expr);
            CollectLambdasInExpr(u->operand.get());
            break;
        }
        case NodeKind::CallExpr: {
            const CallExpr *c = static_cast<const CallExpr *>(expr);
            CollectLambdasInExpr(c->callee.get());
            for (const auto &it : c->args) {
                CollectLambdasInExpr(it.get());
            }
            break;
        }
        case NodeKind::MemberExpr: {
            const MemberExpr *m = static_cast<const MemberExpr *>(expr);
            CollectLambdasInExpr(m->object.get());
            break;
        }
        case NodeKind::IndexExpr: {
            const IndexExpr *idx = static_cast<const IndexExpr *>(expr);
            CollectLambdasInExpr(idx->object.get());
            CollectLambdasInExpr(idx->index.get());
            CollectLambdasInExpr(idx->index_end.get());
            break;
        }
        case NodeKind::TernaryExpr: {
            const TernaryExpr *t = static_cast<const TernaryExpr *>(expr);
            CollectLambdasInExpr(t->cond.get());
            CollectLambdasInExpr(t->then_expr.get());
            CollectLambdasInExpr(t->else_expr.get());
            break;
        }
        case NodeKind::MappingLiteralExpr: {
            const MappingLiteralExpr *m = static_cast<const MappingLiteralExpr *>(expr);
            for (const auto &pair : m->pairs) {
                CollectLambdasInExpr(pair.first.get());
                CollectLambdasInExpr(pair.second.get());
            }
            break;
        }
        default:
            break;
        }
    }

    MirFunction BuildLambda(const LambdaExpr &lambda) {
        MirFunction f;

        auto it = sema_.lambdas.find(&lambda);
        if (it != sema_.lambdas.end()) {
            const FunctionSemanticInfo &info = it->second;
            f.name = info.name;
            f.nargs = static_cast<int>(lambda.params.size());
            f.locals = info.locals;
            f.upvalues = info.captures;
            f.upvalue_source_kind = info.capture_source_kind;
            f.upvalue_source_index = info.capture_source_index;
        } else {
            f.name = "<lambda>";
            f.nargs = static_cast<int>(lambda.params.size());
        }

        locals_.clear();
        for (int i = 0; i < static_cast<int>(lambda.params.size()); ++i) {
            locals_[lambda.params[i]] = i;
        }
        int base = static_cast<int>(locals_.size());
        for (int i = 0; i < static_cast<int>(f.locals.size()); ++i) {
            locals_[f.locals[i]] = base + i;
        }

        upvalues_.clear();
        for (int i = 0; i < static_cast<int>(f.upvalues.size()); ++i) {
            upvalues_[f.upvalues[i]] = i;
        }

        for (const auto &st : lambda.body) {
            EmitStmt(st.get(), f);
        }
        f.code.push_back({MirOp::Return, 0, 0});
        return f;
    }

    MirFunction BuildInitFunction(const Module &module) {
        MirFunction f;
        f.name = "__init_globals";
        f.nargs = 0;

        locals_.clear();
        upvalues_.clear();

        for (const auto &decl : module.decls) {
            if (decl->kind != NodeKind::VarDecl) continue;
            const VarDeclStmt *vd = static_cast<const VarDeclStmt *>(decl.get());
            if (!globals_.count(vd->name)) continue;
            if (vd->init) {
                EmitExpr(vd->init.get(), f);
            } else {
                int zero_idx = static_cast<int>(f.iconsts.size());
                f.iconsts.push_back(0);
                f.code.push_back({MirOp::LoadConst, zero_idx, 0});
            }
            f.code.push_back({MirOp::StoreGlobal, globals_[vd->name], 0});
        }

        f.code.push_back({MirOp::Return, 0, 0});
        return f;
    }

    const SemanticModel &sema_;
    int find_function_index(const std::string &name) const {
        auto iter = function_index_by_name_.find(name);
        if (iter != function_index_by_name_.end()) {
            return iter->second;
        }
        return -1;
    }

    std::unordered_map<std::string, int> locals_;
    std::unordered_map<std::string, int> upvalues_;
    std::unordered_map<std::string, int> globals_;
    std::unordered_map<std::string, int> function_index_by_name_;
    std::unordered_map<const LambdaExpr *, int> lambda_index_by_expr_;
    std::vector<const LambdaExpr *> lambdas_in_order_;
    std::vector<LoopContext> loop_stack_;
    std::string current_function_name_;
    int current_nargs_ = 0;
    std::vector<std::string> current_params_;
    int temp_counter_ = 0;
};

PipelineResult CompileSourceToMir(
    const std::string &path,
    const std::string &text,
    const std::vector<std::string> &include_dirs,
    const PipelineOptions &options) {
    PipelineResult result;

    SourceFile source;
    source.path = path;
    PreprocessResult pre = PreprocessSource(path, text, &result.diagnostics, include_dirs);
    source.text = pre.text;
    result.source_map = std::move(pre.source_map);

    Lexer lexer(&result.diagnostics);
    std::vector<Token> tokens = lexer.Tokenize(source);

    Parser parser(&result.diagnostics);
    std::unique_ptr<Module> module = parser.Parse(tokens);

    if (module) {
        Sema sema(&result.diagnostics);
        SemanticModel model = sema.Analyze(*module);

        MirBuilder builder(model);
        result.module = builder.Build(*module);
        if (options.keep_pre_opt_module) {
            result.pre_opt_module = result.module;
            result.has_pre_opt_module = true;
        }
        MirOptStats st = OptimizeMirModuleWithStats(&result.module);
        result.mir_opt_stats = st;
        result.mir_instr_before_opt = st.before_instr;
        result.mir_instr_after_opt = st.after_instr;

        Verify2Result vr = VerifyMirModule(result.module);
        if (!vr.ok) {
            SourceSpan sp;
            result.diagnostics.Add(
                DiagnosticLevel::Error,
                sp,
                "MIR verify failed at function=" + std::to_string(vr.function_index) +
                ", instr=" + std::to_string(vr.instr_index) +
                ": " + vr.message);
        }
    }

    return result;
}

} // namespace frontend
} // namespace lpc
