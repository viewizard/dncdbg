// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "expressionparser/parser.h"
#include <algorithm>
#include <cassert>
#include <exception>
#include <functional>
#include <new>
#include <stack>
#include <unordered_map>
#include <utility>
#include <variant>
#include <tree_sitter/api.h>
extern "C" const TSLanguage *tree_sitter_c_sharp();

namespace dncdbg::Parser
{

namespace
{

// Helper to get raw source text belonging to a specific node
std::string_view GetNodeText(TSNode node, const std::string &source)
{
    if (ts_node_is_null(node))
    {
        return "";
    }

    const uint32_t start = ts_node_start_byte(node);
    const uint32_t end = ts_node_end_byte(node);
    return std::string_view(source).substr(start, end - start);
}

// Pack 1 or 2 character string into uint16_t for use as map key.
// First character goes in low byte, second character (if present) in high byte.
constexpr uint16_t packString(std::string_view str)
{
    if (str.empty() || str.size() > 2)
    {
        assert(false && "String must be 1 or 2 characters only");
    }

    uint16_t result = static_cast<uint8_t>(str.at(0));
    if (str.size() == 2)
    {
        constexpr uint8_t bitsInByte = 8;
        result |= (static_cast<uint16_t>(static_cast<uint8_t>(str.at(1))) << bitsInByte);
    }

    return result;
}

// The traversal stack holds two kinds of work:
//   VisitNode  – a TSNode that must be dispatched through the handler map.
//   EmitAction – a deferred callback that emits opcodes *after* the node's
//                children have been fully processed (post-order work).
struct VisitNode
{
    TSNode node;
};
struct EmitAction
{
    std::function<HRESULT(std::list<Opcode> &, std::string &)> action;
};

using WorkItem = std::variant<VisitNode, EmitAction>;
using SyntaxKindHandler = std::function<HRESULT(TSNode node,
                                                const std::string &source,
                                                std::list<Opcode> &program,
                                                std::string &output,
                                                std::stack<WorkItem> &workStack)>;

HRESULT GenerateExecutionSteps(TSNode rootNode, const std::string &source, std::list<Opcode> &program, std::string &output)
{
    auto StringLiteralKindHandler = [](TSNode node, const std::string &source, std::list<Opcode> &program, std::string &/*output*/, std::stack<WorkItem> &/*workStack*/) -> HRESULT
    {
        std::string_view str = GetNodeText(node, source);
        if (str.size() >= 2 && str.front() == '"' && str.back() == '"')
        {
            str.remove_prefix(1);
            str.remove_suffix(1);
        }
        program.emplace_back(SyntaxKind::StringLiteralExpression, std::string(str));
        return S_OK;
    };

    static const std::unordered_map<std::string_view, SyntaxKindHandler> syntaxKindHandlerMap{
    // Roslyn: NullLiteralExpression
    {"null_literal",
        [](TSNode /*node*/, const std::string &/*source*/, std::list<Opcode> &program, std::string &/*output*/, std::stack<WorkItem> &/*workStack*/) -> HRESULT
        {
            program.emplace_back(SyntaxKind::NullLiteralExpression);
            return S_OK;
        }
    },
    // Roslyn: ThisExpression
    {"this",
        [](TSNode /*node*/, const std::string &/*source*/, std::list<Opcode> &program, std::string &/*output*/, std::stack<WorkItem> &/*workStack*/) -> HRESULT
        {
            program.emplace_back(SyntaxKind::ThisExpression);
            return S_OK;
        }
    },
    // Roslyn: TrueLiteralExpression (one word expression: "true")
    {"true",
        [](TSNode /*node*/, const std::string &/*source*/, std::list<Opcode> &program, std::string &/*output*/, std::stack<WorkItem> &/*workStack*/) -> HRESULT
        {
            program.emplace_back(SyntaxKind::TrueLiteralExpression);
            return S_OK;
        }
    },
    // Roslyn: FalseLiteralExpression (one word expression: "false")
    {"false",
        [](TSNode /*node*/, const std::string &/*source*/, std::list<Opcode> &program, std::string &/*output*/, std::stack<WorkItem> &/*workStack*/) -> HRESULT
        {
            program.emplace_back(SyntaxKind::FalseLiteralExpression);
            return S_OK;
        }
    },
    // Roslyn: TrueLiteralExpression and FalseLiteralExpression (complex expression)
    {"boolean_literal",
        [](TSNode node, const std::string &source, std::list<Opcode> &program, std::string &output, std::stack<WorkItem> &/*workStack*/) -> HRESULT
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
        [](TSNode node, const std::string &source, std::list<Opcode> &program, std::string &/*output*/, std::stack<WorkItem> &/*workStack*/) -> HRESULT
        {
            const std::string_view rawText = GetNodeText(node, source);
            program.emplace_back(SyntaxKind::IdentifierName, std::string(rawText));
            return S_OK;
        }
    },
    // Roslyn: GenericName (e.g., List<int>)
    {"generic_name",
        [](TSNode node, const std::string &source, std::list<Opcode> &/*program*/, std::string &/*output*/, std::stack<WorkItem> &workStack) -> HRESULT
        {
            const TSNode nameNode = ts_node_named_child(node, 0);
            const TSNode typeArgs = ts_node_named_child(node, 1);
            const uint32_t count = ts_node_named_child_count(typeArgs);

            // Capture name text now; emit GenericName opcode after all type-arg children.
            const std::string nameText(GetNodeText(nameNode, source));

            // Push in reverse: first the post-action, then children (last-to-first).
            workStack.emplace(EmitAction{[nameText, count](std::list<Opcode> &prog, std::string &/*out*/) -> HRESULT // NOLINT(bugprone-exception-escape) handled by try-catch in code below
            {
                prog.emplace_back(SyntaxKind::GenericName, nameText, count);
                return S_OK;
            }});
            for (uint32_t i = count; i > 0; --i)
            {
                workStack.emplace(VisitNode{ts_node_named_child(typeArgs, i - 1)});
            }
            return S_OK;
        }
    },
    // Roslyn: QualifiedName (e.g., System.Collections)
    {"qualified_name",
        [](TSNode node, const std::string &/*source*/, std::list<Opcode> &/*program*/, std::string &/*output*/, std::stack<WorkItem> &workStack) -> HRESULT
        {
            const TSNode left = ts_node_named_child(node, 0);
            const TSNode right = ts_node_named_child(node, 1);

            // Push in reverse: post-action, then right, then left.
            workStack.emplace(EmitAction{[](std::list<Opcode> &prog, std::string &/*out*/) -> HRESULT
            {
                prog.emplace_back(SyntaxKind::QualifiedName);
                return S_OK;
            }});
            workStack.emplace(VisitNode{right});
            workStack.emplace(VisitNode{left});
            return S_OK;
        }
    },
    // Roslyn: AliasQualifiedName (e.g., global::System)
    {"alias_qualified_name",
        [](TSNode /*node*/, const std::string &/*source*/, std::list<Opcode> &/*program*/, std::string &output, std::stack<WorkItem> &/*workStack*/) -> HRESULT
        {
            // TODO implement in evalstackmachine.cpp before uncommenting this code

            // TSNode alias = ts_node_named_child(node, 0);
            // TSNode name = ts_node_named_child(node, 1);
            // "PUSH_ALIAS(" + std::string(GetNodeText(alias, source)) + ")"
            // workStack.emplace(VisitNode{name});
            // Note: post-action "RESOLVE_ALIAS_NAME" would be pushed before the child visit.

            output = "Alias qualified name not implemented.";
            return E_NOTIMPL;
        }
    },
    // Roslyn: NumericLiteralExpression
    {"integer_literal",
        [](TSNode node, const std::string &source, std::list<Opcode> &program, std::string &/*output*/, std::stack<WorkItem> &/*workStack*/) -> HRESULT
        {
            const std::string_view rawText = GetNodeText(node, source);
            program.emplace_back(SyntaxKind::NumericLiteralExpression, std::string(rawText), uint32_t{0});
            return S_OK;
        }
    },
    // Roslyn: NumericLiteralExpression
    {"real_literal",
        [](TSNode node, const std::string &source, std::list<Opcode> &program, std::string &/*output*/, std::stack<WorkItem> &/*workStack*/) -> HRESULT
        {
            const std::string_view rawText = GetNodeText(node, source);
            program.emplace_back(SyntaxKind::NumericLiteralExpression, std::string(rawText), uint32_t{1});
            return S_OK;
        }
    },
    // Roslyn: StringLiteralExpression
    {"string_literal", StringLiteralKindHandler},
    // Roslyn: StringLiteralExpression
    {"verbatim_string_literal", StringLiteralKindHandler},
    // Roslyn: CharacterLiteralExpression
    {"character_literal",
        [](TSNode node, const std::string &source, std::list<Opcode> &program, std::string &/*output*/, std::stack<WorkItem> &/*workStack*/) -> HRESULT
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
        [](TSNode node, const std::string &source, std::list<Opcode> &program, std::string &/*output*/, std::stack<WorkItem> &/*workStack*/) -> HRESULT
        {
            const std::string_view rawText = GetNodeText(node, source);
            program.emplace_back(SyntaxKind::PredefinedType, std::string(rawText));
            return S_OK;
        }
    },
    // Roslyn: ObjectCreationExpression (e.g., new Foo())
    {"object_creation_expression",
        [](TSNode /*node*/, const std::string &/*source*/, std::list<Opcode> &/*program*/, std::string &output, std::stack<WorkItem> &/*workStack*/) -> HRESULT
        {
            // TODO implement in evalstackmachine.cpp before uncommenting this code

            // const TSNode typeNode = ts_node_named_child(node, 0);
            // const TSNode argList = ts_node_named_child(node, 1);
            // uint32_t argCount = 0;
            // if (!ts_node_is_null(argList) && std::string_view(ts_node_type(argList)) == "argument_list")
            // {
            //     argCount = ts_node_named_child_count(argList);
            //     // Push in reverse: post-action first, then arg children (last-to-first), then typeNode.
            //     workStack.emplace(EmitAction{[argCount](std::list<Opcode> &prog, std::string &/*out*/) -> HRESULT
            //     {
            //         prog.emplace_back(SyntaxKind::ObjectCreationExpression, argCount);
            //         return S_OK;
            //     }});
            //     workStack.emplace(VisitNode{typeNode});
            //     for (uint32_t i = argCount; i > 0; --i)
            //     {
            //         workStack.emplace(VisitNode{ts_node_named_child(argList, i - 1)});
            //     }
            // }
            // else
            // {
            //     workStack.emplace(EmitAction{[](std::list<Opcode> &prog, std::string &/*out*/) -> HRESULT
            //     {
            //         prog.emplace_back(SyntaxKind::ObjectCreationExpression, uint32_t{0});
            //         return S_OK;
            //     }});
            //     workStack.emplace(VisitNode{typeNode});
            // }
            // return S_OK;

            output = "Object creation expression not implemented.";
            return E_NOTIMPL;
        }
    },
    // Roslyn: SimpleMemberAccessExpression (a.b) AND PointerMemberAccessExpression (a->b)
    {"member_access_expression",
        [](TSNode node, const std::string &source, std::list<Opcode> &/*program*/, std::string &output, std::stack<WorkItem> &workStack) -> HRESULT
        {
            const TSNode expr = ts_node_child(node, 0);
            const TSNode op = ts_node_child(node, 1); // The operator token (. or ->)
            const TSNode name = ts_node_child(node, 2);

            // Check what kind of separator is used in the source code
            const std::string_view opText = GetNodeText(op, source);
            if (opText == "->")
            {
                // TODO implement in evalstackmachine.cpp before uncommenting this code
                // workStack.emplace(EmitAction{[](std::list<Opcode> &prog, std::string &/*out*/) -> HRESULT
                // {
                //     prog.emplace_back(SyntaxKind::PointerMemberAccessExpression);
                //     return S_OK;
                // }});
                // workStack.emplace(VisitNode{name});
                // workStack.emplace(VisitNode{expr});
                // return S_OK;
                output = "Pointer member access expression not implemented.";
                return E_NOTIMPL;
            }

            // Push in reverse: post-action, then name, then expr.
            workStack.emplace(EmitAction{[](std::list<Opcode> &prog, std::string &/*out*/) -> HRESULT
            {
                prog.emplace_back(SyntaxKind::SimpleMemberAccessExpression);
                return S_OK;
            }});
            workStack.emplace(VisitNode{name});
            workStack.emplace(VisitNode{expr});
            return S_OK;
        }
    },
    // Roslyn: MemberBindingExpression
    {"member_binding_expression",
        [](TSNode node, const std::string &/*source*/, std::list<Opcode> &/*program*/, std::string &/*output*/, std::stack<WorkItem> &workStack) -> HRESULT
        {
            const TSNode name = ts_node_named_child(node, 0);

            // Push in reverse: post-action, then child.
            workStack.emplace(EmitAction{[](std::list<Opcode> &prog, std::string &/*out*/) -> HRESULT
            {
                prog.emplace_back(SyntaxKind::MemberBindingExpression);
                return S_OK;
            }});
            workStack.emplace(VisitNode{name});
            return S_OK;
        }
    },
    // Roslyn: ElementBindingExpression
    {"element_binding_expression",
        [](TSNode node, const std::string &/*source*/, std::list<Opcode> &/*program*/, std::string &/*output*/, std::stack<WorkItem> &workStack) -> HRESULT
        {
            const TSNode indexList = ts_node_named_child(node, 0);
            const uint32_t argCount = ts_node_named_child_count(indexList);

            // Push in reverse: post-action, then index children (last-to-first).
            workStack.emplace(EmitAction{[argCount](std::list<Opcode> &prog, std::string &/*out*/) -> HRESULT
            {
                prog.emplace_back(SyntaxKind::ElementBindingExpression, argCount);
                return S_OK;
            }});
            for (uint32_t i = argCount; i > 0; --i)
            {
                workStack.emplace(VisitNode{ts_node_named_child(indexList, i - 1)});
            }
            return S_OK;
        }
    },
    // Roslyn: ConditionalAccessExpression (e.g., user?.Name or array?[0])
    {"conditional_access_expression",
        [](TSNode node, const std::string &/*source*/, std::list<Opcode> &/*program*/, std::string &/*output*/, std::stack<WorkItem> &workStack) -> HRESULT
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
            workStack.emplace(VisitNode{binding});
            workStack.emplace(VisitNode{baseExpr});
            return S_OK;
        }
    },
    // Roslyn: InvocationExpression (e.g., Method())
    {"invocation_expression",
        [](TSNode node, const std::string &/*source*/, std::list<Opcode> &/*program*/, std::string &/*output*/, std::stack<WorkItem> &workStack) -> HRESULT
        {
            const TSNode function = ts_node_named_child(node, 0);
            const TSNode argList = ts_node_named_child(node, 1);
            uint32_t argCount = 0;

            if (!ts_node_is_null(argList))
            {
                argCount = ts_node_named_child_count(argList);
            }

            // Push in reverse: post-action, then arg children (last-to-first), then function.
            workStack.emplace(EmitAction{[argCount](std::list<Opcode> &prog, std::string &/*out*/) -> HRESULT
            {
                prog.emplace_back(SyntaxKind::InvocationExpression, argCount);
                return S_OK;
            }});
            for (uint32_t i = argCount; i > 0; --i)
            {
                workStack.emplace(VisitNode{ts_node_named_child(argList, i - 1)});
            }
            workStack.emplace(VisitNode{function});
            return S_OK;
        }
    },
    // Roslyn: ElementAccessExpression (e.g., array[index])
    {"element_access_expression",
        [](TSNode node, const std::string &/*source*/, std::list<Opcode> &/*program*/, std::string &/*output*/, std::stack<WorkItem> &workStack) -> HRESULT
        {
            const TSNode expression = ts_node_named_child(node, 0);
            const TSNode indexList = ts_node_named_child(node, 1);
            const uint32_t argCount = ts_node_named_child_count(indexList);

            // Push in reverse: post-action, then index children (last-to-first), then expression.
            workStack.emplace(EmitAction{[argCount](std::list<Opcode> &prog, std::string &/*out*/) -> HRESULT
            {
                prog.emplace_back(SyntaxKind::ElementAccessExpression, argCount);
                return S_OK;
            }});
            for (uint32_t i = argCount; i > 0; --i)
            {
                workStack.emplace(VisitNode{ts_node_named_child(indexList, i - 1)});
            }
            workStack.emplace(VisitNode{expression});
            return S_OK;
        }
    },
    // Roslyn Binary Group: Handles Add, Multiply, Subtract, Divide, Modulo, LeftShift, RightShift,
    // BitwiseAnd, BitwiseOr, ExclusiveOr, LogicalAnd, LogicalOr, Equals, NotEquals,
    // GreaterThan, LessThan, GreaterThanOrEqual, LessThanOrEqual, Coalesce Expressions.
    {"binary_expression",
        [](TSNode node, const std::string &source, std::list<Opcode> &/*program*/, std::string &output, std::stack<WorkItem> &workStack) -> HRESULT
        {
            const TSNode left = ts_node_child(node, 0); // Always the first child (index 0)
            const TSNode right = ts_node_child(node, 2); // Always the third child (index 2)
            const std::string_view op = GetNodeText(ts_node_child(node, 1), source); // Operator is index 1

            if (op.empty() || op.size() > 2)
            {
                output = "Unknown binary expression: " + std::string(op);
                return E_INVALIDARG;
            }

            static const std::unordered_map<uint16_t, SyntaxKind> opMap{
                { packString("+"),  SyntaxKind::AddExpression },
                { packString("-"),  SyntaxKind::SubtractExpression },
                { packString("*"),  SyntaxKind::MultiplyExpression },
                { packString("/"),  SyntaxKind::DivideExpression },
                { packString("%"),  SyntaxKind::ModuloExpression },
                { packString("<<"), SyntaxKind::LeftShiftExpression },
                { packString(">>"), SyntaxKind::RightShiftExpression },
                { packString("&"),  SyntaxKind::BitwiseAndExpression },
                { packString("|"),  SyntaxKind::BitwiseOrExpression },
                { packString("^"),  SyntaxKind::ExclusiveOrExpression },
                { packString("&&"), SyntaxKind::LogicalAndExpression },
                { packString("||"), SyntaxKind::LogicalOrExpression },
                { packString("=="), SyntaxKind::EqualsExpression },
                { packString("!="), SyntaxKind::NotEqualsExpression },
                { packString(">"),  SyntaxKind::GreaterThanExpression },
                { packString("<"),  SyntaxKind::LessThanExpression },
                { packString(">="), SyntaxKind::GreaterThanOrEqualExpression },
                { packString("<="), SyntaxKind::LessThanOrEqualExpression },
                { packString("??"), SyntaxKind::CoalesceExpression }
            };

            const auto findOp = opMap.find(packString(op));
            if (findOp == opMap.end())
            {
                output = "Unknown binary expression: " + std::string(op);
                return E_INVALIDARG;
            }

            const SyntaxKind kind = findOp->second;

            // Push in reverse: post-action, then right, then left.
            workStack.emplace(EmitAction{[kind](std::list<Opcode> &prog, std::string &/*out*/) -> HRESULT
            {
                prog.emplace_back(kind);
                return S_OK;
            }});
            workStack.emplace(VisitNode{right});
            workStack.emplace(VisitNode{left});
            return S_OK;
        }
    },
    // Roslyn Unary Group: UnaryPlus, UnaryMinus, LogicalNot, BitwiseNot, PreIncrement, PreDecrement
    {"prefix_unary_expression",
        [](TSNode node, const std::string &source, std::list<Opcode> &/*program*/, std::string &output, std::stack<WorkItem> &workStack) -> HRESULT
        {
            const TSNode operand = ts_node_named_child(node, 0);
            const std::string op = std::string(GetNodeText(ts_node_child(node, 0), source)); // Operator is first child

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
                // TODO implement in evalstackmachine.cpp before uncommenting this code
                // kind = SyntaxKind::PreIncrementExpression;
                output = "Pre-increment expression not implemented.";
                return E_NOTIMPL;
            }
            else if (op == "--")
            {
                // TODO implement in evalstackmachine.cpp before uncommenting this code
                // kind = SyntaxKind::PreDecrementExpression;
                output = "Pre-decrement expression not implemented.";
                return E_NOTIMPL;
            }
            else
            {
                const std::string_view rawText = GetNodeText(node, source);
                output = "Unknown prefix unary expression: " + std::string(rawText);
                return E_INVALIDARG;
            }

            // Push in reverse: post-action, then operand.
            workStack.emplace(EmitAction{[kind](std::list<Opcode> &prog, std::string &/*out*/) -> HRESULT
            {
                prog.emplace_back(kind);
                return S_OK;
            }});
            workStack.emplace(VisitNode{operand});
            return S_OK;
        }
    },
    // Roslyn Unary Group: PostIncrement, PostDecrement
    {"postfix_unary_expression",
        [](TSNode node, const std::string &source, std::list<Opcode> &/*program*/, std::string &output, std::stack<WorkItem> &/*workStack*/) -> HRESULT
        {
            // TODO implement in evalstackmachine.cpp before uncommenting this code

            // const TSNode operand = ts_node_named_child(node, 0);
            const std::string op = std::string(GetNodeText(ts_node_child(node, 1), source));

            // SyntaxKind kind{};
            if (op == "++")
            {
                // kind = SyntaxKind::PostIncrementExpression;
                output = "Post-increment expression not implemented.";
                return E_NOTIMPL;
            }
            else if (op == "--")
            {
                // kind = SyntaxKind::PostDecrementExpression;
                output = "Post-decrement expression not implemented.";
                return E_NOTIMPL;
            }
            else
            {
                const std::string_view rawText = GetNodeText(node, source);
                output = "Unknown postfix unary expression: " + std::string(rawText);
                return E_INVALIDARG;
            }

            // Push in reverse: post-action, then operand.
            // workStack.emplace(EmitAction{[kind](std::list<Opcode> &prog, std::string &/*out*/) -> HRESULT
            // {
            //     prog.emplace_back(kind);
            //     return S_OK;
            // }});
            // workStack.emplace(VisitNode{operand});
            // return S_OK;
        }
    },
    // Roslyn: CastExpression (e.g., (int)a or (Object)this)
    {"cast_expression",
        [](TSNode /*node*/, const std::string &/*source*/, std::list<Opcode> &/*program*/, std::string &output, std::stack<WorkItem> &/*workStack*/) -> HRESULT
        {
            // TODO implement in evalstackmachine.cpp before uncommenting this code

            // In Tree-sitter C#, cast_expression structure:
            // child 0: "("
            // child 1: type node
            // child 2: ")"
            // child 3: target expression node (can be anonymous)
            // const TSNode typeNode = ts_node_child(node, 1);
            // const TSNode exprNode = ts_node_child(node, 3);

            // workStack.emplace(EmitAction{[](std::list<Opcode> &prog, std::string &/*out*/) -> HRESULT
            // {
            //     prog.emplace_back(SyntaxKind::CastExpression);
            //     return S_OK;
            // }});
            // workStack.emplace(VisitNode{typeNode});
            // workStack.emplace(VisitNode{exprNode});
            // return S_OK;

            output = "Cast expression not implemented.";
            return E_NOTIMPL;
        }
    },
    // Roslyn: AsExpression (e.g., a as Foo or this as Foo)
    {"as_expression",
        [](TSNode /*node*/, const std::string &/*source*/, std::list<Opcode> &/*program*/, std::string &output, std::stack<WorkItem> &/*workStack*/) -> HRESULT
        {
            // TODO implement in evalstackmachine.cpp before uncommenting this code

            // child 0: left expression node (can be anonymous)
            // child 1: "as" keyword
            // child 2: type node
            // const TSNode exprNode = ts_node_child(node, 0);
            // const TSNode typeNode = ts_node_child(node, 2);

            // workStack.emplace(EmitAction{[](std::list<Opcode> &prog, std::string &/*out*/) -> HRESULT
            // {
            //     prog.emplace_back(SyntaxKind::AsExpression);
            //     return S_OK;
            // }});
            // workStack.emplace(VisitNode{typeNode});
            // workStack.emplace(VisitNode{exprNode});
            // return S_OK;

            output = "'as' expression not implemented.";
            return E_NOTIMPL;
        }
    },
    // Roslyn: IsExpression (e.g., a is Foo or null is Foo)
    {"is_expression",
        [](TSNode /*node*/, const std::string &/*source*/, std::list<Opcode> &/*program*/, std::string &output, std::stack<WorkItem> &/*workStack*/) -> HRESULT
        {
            // TODO implement in evalstackmachine.cpp before uncommenting this code

            // child 0: left expression node (can be anonymous)
            // child 1: "is" keyword
            // child 2: type or pattern node
            // const TSNode exprNode = ts_node_child(node, 0);
            // const TSNode typeNode = ts_node_child(node, 2);

            // workStack.emplace(EmitAction{[](std::list<Opcode> &prog, std::string &/*out*/) -> HRESULT
            // {
            //     prog.emplace_back(SyntaxKind::IsExpression);
            //     return S_OK;
            // }});
            // workStack.emplace(VisitNode{typeNode});
            // workStack.emplace(VisitNode{exprNode});
            // return S_OK;

            output = "'is' expression not implemented.";
            return E_NOTIMPL;
        }
    },
    // Roslyn: SizeOfExpression (e.g., sizeof(int))
    {"sizeof_expression",
        [](TSNode node, const std::string &/*source*/, std::list<Opcode> &/*program*/, std::string &/*output*/, std::stack<WorkItem> &workStack) -> HRESULT
        {
            const TSNode typeNode = ts_node_named_child(node, 0);

            // Push in reverse: post-action, then child.
            workStack.emplace(EmitAction{[](std::list<Opcode> &prog, std::string &/*out*/) -> HRESULT
            {
                prog.emplace_back(SyntaxKind::SizeOfExpression);
                return S_OK;
            }});
            workStack.emplace(VisitNode{typeNode});
            return S_OK;
        }
    },
    // Roslyn: TypeOfExpression (e.g., typeof(List<int>))
    {"typeof_expression",
        [](TSNode /*node*/, const std::string &/*source*/, std::list<Opcode> &/*program*/, std::string &output, std::stack<WorkItem> &/*workStack*/) -> HRESULT
        {
            // TODO implement in evalstackmachine.cpp before uncommenting this code

            // child 0: "typeof", child 1: "(", child 2: type node, child 3: ")"
            // const TSNode typeNode = ts_node_child(node, 2);

            // workStack.emplace(EmitAction{[](std::list<Opcode> &prog, std::string &/*out*/) -> HRESULT
            // {
            //     prog.emplace_back(SyntaxKind::TypeOfExpression);
            //     return S_OK;
            // }});
            // workStack.emplace(VisitNode{typeNode});
            // return S_OK;

            output = "'typeof' expression not implemented.";
            return E_NOTIMPL;
        }
    },
    // Roslyn: ConditionalExpression (e.g., a ? b : c)
    {"conditional_expression",
        [](TSNode /*node*/, const std::string &/*source*/, std::list<Opcode> &/*program*/, std::string &output, std::stack<WorkItem> &/*workStack*/) -> HRESULT
        {
            // TODO implement in evalstackmachine.cpp before uncommenting this code

            // In Tree-sitter C#, a conditional expression has exactly 5 children:
            // index 0: condition node
            // index 1: "?" token
            // index 2: true branch node (can be anonymous like null or this)
            // index 3: ":" token
            // index 4: false branch node (can be anonymous like null or this)
            // const TSNode condition = ts_node_child(node, 0);
            // const TSNode conTrue = ts_node_child(node, 2);
            // const TSNode conFalse = ts_node_child(node, 4);

            // Push in reverse execution order:
            // "MARK_END"
            // workStack.emplace(EmitAction{[](std::list<Opcode> &/*prog*/, std::string &/*out*/) -> HRESULT
            // {
            //     // "MARK_END"
            //     return S_OK;
            // }});
            // workStack.emplace(VisitNode{conFalse});
            // workStack.emplace(EmitAction{[](std::list<Opcode> &/*prog*/, std::string &/*out*/) -> HRESULT
            // {
            //     // "JUMP_TO_END"
            //     return S_OK;
            // }});
            // workStack.emplace(VisitNode{conTrue});
            // workStack.emplace(EmitAction{[](std::list<Opcode> &/*prog*/, std::string &/*out*/) -> HRESULT
            // {
            //     // "JUMP_IF_FALSE_TO_ELSE"
            //     return S_OK;
            // }});
            // workStack.emplace(VisitNode{condition});
            // return S_OK;

            output = "Conditional expression not implemented.";
            return E_NOTIMPL;
        }
    },
    // Roslyn: CheckedExpression AND UncheckedExpression (e.g., checked(a + b) or unchecked(x * y))
    {"checked_expression",
        [](TSNode /*node*/, const std::string &/*source*/, std::list<Opcode> &/*program*/, std::string &output, std::stack<WorkItem> &/*workStack*/) -> HRESULT
        {
            // TODO implement in evalstackmachine.cpp before uncommenting this code

            // In Tree-sitter C#, checked_expression has exactly 4 children:
            // child 0: "checked" or "unchecked" keyword token
            // child 1: "(" token
            // child 2: the inner expression node being guarded
            // child 3: ")" token
            // const TSNode keywordNode = ts_node_child(node, 0);
            // const TSNode innerExpr = ts_node_child(node, 2);

            // const std::string_view keyword = GetNodeText(keywordNode, source);

            // if (keyword == "checked")
            // {
            //     // Push in reverse: restore, then inner expr, then set.
            //     workStack.emplace(EmitAction{[](std::list<Opcode> &/*prog*/, std::string &/*out*/) -> HRESULT
            //     {
            //         // "RESTORE_ARITHMETIC_CHECKED"
            //         return S_OK;
            //     }});
            //     workStack.emplace(VisitNode{innerExpr});
            //     workStack.emplace(EmitAction{[](std::list<Opcode> &/*prog*/, std::string &/*out*/) -> HRESULT
            //     {
            //         // "SET_ARITHMETIC_CHECKED(true)"
            //         return S_OK;
            //     }});
            // }
            // else
            // {
            //     workStack.emplace(EmitAction{[](std::list<Opcode> &/*prog*/, std::string &/*out*/) -> HRESULT
            //     {
            //         // "RESTORE_ARITHMETIC_CHECKED"
            //         return S_OK;
            //     }});
            //     workStack.emplace(VisitNode{innerExpr});
            //     workStack.emplace(EmitAction{[](std::list<Opcode> &/*prog*/, std::string &/*out*/) -> HRESULT
            //     {
            //         // "SET_ARITHMETIC_CHECKED(false)"
            //         return S_OK;
            //     }});
            // }
            // return S_OK;

            output = "Checked and Unchecked expressions not implemented.";
            return E_NOTIMPL;
        }
    },
    // Roslyn: ParenthesizedExpression (e.g., "(a + b)")
    {"parenthesized_expression",
        [](TSNode node, const std::string &/*source*/, std::list<Opcode> &/*program*/, std::string &/*output*/, std::stack<WorkItem> &workStack) -> HRESULT
        {
            // child 0: "("
            // child 1: core inner expression node
            // child 2: ")"
            const TSNode innerExpr = ts_node_child(node, 1);

            // Just drill straight into the parenthesized code.
            // The tree shape guarantees mathematical ordering automatically.
            workStack.emplace(VisitNode{innerExpr});
            return S_OK;
        }
    },
    // Roslyn: Argument (Wraps modifiers like 'ref', 'out', 'in' or named args like 'x:')
    {"argument",
        [](TSNode node, const std::string &source, std::list<Opcode> &/*program*/, std::string &output, std::stack<WorkItem> &workStack) -> HRESULT
        {
            const uint32_t childCount = ts_node_child_count(node);
            const TSNode exprNode = ts_node_child(node, childCount - 1); // The core value expression is always last

            // Scan for ref/out modifiers to tell the debugger how to pass the variable descriptor
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

            workStack.emplace(VisitNode{exprNode});
            return S_OK;
        }
    }};

