// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#ifndef EXPRESSIONPARSER_PARSER_H
#define EXPRESSIONPARSER_PARSER_H

#include <corerror.h>
#ifdef FEATURE_PAL
#include <specstrings_undef.h>
#endif

#include <cstdint>
#include <list>
#include <string>
#include <string_view>
#include <utility>

namespace dncdbg::Parser
{

// Roslyn SyntaxKind
enum class SyntaxKind : uint8_t
{
    IdentifierName,
    GenericName,
    InvocationExpression,
    ObjectCreationExpression,
    ElementAccessExpression,
    ElementBindingExpression,
    NumericLiteralExpression,
    StringLiteralExpression,
    CharacterLiteralExpression,
    PredefinedType,
    QualifiedName,
    AliasQualifiedName,
    MemberBindingExpression,
    ConditionalExpression,
    SimpleMemberAccessExpression,
    PointerMemberAccessExpression,
    CastExpression,
    AsExpression,
    AddExpression,
    MultiplyExpression,
    SubtractExpression,
    DivideExpression,
    ModuloExpression,
    LeftShiftExpression,
    RightShiftExpression,
    UnsignedRightShiftExpression,
    BitwiseAndExpression,
    BitwiseOrExpression,
    ExclusiveOrExpression,
    LogicalAndExpression,
    LogicalOrExpression,
    EqualsExpression,
    NotEqualsExpression,
    GreaterThanExpression,
    LessThanExpression,
    GreaterThanOrEqualExpression,
    LessThanOrEqualExpression,
    IsExpression,
    UnaryPlusExpression,
    UnaryMinusExpression,
    LogicalNotExpression,
    BitwiseNotExpression,
    TrueLiteralExpression,
    FalseLiteralExpression,
    NullLiteralExpression,
    PreIncrementExpression,
    PostIncrementExpression,
    PreDecrementExpression,
    PostDecrementExpression,
    SizeOfExpression,
    TypeOfExpression,
    CoalesceExpression,
    ThisExpression
};

struct Opcode
{
    std::string str;
    uint32_t count{0};
    SyntaxKind kind;

    explicit Opcode(SyntaxKind kind_)
        : kind(kind_)
    {
    }

    Opcode(SyntaxKind kind_, std::string str_)
        : str(std::move(str_)),
          kind(kind_)
    {
    }

    Opcode(SyntaxKind kind_, uint32_t count_)
        : count(count_),
          kind(kind_)
    {
    }

    Opcode(SyntaxKind kind_, std::string str_, uint32_t count_)
        : str(std::move(str_)),
          count(count_),
          kind(kind_)
    {
    }
};

HRESULT GenerateProgram(std::string_view expression, std::list<Opcode> &stackProgram, std::string &output);

} // namespace dncdbg::Parser

#endif // EXPRESSIONPARSER_PARSER_H
