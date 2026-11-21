// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "expressionparser/parser.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <exception>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>
#include <tree_sitter/api.h>
extern "C" const TSLanguage *tree_sitter_c_sharp();

namespace dncdbg::Parser
{

namespace
{

// Initial capacity of the traversal stack, enough for typical expressions.
constexpr size_t initialWorkStackCapacity = 32;

// Returns the raw source text of a specific node.
std::string_view GetNodeText(TSNode node, std::string_view source)
{
    if (ts_node_is_null(node))
    {
        return {};
    }

    const uint32_t start = ts_node_start_byte(node);
    const uint32_t end = ts_node_end_byte(node);
    return source.substr(start, end - start);
}

// Packs a 1-4 character string into a uint32_t for use as a lookup key.
// The first character goes in the lowest byte, and each subsequent character
// goes in the next higher byte. This guarantees that operators of different
// lengths never collide (e.g., "<", "<<", and "<<<" produce distinct values),
// which a fixed two-byte scheme cannot guarantee for 3+ character tokens
// such as ">>>" (UnsignedRightShiftExpression) or future assignment
// operators such as ">>>=" (UnsignedRightShiftAssignmentExpression).
constexpr uint32_t packString(std::string_view str)
{
    if (str.empty() || str.size() > 4)
    {
        assert(false && "String must be 1 to 4 characters only");
    }

    constexpr uint8_t bitsInByte = 8;
    uint32_t result = 0;
    for (size_t i = 0; i < str.size(); ++i)
    {
        result |= static_cast<uint32_t>(static_cast<uint8_t>(str.at(i)))
                  << (bitsInByte * static_cast<uint32_t>(i));
    }

    return result;
}

// Returns the string literal content without the surrounding quotes. Verbatim string literals
// (@"...") keep their content unchanged, except that a doubled quote is converted to a single
// quote character. The optional UTF-8 encoding suffix (u8/U8) is dropped as well.
std::string GetStringLiteralContent(TSNode node, std::string_view source)
{
    std::string_view str = GetNodeText(node, source);

    // Drop the encoding suffix that may follow the closing quote.
    if (str.size() >= 2 && str.back() == '8' &&
        (str.at(str.size() - 2) == 'u' || str.at(str.size() - 2) == 'U'))
    {
        str.remove_suffix(2);
    }

    if (str.size() >= 3 && str.compare(0, 2, "@\"") == 0)
    {
        str.remove_prefix(2);
        if (!str.empty() && str.back() == '"')
        {
            str.remove_suffix(1);
        }

        // Copy runs of ordinary characters in bulk. Each doubled quote is copied once and the
        // second quote of the pair is skipped.
        std::string result;
        result.reserve(str.size());
        while (!str.empty())
        {
            const size_t quote = str.find('"');
            if (quote == std::string_view::npos)
            {
                result.append(str);
                break;
            }
            result.append(str.substr(0, quote + 1));
            str.remove_prefix(quote + 1);
            if (!str.empty() && str.front() == '"')
            {
                str.remove_prefix(1); // Skip the second quote of the doubled pair.
            }
        }
        return result;
    }

    if (str.size() >= 2 && str.front() == '"' && str.back() == '"')
    {
        str.remove_prefix(1);
        str.remove_suffix(1);
    }

    return std::string(str);
}

// Traversal work items. The stack is processed LIFO, so children are pushed in reverse order.
//   Visit    - a TSNode that must be dispatched through the handler map.
//   Emit     - post-order work: append an opcode (kind, count) after the node's children.
//   EmitText - same as Emit, but the opcode string is the source text of `node`.
enum class WorkType : uint8_t
{
    Visit,
    Emit,
    EmitText
};

struct WorkItem
{
    TSNode node;
    uint32_t count;
    SyntaxKind kind;
    WorkType type;
};

using WorkStack = std::vector<WorkItem>;
using SyntaxKindHandler = HRESULT (*)(TSNode node, std::string_view source, std::list<Opcode> &program,
                                      std::string &output, WorkStack &workStack);

void PushVisit(WorkStack &workStack, TSNode node)
{
    workStack.push_back(WorkItem{node, 0, SyntaxKind::IdentifierName, WorkType::Visit});
}

void PushEmit(WorkStack &workStack, SyntaxKind kind, uint32_t count = 0)
{
    workStack.push_back(WorkItem{TSNode{}, count, kind, WorkType::Emit});
}

void PushEmitText(WorkStack &workStack, SyntaxKind kind, TSNode node, uint32_t count = 0)
{
    workStack.push_back(WorkItem{node, count, kind, WorkType::EmitText});
}

HRESULT StringLiteralHandler(TSNode node, std::string_view source, std::list<Opcode> &program,
                             std::string &/*output*/, WorkStack &/*workStack*/)
{
    program.emplace_back(SyntaxKind::StringLiteralExpression, GetStringLiteralContent(node, source));
    return S_OK;
}

// Returns a per-thread tree-sitter parser for the C# grammar. Creating a parser is expensive,
// so it is created once per thread and reused by every call. Returns nullptr on failure.
TSParser *GetThreadParser()
{
    using ParserPtr = std::unique_ptr<TSParser, decltype(&ts_parser_delete)>;
    thread_local ParserPtr parser(nullptr, &ts_parser_delete);

    if (!parser)
    {
        TSParser *created = ts_parser_new();
        if (created == nullptr)
        {
            return nullptr;
        }

        if (!ts_parser_set_language(created, tree_sitter_c_sharp()))
        {
            ts_parser_delete(created);
            return nullptr;
        }
        parser.reset(created);
    }

    return parser.get();
}

HRESULT GenerateExecutionSteps(TSNode startNode, std::string_view source, std::list<Opcode> &program, std::string &output)
{
    static const std::unordered_map<std::string_view, SyntaxKindHandler> syntaxKindHandlerMap{
    // Roslyn: NullLiteralExpression
    {"null_literal",
        [](TSNode /*node*/, std::string_view /*source*/, std::list<Opcode> &program, std::string &/*output*/, WorkStack &/*workStack*/) -> HRESULT
        {
            program.emplace_back(SyntaxKind::NullLiteralExpression);
            return S_OK;
        }
    },
    // Roslyn: ThisExpression
    {"this",
        [](TSNode /*node*/, std::string_view /*source*/, std::list<Opcode> &program, std::string &/*output*/, WorkStack &/*workStack*/) -> HRESULT
        {
            program.emplace_back(SyntaxKind::ThisExpression);
            return S_OK;
        }
    },
    // Roslyn: TrueLiteralExpression (one-word expression: "true")
    {"true",
        [](TSNode /*node*/, std::string_view /*source*/, std::list<Opcode> &program, std::string &/*output*/, WorkStack &/*workStack*/) -> HRESULT
        {
            program.emplace_back(SyntaxKind::TrueLiteralExpression);
            return S_OK;
        }
    },
    // Roslyn: FalseLiteralExpression (one-word expression: "false")
    {"false",
        [](TSNode /*node*/, std::string_view /*source*/, std::list<Opcode> &program, std::string &/*output*/, WorkStack &/*workStack*/) -> HRESULT
        {
            program.emplace_back(SyntaxKind::FalseLiteralExpression);
            return S_OK;
        }
    },
    // Roslyn: TrueLiteralExpression and FalseLiteralExpression (complex expression)
    {"boolean_literal",
        [](TSNode node, std::string_view source, std::list<Opcode> &program, std::string &output, WorkStack &/*workStack*/) -> HRESULT
        {
            const std::string_view rawText = GetNodeText(node, source);
            if (rawText == "true")
            {
                program.emplace_back(SyntaxKind::TrueLiteralExpression);
            }
            else if (rawText == "false")
            {
                program.emplace_back(SyntaxKind::FalseLiteralExpression);
            }
            else
            {
                output = "Unknown boolean literal expression: " + std::string(rawText);
                return E_INVALIDARG;
            }
            return S_OK;
        }
    },
    // Roslyn: IdentifierName
    {"identifier",
        [](TSNode node, std::string_view source, std::list<Opcode> &program, std::string &/*output*/, WorkStack &/*workStack*/) -> HRESULT
        {
            program.emplace_back(SyntaxKind::IdentifierName, std::string(GetNodeText(node, source)));
            return S_OK;
        }
    },
    // Roslyn: GenericName (e.g., List<int>)
    {"generic_name",
        [](TSNode node, std::string_view /*source*/, std::list<Opcode> &/*program*/, std::string &/*output*/, WorkStack &workStack) -> HRESULT
        {
            const TSNode nameNode = ts_node_named_child(node, 0);
            const TSNode typeArgs = ts_node_named_child(node, 1);
            const uint32_t count = ts_node_named_child_count(typeArgs);

            // Emit GenericName (name text taken from nameNode) after all type-argument children.
            PushEmitText(workStack, SyntaxKind::GenericName, nameNode, count);
            for (uint32_t i = count; i > 0; --i)
            {
                PushVisit(workStack, ts_node_named_child(typeArgs, i - 1));
            }
            return S_OK;
        }
    },
    // Roslyn: QualifiedName (e.g., System.Collections)
    {"qualified_name",
        [](TSNode node, std::string_view /*source*/, std::list<Opcode> &/*program*/, std::string &/*output*/, WorkStack &workStack) -> HRESULT
        {
            const TSNode left = ts_node_named_child(node, 0);
            const TSNode right = ts_node_named_child(node, 1);

            PushEmit(workStack, SyntaxKind::QualifiedName);
            PushVisit(workStack, right);
            PushVisit(workStack, left);
            return S_OK;
        }
    },
    // Roslyn: AliasQualifiedName (e.g., global::System)
    {"alias_qualified_name",
        [](TSNode /*node*/, std::string_view /*source*/, std::list<Opcode> &/*program*/, std::string &output, WorkStack &/*workStack*/) -> HRESULT
        {
            // TODO implement in evaluation/stackmachine: PUSH_ALIAS(alias text), then visit the name and resolve it.
            output = "Alias qualified name not implemented.";
            return E_NOTIMPL;
        }
    },
    // Roslyn: NumericLiteralExpression (integer)
    {"integer_literal",
        [](TSNode node, std::string_view source, std::list<Opcode> &program, std::string &/*output*/, WorkStack &/*workStack*/) -> HRESULT
        {
            program.emplace_back(SyntaxKind::NumericLiteralExpression, std::string(GetNodeText(node, source)), uint32_t{0});
            return S_OK;
        }
    },
    // Roslyn: NumericLiteralExpression (real)
    {"real_literal",
        [](TSNode node, std::string_view source, std::list<Opcode> &program, std::string &/*output*/, WorkStack &/*workStack*/) -> HRESULT
        {
            program.emplace_back(SyntaxKind::NumericLiteralExpression, std::string(GetNodeText(node, source)), uint32_t{1});
            return S_OK;
        }
    },
    // Roslyn: StringLiteralExpression
    {"string_literal", StringLiteralHandler},
    // Roslyn: StringLiteralExpression
    {"verbatim_string_literal", StringLiteralHandler},
    // Roslyn: CharacterLiteralExpression
    {"character_literal",
        [](TSNode node, std::string_view source, std::list<Opcode> &program, std::string &/*output*/, WorkStack &/*workStack*/) -> HRESULT
        {
            std::string_view ch = GetNodeText(node, source);
            if (ch.size() >= 2 && ch.front() == '\'' && ch.back() == '\'')
            {
                ch.remove_prefix(1);
                ch.remove_suffix(1);
            }
            program.emplace_back(SyntaxKind::CharacterLiteralExpression, std::string(ch));
            return S_OK;
        }
    },
    // Roslyn: PredefinedType (e.g., int, string, void)
    {"predefined_type",
        [](TSNode node, std::string_view source, std::list<Opcode> &program, std::string &/*output*/, WorkStack &/*workStack*/) -> HRESULT
        {
            program.emplace_back(SyntaxKind::PredefinedType, std::string(GetNodeText(node, source)));
            return S_OK;
        }
    },
    // Roslyn: ObjectCreationExpression (e.g., new Foo(1, 2)). Object/collection
    // initializers (`new Foo { X = 1 }`) are rejected with a clear error rather than
    // being silently dropped, since ignoring them would produce an uninitialized object.
    {"object_creation_expression",
        [](TSNode node, std::string_view /*source*/, std::list<Opcode> &/*program*/, std::string &output, WorkStack &workStack) -> HRESULT
        {
            static constexpr std::string_view typeField = "type";
            static constexpr std::string_view argumentsField = "arguments";
            static constexpr std::string_view initializerField = "initializer";
            const TSNode typeNode = ts_node_child_by_field_name(node, typeField.data(), static_cast<uint32_t>(typeField.size()));
            const TSNode argList = ts_node_child_by_field_name(node, argumentsField.data(), static_cast<uint32_t>(argumentsField.size()));
            const TSNode initializerNode = ts_node_child_by_field_name(node, initializerField.data(), static_cast<uint32_t>(initializerField.size()));

            if (!ts_node_is_null(initializerNode))
            {
                output = "Object/collection initializers are not implemented (only plain constructor calls, e.g. `new Foo(1, 2)`).";
                return E_NOTIMPL;
            }

            const uint32_t argCount = ts_node_is_null(argList) ? 0 : ts_node_named_child_count(argList);

            // Post-order: ObjectCreationExpression after the args and the type. Children are
            // pushed in reverse order so that the type is processed first.
            PushEmit(workStack, SyntaxKind::ObjectCreationExpression, argCount);
            for (uint32_t i = argCount; i > 0; --i)
            {
                PushVisit(workStack, ts_node_named_child(argList, i - 1));
            }
            PushVisit(workStack, typeNode);
            return S_OK;
        }
    },
    // Roslyn: SimpleMemberAccessExpression (a.b) AND PointerMemberAccessExpression (a->b)
    {"member_access_expression",
        [](TSNode node, std::string_view source, std::list<Opcode> &/*program*/, std::string &output, WorkStack &workStack) -> HRESULT
        {
            const TSNode expr = ts_node_child(node, 0);
            const TSNode op = ts_node_child(node, 1); // The operator token (. or ->)
            const TSNode name = ts_node_child(node, 2);

            // Check which separator is used in the source code
            if (GetNodeText(op, source) == "->")
            {
                // TODO implement PointerMemberAccessExpression in evaluation/stackmachine:
                //      PushEmit(SyntaxKind::PointerMemberAccessExpression), PushVisit(name), PushVisit(expr).
                output = "Pointer member access expression not implemented.";
                return E_NOTIMPL;
            }

            // Post-order: SimpleMemberAccessExpression after name and expr.
            PushEmit(workStack, SyntaxKind::SimpleMemberAccessExpression);
            PushVisit(workStack, name);
            PushVisit(workStack, expr);
            return S_OK;
        }
    },
    // Roslyn: MemberBindingExpression
    {"member_binding_expression",
        [](TSNode node, std::string_view /*source*/, std::list<Opcode> &/*program*/, std::string &/*output*/, WorkStack &workStack) -> HRESULT
        {
            const TSNode name = ts_node_named_child(node, 0);

            PushEmit(workStack, SyntaxKind::MemberBindingExpression);
            PushVisit(workStack, name);
            return S_OK;
        }
    },
    // Roslyn: ElementBindingExpression
    {"element_binding_expression",
        [](TSNode node, std::string_view /*source*/, std::list<Opcode> &/*program*/, std::string &/*output*/, WorkStack &workStack) -> HRESULT
        {
            const TSNode indexList = ts_node_named_child(node, 0);
            const uint32_t argCount = ts_node_named_child_count(indexList);

            PushEmit(workStack, SyntaxKind::ElementBindingExpression, argCount);
            for (uint32_t i = argCount; i > 0; --i)
            {
                PushVisit(workStack, ts_node_named_child(indexList, i - 1));
            }
            return S_OK;
        }
    },
    // Roslyn: ConditionalAccessExpression (e.g., user?.Name or array?[0])
    {"conditional_access_expression",
        [](TSNode node, std::string_view /*source*/, std::list<Opcode> &/*program*/, std::string &/*output*/, WorkStack &workStack) -> HRESULT
        {
            // In Tree-sitter C#, conditional_access_expression always has exactly 3 children:
            // child 0: The base expression (e.g., 'user' or 'array')
            // child 1: The operator token ('?.' or '?[')
            // child 2: The binding expression (member_binding_expression or element_binding_expression)
            const TSNode baseExpr = ts_node_child(node, 0);
            const TSNode binding = ts_node_child(node, 2);

            // FIXME: Binding currently always performs a null check. Emit the conditional jump or
            //        provide a marker in MemberBindingExpression and ElementBindingExpression.

            // Push in reverse: binding (processed second), then baseExpr (processed first).
            PushVisit(workStack, binding);
            PushVisit(workStack, baseExpr);
            return S_OK;
        }
    },
    // Roslyn: InvocationExpression (e.g., Method())
    {"invocation_expression",
        [](TSNode node, std::string_view /*source*/, std::list<Opcode> &/*program*/, std::string &/*output*/, WorkStack &workStack) -> HRESULT
        {
            const TSNode function = ts_node_named_child(node, 0);
            const TSNode argList = ts_node_named_child(node, 1);
            uint32_t argCount = 0;

            if (!ts_node_is_null(argList))
            {
                argCount = ts_node_named_child_count(argList);
            }

            // Post-order: InvocationExpression after the function and its args.
            PushEmit(workStack, SyntaxKind::InvocationExpression, argCount);
            for (uint32_t i = argCount; i > 0; --i)
            {
                PushVisit(workStack, ts_node_named_child(argList, i - 1));
            }
            PushVisit(workStack, function);
            return S_OK;
        }
    },
    // Roslyn: ElementAccessExpression (e.g., array[index])
    {"element_access_expression",
        [](TSNode node, std::string_view /*source*/, std::list<Opcode> &/*program*/, std::string &/*output*/, WorkStack &workStack) -> HRESULT
        {
            const TSNode expression = ts_node_named_child(node, 0);
            const TSNode indexList = ts_node_named_child(node, 1);
            const uint32_t argCount = ts_node_named_child_count(indexList);

            PushEmit(workStack, SyntaxKind::ElementAccessExpression, argCount);
            for (uint32_t i = argCount; i > 0; --i)
            {
                PushVisit(workStack, ts_node_named_child(indexList, i - 1));
            }
            PushVisit(workStack, expression);
            return S_OK;
        }
    },
    // Roslyn Binary Group: Handles Add, Multiply, Subtract, Divide, Modulo, LeftShift, RightShift,
    // BitwiseAnd, BitwiseOr, ExclusiveOr, LogicalAnd, LogicalOr, Equals, NotEquals,
    // GreaterThan, LessThan, GreaterThanOrEqual, LessThanOrEqual, Coalesce Expressions.
    {"binary_expression",
        [](TSNode node, std::string_view source, std::list<Opcode> &/*program*/, std::string &output, WorkStack &workStack) -> HRESULT
        {
            const TSNode left = ts_node_child(node, 0); // Always the first child (index 0)
            const TSNode right = ts_node_child(node, 2); // Always the third child (index 2)
            const std::string_view op = GetNodeText(ts_node_child(node, 1), source); // The operator token (index 1)

            if (op.empty() || op.size() > 4)
            {
                output = "Unknown binary expression: " + std::string(op);
                return E_INVALIDARG;
            }

            // The table is small and fixed, so a linear scan over its 20 keys is faster than
            // hashing and needs neither heap allocation nor a static initialization guard.
            static constexpr std::array<std::pair<uint32_t, SyntaxKind>, 20> opMap{{
                { packString("+"),   SyntaxKind::AddExpression },
                { packString("-"),   SyntaxKind::SubtractExpression },
                { packString("*"),   SyntaxKind::MultiplyExpression },
                { packString("/"),   SyntaxKind::DivideExpression },
                { packString("%"),   SyntaxKind::ModuloExpression },
                { packString("<<"),  SyntaxKind::LeftShiftExpression },
                { packString(">>"),  SyntaxKind::RightShiftExpression },
                { packString(">>>"), SyntaxKind::UnsignedRightShiftExpression },
                { packString("&"),   SyntaxKind::BitwiseAndExpression },
                { packString("|"),   SyntaxKind::BitwiseOrExpression },
                { packString("^"),   SyntaxKind::ExclusiveOrExpression },
                { packString("&&"),  SyntaxKind::LogicalAndExpression },
                { packString("||"),  SyntaxKind::LogicalOrExpression },
                { packString("=="),  SyntaxKind::EqualsExpression },
                { packString("!="),  SyntaxKind::NotEqualsExpression },
                { packString(">"),   SyntaxKind::GreaterThanExpression },
                { packString("<"),   SyntaxKind::LessThanExpression },
                { packString(">="),  SyntaxKind::GreaterThanOrEqualExpression },
                { packString("<="),  SyntaxKind::LessThanOrEqualExpression },
                { packString("??"),  SyntaxKind::CoalesceExpression }
            }};

            const uint32_t key = packString(op);
            const auto findOp = std::find_if(std::cbegin(opMap), std::cend(opMap),
                                             [key](const auto &entry) { return entry.first == key; });
            if (findOp == std::cend(opMap))
            {
                output = "Unknown binary expression: " + std::string(op);
                return E_INVALIDARG;
            }

            // Post-order: the operator after both operands; left is processed first.
            PushEmit(workStack, findOp->second);
            PushVisit(workStack, right);
            PushVisit(workStack, left);
            return S_OK;
        }
    },
    // Roslyn Unary Group: UnaryPlus, UnaryMinus, LogicalNot, BitwiseNot, PreIncrement, PreDecrement
    {"prefix_unary_expression",
        [](TSNode node, std::string_view source, std::list<Opcode> &/*program*/, std::string &output, WorkStack &workStack) -> HRESULT
        {
            const TSNode operand = ts_node_named_child(node, 0);
            const std::string_view op = GetNodeText(ts_node_child(node, 0), source); // The operator is the first child

            SyntaxKind kind{};
            if (op == "+")
            {
                kind = SyntaxKind::UnaryPlusExpression;
            }
            else if (op == "-")
            {
                kind = SyntaxKind::UnaryMinusExpression;
            }
            else if (op == "!")
            {
                kind = SyntaxKind::LogicalNotExpression;
            }
            else if (op == "~")
            {
                kind = SyntaxKind::BitwiseNotExpression;
            }
            else if (op == "++")
            {
                // TODO implement in evaluation/stackmachine: kind = SyntaxKind::PreIncrementExpression;
                output = "Pre-increment expression not implemented.";
                return E_NOTIMPL;
            }
            else if (op == "--")
            {
                // TODO implement in evaluation/stackmachine: kind = SyntaxKind::PreDecrementExpression;
                output = "Pre-decrement expression not implemented.";
                return E_NOTIMPL;
            }
            else
            {
                output = "Unknown prefix unary expression: " + std::string(GetNodeText(node, source));
                return E_INVALIDARG;
            }

            // Post-order: the operator after its operand.
            PushEmit(workStack, kind);
            PushVisit(workStack, operand);
            return S_OK;
        }
    },
    // Roslyn Unary Group: PostIncrement, PostDecrement
    {"postfix_unary_expression",
        [](TSNode node, std::string_view source, std::list<Opcode> &/*program*/, std::string &output, WorkStack &/*workStack*/) -> HRESULT
        {
            // TODO implement in evaluation/stackmachine: PostIncrementExpression / PostDecrementExpression
            //      (PushEmit(kind), PushVisit(operand) with operand = ts_node_named_child(node, 0)).
            const std::string_view op = GetNodeText(ts_node_child(node, 1), source);
            if (op == "++")
            {
                output = "Post-increment expression not implemented.";
                return E_NOTIMPL;
            }
            if (op == "--")
            {
                output = "Post-decrement expression not implemented.";
                return E_NOTIMPL;
            }

            output = "Unknown postfix unary expression: " + std::string(GetNodeText(node, source));
            return E_INVALIDARG;
        }
    },
    // Roslyn: CastExpression (e.g., (int)a or (Object)this)
    {"cast_expression",
        [](TSNode /*node*/, std::string_view /*source*/, std::list<Opcode> &/*program*/, std::string &output, WorkStack &/*workStack*/) -> HRESULT
        {
            // TODO implement in evaluation/stackmachine: child 1 = type node, child 3 = expression node;
            //      PushEmit(SyntaxKind::CastExpression), PushVisit(typeNode), PushVisit(exprNode).
            output = "Cast expression not implemented.";
            return E_NOTIMPL;
        }
    },
    // Roslyn: AsExpression (e.g., a as Foo or this as Foo)
    {"as_expression",
        [](TSNode /*node*/, std::string_view /*source*/, std::list<Opcode> &/*program*/, std::string &output, WorkStack &/*workStack*/) -> HRESULT
        {
            // TODO implement in evaluation/stackmachine: child 0 = left expression, child 2 = type node;
            //      PushEmit(SyntaxKind::AsExpression), PushVisit(typeNode), PushVisit(exprNode).
            output = "'as' expression not implemented.";
            return E_NOTIMPL;
        }
    },
    // Roslyn: IsExpression (e.g., a is Foo or null is Foo)
    {"is_expression",
        [](TSNode /*node*/, std::string_view /*source*/, std::list<Opcode> &/*program*/, std::string &output, WorkStack &/*workStack*/) -> HRESULT
        {
            // TODO implement in evaluation/stackmachine: child 0 = left expression, child 2 = type or pattern node;
            //      PushEmit(SyntaxKind::IsExpression), PushVisit(typeNode), PushVisit(exprNode).
            output = "'is' expression not implemented.";
            return E_NOTIMPL;
        }
    },
    // Roslyn: SizeOfExpression (e.g., sizeof(int))
    {"sizeof_expression",
        [](TSNode node, std::string_view /*source*/, std::list<Opcode> &/*program*/, std::string &/*output*/, WorkStack &workStack) -> HRESULT
        {
            const TSNode typeNode = ts_node_named_child(node, 0);

            PushEmit(workStack, SyntaxKind::SizeOfExpression);
            PushVisit(workStack, typeNode);
            return S_OK;
        }
    },
    // Roslyn: TypeOfExpression (e.g., typeof(List<int>))
    {"typeof_expression",
        [](TSNode /*node*/, std::string_view /*source*/, std::list<Opcode> &/*program*/, std::string &output, WorkStack &/*workStack*/) -> HRESULT
        {
            // TODO implement in evaluation/stackmachine: child 2 = type node;
            //      PushEmit(SyntaxKind::TypeOfExpression), PushVisit(typeNode).
            output = "'typeof' expression not implemented.";
            return E_NOTIMPL;
        }
    },
    // Roslyn: ConditionalExpression (e.g., a ? b : c)
    {"conditional_expression",
        [](TSNode /*node*/, std::string_view /*source*/, std::list<Opcode> &/*program*/, std::string &output, WorkStack &/*workStack*/) -> HRESULT
        {
            // TODO implement in evaluation/stackmachine. In Tree-sitter C# the node has 5 children:
            //      0 = condition, 1 = "?", 2 = true branch, 3 = ":", 4 = false branch.
            output = "Conditional expression not implemented.";
            return E_NOTIMPL;
        }
    },
    // Roslyn: CheckedExpression and UncheckedExpression (e.g., checked(a + b) or unchecked(x * y))
    {"checked_expression",
        [](TSNode /*node*/, std::string_view /*source*/, std::list<Opcode> &/*program*/, std::string &output, WorkStack &/*workStack*/) -> HRESULT
        {
            // TODO implement in evaluation/stackmachine. In Tree-sitter C# the node has 4 children:
            //      0 = "checked"/"unchecked", 1 = "(", 2 = inner expression, 3 = ")".
            output = "Checked and Unchecked expressions not implemented.";
            return E_NOTIMPL;
        }
    },
    // Roslyn: ParenthesizedExpression (e.g., "(a + b)")
    {"parenthesized_expression",
        [](TSNode node, std::string_view /*source*/, std::list<Opcode> &/*program*/, std::string &/*output*/, WorkStack &workStack) -> HRESULT
        {
            // child 0: "(", child 1: core inner expression node, child 2: ")"
            // Descend directly into the parenthesized expression.
            // The tree shape already preserves operator precedence.
            PushVisit(workStack, ts_node_child(node, 1));
            return S_OK;
        }
    },
    // Roslyn: Argument (Wraps modifiers like 'ref', 'out', 'in' or named args like 'x:')
    {"argument",
        [](TSNode node, std::string_view source, std::list<Opcode> &/*program*/, std::string &output, WorkStack &workStack) -> HRESULT
        {
            const uint32_t childCount = ts_node_child_count(node);
            const TSNode exprNode = ts_node_child(node, childCount - 1); // The core value expression is always last

            // Scan for ref, out, and in modifiers to determine how the debugger passes the argument.
            for (uint32_t i = 0; i < childCount - 1; ++i)
            {
                const std::string_view txt = GetNodeText(ts_node_child(node, i), source);
                if (txt == "ref" || txt == "out" || txt == "in")
                {
                    // TODO implement ref/out/in modifiers
                    output = "ref/out/in modifiers not implemented.";
                    return E_NOTIMPL;
                }
                else
                {
                    output = "Unknown argument modifier: " + std::string(txt);
                    return E_INVALIDARG;
                }
            }

            PushVisit(workStack, exprNode);
            return S_OK;
        }
    },
    // Roslyn: SimpleLambdaExpression and ParenthesizedLambdaExpression
    {"lambda_expression",
        [](TSNode /*node*/, std::string_view /*source*/, std::list<Opcode> &/*program*/, std::string &output, WorkStack &/*workStack*/) -> HRESULT
        {
            output = "Lambda expressions not implemented.";
            return E_NOTIMPL;
        }
    },
    // Roslyn Assignment Group: =, +=, -=, /=, *=, %=, &=, |=, ^=, <<=, >>=, >>>=, ??=
    {"assignment_expression",
        [](TSNode /*node*/, std::string_view /*source*/, std::list<Opcode> &/*program*/, std::string &output, WorkStack &/*workStack*/) -> HRESULT
        {
            // Roslyn kinds to add: SimpleAssignmentExpression, AddAssignmentExpression, SubtractAssignmentExpression,
            // MultiplyAssignmentExpression, DivideAssignmentExpression, ModuloAssignmentExpression,
            // AndAssignmentExpression, OrAssignmentExpression, ExclusiveOrAssignmentExpression,
            // LeftShiftAssignmentExpression, RightShiftAssignmentExpression,
            // UnsignedRightShiftAssignmentExpression, CoalesceAssignmentExpression.
            output = "Assignment expressions not implemented.";
            return E_NOTIMPL;
        }
    },
    // Roslyn: QueryExpression, LINQ query expression
    {"query_expression",
        [](TSNode /*node*/, std::string_view /*source*/, std::list<Opcode> &/*program*/, std::string &output, WorkStack &/*workStack*/) -> HRESULT
        {
            // Roslyn kinds to add: query_body (QueryBody), from_clause (FromClause), where_clause (WhereClause),
            // select_clause (SelectClause), group_clause (GroupClause), order_by_clause (OrderByClause),
            // ordering (Ordering), join_clause (JoinClause / JoinIntoClause), let_clause (LetClause).
            output = "LINQ query expression not implemented.";
            return E_NOTIMPL;
        }
    }};

    try
    {
        WorkStack workStack;
        workStack.reserve(initialWorkStackCapacity);
        PushVisit(workStack, startNode);

        while (!workStack.empty())
        {
            const WorkItem item = workStack.back();
            workStack.pop_back();

            // Execute deferred opcode emission.
            if (item.type == WorkType::Emit)
            {
                program.emplace_back(item.kind, item.count);
                continue;
            }
            if (item.type == WorkType::EmitText)
            {
                program.emplace_back(item.kind, std::string(GetNodeText(item.node, source)), item.count);
                continue;
            }

            // Visit path: dispatch through the handler map.
            const TSNode node = item.node;
            if (ts_node_is_null(node) ||
                // Skip zero-width recovery nodes that aren't explicit errors.
                ts_node_start_byte(node) == ts_node_end_byte(node))
            {
                continue;
            }

            const std::string_view type = ts_node_type(node);
            const auto findHandler = syntaxKindHandlerMap.find(type);
            if (findHandler == syntaxKindHandlerMap.cend())
            {
                output = "Failed to parse node type: '" + std::string(type) + "' expression: '" +
                         std::string(GetNodeText(node, source)) + "'";
                return E_INVALIDARG;
            }

            const HRESULT hr = findHandler->second(node, source, program, output, workStack);
            if (FAILED(hr))
            {
                return hr;
            }
        }
    }
    catch (const std::bad_alloc &)
    {
        output = "Failed to allocate memory while generating expression program.";
        return E_OUTOFMEMORY;
    }
    catch (const std::exception &ex)
    {
        output = "Unhandled exception while generating expression program: " + std::string(ex.what());
        return E_FAIL;
    }
    catch (...)
    {
        output = "Unknown exception while generating expression program.";
        return E_FAIL;
    }

    return S_OK;
}

} // unnamed namespace

HRESULT GenerateProgram(std::string_view expression, std::list<Opcode> &program, std::string &output)
{
    TSParser *parser = GetThreadParser();
    if (parser == nullptr)
    {
        output = "Failed to initialize the C# expression parser.";
        return E_FAIL;
    }

    // Trim trailing whitespace from the input expression.
    size_t length = expression.size();
    while (length > 0 && std::isspace(static_cast<unsigned char>(expression.at(length - 1))) != 0)
    {
        --length;
    }
    const std::string_view trimmedExpression = expression.substr(0, length);

    constexpr std::string_view prefix = "class W{void M(){_ = ";
    constexpr std::string_view suffix = ";}}";

    // Build the full compilation unit with a single allocation.
    std::string fullSource;
    fullSource.reserve(prefix.size() + trimmedExpression.size() + suffix.size());
    fullSource.append(prefix).append(trimmedExpression).append(suffix);

    const auto startByte = static_cast<uint32_t>(prefix.size());
    const uint32_t endByte = startByte + static_cast<uint32_t>(trimmedExpression.size());

    const std::unique_ptr<TSTree, decltype(&ts_tree_delete)> tree(
        ts_parser_parse_string(parser, nullptr, fullSource.data(), static_cast<uint32_t>(fullSource.size())),
        &ts_tree_delete);
    if (!tree)
    {
        output = "Failed to parse expression.";
        return E_FAIL;
    }

    const TSNode rootNode = ts_tree_root_node(tree.get());

    // ts_node_has_error also detects hidden missing tokens and malformed parts deeper in the tree.
    if (ts_node_has_error(rootNode))
    {
        output = "Expression has malformed or incomplete syntax.";
        return E_INVALIDARG; // Stop here and skip the generation steps
    }

    // The tree is valid, so locate the expression node.
    const TSNode exprNode = ts_node_descendant_for_byte_range(rootNode, startByte, endByte);

    const HRESULT status = GenerateExecutionSteps(exprNode, fullSource, program, output);
    if (FAILED(status))
    {
        program.clear();
    }
    return status;
}

} // namespace dncdbg::Parser