    std::stack<WorkItem> workStack;
    workStack.emplace(VisitNode{rootNode});

    while (!workStack.empty())
    {
        WorkItem item = std::move(workStack.top());
        workStack.pop();

        if (auto *emit = std::get_if<EmitAction>(&item))
        {
            // Execute deferred opcode emission.
            HRESULT hr = S_OK;
            try
            {
                hr = emit->action(program, output);
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

            if (FAILED(hr))
            {
                return hr;
            }
            continue;
        }

        // VisitNode path — dispatch through the handler map.
        const auto &visit = std::get<VisitNode>(item);
        const TSNode node = visit.node;

        if (ts_node_is_null(node) ||
            // Skip zero-width recovery nodes that aren't explicit errors
            ts_node_start_byte(node) == ts_node_end_byte(node))
        {
            continue;
        }

        const std::string_view type = ts_node_type(node);

        const auto findHandler = syntaxKindHandlerMap.find(type);
        if (findHandler != syntaxKindHandlerMap.end())
        {
            HRESULT hr = S_OK;
            try
            {
                hr = findHandler->second(node, source, program, output, workStack);
            }
            catch (const std::bad_alloc &)
            {
                output = "Failed to allocate memory while parsing expression node.";
                return E_OUTOFMEMORY;
            }
            catch (const std::exception &ex)
            {
                output = "Unhandled exception while parsing expression node: " + std::string(ex.what());
                return E_FAIL;
            }
            catch (...)
            {
                output = "Unknown exception while parsing expression node.";
                return E_FAIL;
            }

            if (FAILED(hr))
            {
                return hr;
            }
            continue;
        }

        const std::string_view rawText = GetNodeText(node, source);
        output = "Failed to parse node type: '" + std::string(type) + "' expression: '" + std::string(rawText) + "'";
        return E_INVALIDARG;
    }

    return S_OK;
}

} // unnamed namespace

