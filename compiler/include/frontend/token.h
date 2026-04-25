#ifndef LPC_FRONTEND_TOKEN_H
#define LPC_FRONTEND_TOKEN_H

#include <string>

#include "frontend/source.h"

namespace lpc {
namespace frontend {

enum class TokenKind {
    EndOfFile,
    Identifier,
    Number,
    String,
    KeywordFun,
    KeywordVar,
    KeywordReturn,
    KeywordIf,
    KeywordElse,
    KeywordWhile,
    KeywordFor,
    KeywordSwitch,
    KeywordCase,
    KeywordDefault,
    KeywordBreak,
    KeywordContinue,
    KeywordCatch,
    KeywordForeach,
    KeywordIn,
    KeywordClass,
    KeywordNew,
    KeywordInherit,
    KeywordVoid,
    KeywordInt,
    KeywordFloat,
    KeywordStringType,
    KeywordObject,
    KeywordMapping,
    KeywordMixed,
    KeywordStatic,
    KeywordPrivate,
    KeywordPublic,
    KeywordNomask,
    KeywordTrue,
    KeywordFalse,
    LParen,
    RParen,
    LBrace,
    RBrace,
    LArrInit,
    RArrInit,
    LBracket,
    RBracket,
    Comma,
    Colon,
    Semicolon,
    Assign,
    EqEq,
    NotEq,
    Lt,
    Lte,
    Gt,
    Gte,
    Plus,
    Minus,
    Star,
    Slash,
    Amp,
    Pipe,
    Caret,
    Bang,
    Tilde,
    ShiftLeft,
    ShiftRight,
    LogicAnd,
    LogicOr,
    PlusPlus,
    MinusMinus,
    PlusAssign,
    MinusAssign,
    StarAssign,
    SlashAssign,
    Percent,
    PercentAssign,
    AmpAssign,
    PipeAssign,
    CaretAssign,
    ShiftLeftAssign,
    ShiftRightAssign,
    Float,
    Question,
    KeywordDo,
    DotDot,
    Arrow,
    LMapInit,
    Unknown,
};

struct Token {
    TokenKind kind = TokenKind::Unknown;
    std::string lexeme;
    SourceSpan span;
};

} // namespace frontend
} // namespace lpc

#endif