HRESULT GenerateProgram(const std::string &expression, std::list<Opcode> &program, std::string &output)
{
    TSParser *parser = ts_parser_new();
    ts_parser_set_language(parser, tree_sitter_c_sharp());

    // Trim trailing whitespace from the input expression.
    std::string trimmedExpression = expression;
    trimmedExpression.erase(std::find_if(trimmedExpression.rbegin(), trimmedExpression.rend(), 
                                         [](unsigned char ch) { return !std::isspace(ch); }).base(),
                            trimmedExpression.end());

    static const std::string prefix = "class W{void M(){_ = ";
    static const std::string suffix = ";}}";
    const std::string fullSource = prefix + trimmedExpression + suffix;

    const auto startByte = static_cast<uint32_t>(prefix.length());
    const uint32_t endByte = startByte + static_cast<uint32_t>(trimmedExpression.length());

    TSTree *tree = ts_parser_parse_string(parser, nullptr, fullSource.c_str(), static_cast<uint32_t>(fullSource.length()));
    const TSNode rootNode = ts_tree_root_node(tree);

    // ts_node_has_error walks the whole tree internally and finds hidden missing tokens or malformed parts.
    if (ts_node_has_error(rootNode))
    {
        output = "Expression has malformed or incomplete syntax.";
        ts_tree_delete(tree);
        ts_parser_delete(parser);
        return E_INVALIDARG; // Stop here, do not process generation steps
    }

    // If the tree is clean, proceed with safe deconstruction as usual
    const TSNode exprNode = ts_node_descendant_for_byte_range(rootNode, startByte, endByte);

    const HRESULT Status = GenerateExecutionSteps(exprNode, fullSource, program, output);
    if (FAILED(Status))
    {
        program.clear();
    }

    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return Status;
}

} // namespace dncdbg::Parser
