// Copyright (c) 2026 Mikhail Kurinnoi
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "expressionparser/parser.h"
#include "utils/torelease.h"
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

HRESULT GenerateExecutionSteps(TSNode node, const std::string &source, std::list<Opcode> &program, std::string &output)
{
    if (ts_node_is_null(node) ||
        // Skip zero-width recovery nodes that aren't explicit errors
        ts_node_start_byte(node) == ts_node_end_byte(node))
    {
        return S_OK;
    }

    HRESULT Status = S_OK;
    const std::string_view type = ts_node_type(node);
    const std::string_view rawText = GetNodeText(node, source);

    // Roslyn: NullLiteralExpression
    if (rawText == "null")
    {
        program.emplace_back(SyntaxKind::NullLiteralExpression);
        return S_OK;
    }

    // Roslyn: ThisExpression
    if (rawText == "this")
    {
        program.emplace_back(SyntaxKind::ThisExpression);
        return S_OK;
    }

    // Roslyn: TrueLiteralExpression
    if (rawText == "true")
    {
        program.emplace_back(SyntaxKind::TrueLiteralExpression);
        return S_OK;
    }

    // Roslyn: FalseLiteralExpression
    if (rawText == "false")
    {
        program.emplace_back(SyntaxKind::FalseLiteralExpression);
        return S_OK;
    }

    // Roslyn: IdentifierName
    if (type == "identifier")
    {
        program.emplace_back(SyntaxKind::IdentifierName, std::string(rawText));
        return S_OK;
    }

    // Roslyn: GenericName (e.g., List<int>)
    if (type == "generic_name")
    {
        const TSNode nameNode = ts_node_named_child(node, 0);
        const TSNode typeArgs = ts_node_named_child(node, 1);
        const uint32_t count = ts_node_named_child_count(typeArgs);
        for (uint32_t i = 0; i < count; ++i)
        {
            IfFailRet(GenerateExecutionSteps(ts_node_named_child(typeArgs, i), source, program, output));
        }
        program.emplace_back(SyntaxKind::GenericName, std::string(GetNodeText(nameNode, source)), count);
        return S_OK;
    }

    // Roslyn: QualifiedName (e.g., System.Collections)
    if (type == "qualified_name")
    {
        const TSNode left = ts_node_named_child(node, 0);
        const TSNode right = ts_node_named_child(node, 1);
        IfFailRet(GenerateExecutionSteps(left, source, program, output));
        IfFailRet(GenerateExecutionSteps(right, source, program, output));
        program.emplace_back(SyntaxKind::QualifiedName);
        return S_OK;
    }

    // Roslyn: AliasQualifiedName (e.g., global::System)
    if (type == "alias_qualified_name")
    {
        // TODO implement in evalstackmachine.cpp before uncomment this code

        // TSNode alias = ts_node_named_child(node, 0);
        // TSNode name = ts_node_named_child(node, 1);
        // "PUSH_ALIAS(" + std::string(GetNodeText(alias, source)) + ")"
        // generate_execution_steps(name, source, steps);
        // "RESOLVE_ALIAS_NAME"

        output = "Alias qualified name not implemented.";
        return E_NOTIMPL;
    }

    // Roslyn: NumericLiteralExpression
    if (type == "integer_literal" || type == "real_literal")
    {
        const uint32_t realLiteral = (type == "real_literal") ? 1 : 0;
        program.emplace_back(SyntaxKind::NumericLiteralExpression, std::string(rawText), realLiteral);
        return S_OK;
    }

    // Roslyn: StringLiteralExpression
    if (type == "string_literal" || type == "verbatim_string_literal")
    {
        std::string_view str = GetNodeText(node, source);
        if (str.size() >= 2 && str.front() == '"' && str.back() == '"')
        {
            str.remove_prefix(1);
            str.remove_suffix(1);
        }
        program.emplace_back(SyntaxKind::StringLiteralExpression, std::string(str));
        return S_OK;
    }

    // Roslyn: CharacterLiteralExpression
    if (type == "character_literal")
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

    // Roslyn: PredefinedType (e.g., int, string, void)
    if (type == "predefined_type")
    {
        program.emplace_back(SyntaxKind::PredefinedType, std::string(rawText));
        return S_OK;
    }

    // Roslyn: ObjectCreationExpression (e.g., new Foo())
    if (type == "object_creation_expression")
    {
        // TODO implement in evalstackmachine.cpp before uncomment this code

        // const TSNode typeNode = ts_node_named_child(node, 0);
        // const TSNode argList = ts_node_named_child(node, 1);
        // uint32_t argCount = 0;
        // if (!ts_node_is_null(argList) && std::string_view(ts_node_type(argList)) == "argument_list")
        // {
        //     argCount = ts_node_named_child_count(argList);
        //     for (uint32_t i = 0; i < argCount; ++i)
        //     {
        //         IfFailRet(GenerateExecutionSteps(ts_node_named_child(argList, i), source, program, output));
        //     }
        // }
        // IfFailRet(GenerateExecutionSteps(typeNode, source, program, output));
        // program.emplace_back(SyntaxKind::ObjectCreationExpression, argCount);
        // return S_OK;

        output = "Object creation expression not implemented.";
        return E_NOTIMPL;
    }

    // Roslyn: SimpleMemberAccessExpression (a.b) AND PointerMemberAccessExpression (a->b)
    if (type == "member_access_expression")
    {
        const TSNode expr = ts_node_child(node, 0);
        const TSNode op = ts_node_child(node, 1); // The operator token (. or ->)
        const TSNode name = ts_node_child(node, 2);

        IfFailRet(GenerateExecutionSteps(expr, source, program, output));
        IfFailRet(GenerateExecutionSteps(name, source, program, output));

        // Check what kind of separator is used in the source code
        const std::string_view opText = GetNodeText(op, source);
        if (opText == "->")
        {
            // TODO implement in evalstackmachine.cpp before uncomment this code

            // program.emplace_back(SyntaxKind::PointerMemberAccessExpression);
            output = "Pointer member access expression not implemented.";
            return E_NOTIMPL;
        }
        else
        {
            program.emplace_back(SyntaxKind::SimpleMemberAccessExpression);
        }
        return S_OK;
    }

    // Roslyn: MemberBindingExpression
    if (type == "member_binding_expression")
    {
        const TSNode name = ts_node_named_child(node, 0);
        IfFailRet(GenerateExecutionSteps(name, source, program, output));
        program.emplace_back(SyntaxKind::MemberBindingExpression);
        return S_OK;
    }

    // Roslyn: ElementBindingExpression
    if (type == "element_binding_expression")
    {
        const TSNode indexList = ts_node_named_child(node, 0);
        const uint32_t argCount = ts_node_named_child_count(indexList);
        for (uint32_t i = 0; i < argCount; ++i)
        {
            IfFailRet(GenerateExecutionSteps(ts_node_named_child(indexList, i), source, program, output));
        }
        program.emplace_back(SyntaxKind::ElementBindingExpression, argCount);
        return S_OK;
    }

    // Roslyn: ConditionalAccessExpression (e.g., user?.Name or array?[0])
    if (type == "conditional_access_expression")
    {
        // In Tree-sitter C#, conditional_access_expression always has exactly 3 children:
        // child 0: The base expression (e.g., 'user' or 'array')
        // child 1: The operator token ('?.' or '?[')
        // child 2: The binding expression (member_binding_expression or element_binding_expression)
        const TSNode baseExpr = ts_node_child(node, 0);
        const TSNode binding = ts_node_child(node, 2);

        // 1. Evaluate the base object first
        IfFailRet(GenerateExecutionSteps(baseExpr, source, program, output));

        // FIXME: Binding now works with null check all the time. Emit the conditional jump or
        //        provide as marker in MemberBindingExpression and ElementBindingExpression.

        // 2. Evaluate the binding (e.g., fetching the member or the element)
        IfFailRet(GenerateExecutionSteps(binding, source, program, output));
        return S_OK;
    }

    // Roslyn: InvocationExpression (e.g., Method())
    if (type == "invocation_expression")
    {
        const TSNode function = ts_node_named_child(node, 0);
        const TSNode argList = ts_node_named_child(node, 1);
        uint32_t argCount = 0;
        IfFailRet(GenerateExecutionSteps(function, source, program, output));
        if (!ts_node_is_null(argList))
        {
            argCount = ts_node_named_child_count(argList);
            for (uint32_t i = 0; i < argCount; ++i)
            {
                IfFailRet(GenerateExecutionSteps(ts_node_named_child(argList, i), source, program, output));
            }
        }
        program.emplace_back(SyntaxKind::InvocationExpression, argCount);
        return S_OK;
    }

    // Roslyn: ElementAccessExpression (e.g., array[index])
    if (type == "element_access_expression")
    {
        const TSNode expression = ts_node_named_child(node, 0);
        const TSNode indexList = ts_node_named_child(node, 1);
        const uint32_t argCount = ts_node_named_child_count(indexList);
        IfFailRet(GenerateExecutionSteps(expression, source, program, output));
        for (uint32_t i = 0; i < argCount; ++i)
        {
            IfFailRet(GenerateExecutionSteps(ts_node_named_child(indexList, i), source, program, output));
        }
        program.emplace_back(SyntaxKind::ElementAccessExpression, argCount);
        return S_OK;
    }

    // Roslyn Binary Group: Handles Add, Multiply, Subtract, Divide, Modulo, LeftShift, RightShift,
    // BitwiseAnd, BitwiseOr, ExclusiveOr, LogicalAnd, LogicalOr, Equals, NotEquals,
    // GreaterThan, LessThan, GreaterThanOrEqual, LessThanOrEqual, Coalesce Expressions.
    if (type == "binary_expression")
    {
        const TSNode left = ts_node_child(node, 0); // Always the first child (index 0)
        const TSNode right = ts_node_child(node, 2); // Always the third child (index 2)
        const std::string op = std::string(GetNodeText(ts_node_child(node, 1), source)); // Operator is index 1

        IfFailRet(GenerateExecutionSteps(left, source, program, output));
        IfFailRet(GenerateExecutionSteps(right, source, program, output));

        if (op == "+")
        {
            program.emplace_back(SyntaxKind::AddExpression);
        }
        else if (op == "-")
        {
            program.emplace_back(SyntaxKind::SubtractExpression);
        }
        else if (op == "*")
        {
            program.emplace_back(SyntaxKind::MultiplyExpression);
        }
        else if (op == "/")
        {
            program.emplace_back(SyntaxKind::DivideExpression);
        }
        else if (op == "%")
        {
            program.emplace_back(SyntaxKind::ModuloExpression);
        }
        else if (op == "<<")
        {
            program.emplace_back(SyntaxKind::LeftShiftExpression);
        }
        else if (op == ">>")
        {
            program.emplace_back(SyntaxKind::RightShiftExpression);
        }
        else if (op == "&")
        {
            program.emplace_back(SyntaxKind::BitwiseAndExpression);
        }
        else if (op == "|")
        {
            program.emplace_back(SyntaxKind::BitwiseOrExpression);
        }
        else if (op == "^")
        {
            program.emplace_back(SyntaxKind::ExclusiveOrExpression);
        }
        else if (op == "&&")
        {
            program.emplace_back(SyntaxKind::LogicalAndExpression);
        }
        else if (op == "||")
        {
            program.emplace_back(SyntaxKind::LogicalOrExpression);
        }
        else if (op == "==")
        {
            program.emplace_back(SyntaxKind::EqualsExpression);
        }
        else if (op == "!=")
        {
            program.emplace_back(SyntaxKind::NotEqualsExpression);
        }
        else if (op == ">")
        {
            program.emplace_back(SyntaxKind::GreaterThanExpression);
        }
        else if (op == "<")
        {
            program.emplace_back(SyntaxKind::LessThanExpression);
        }
        else if (op == ">=")
        {
            program.emplace_back(SyntaxKind::GreaterThanOrEqualExpression);
        }
        else if (op == "<=")
        {
            program.emplace_back(SyntaxKind::LessThanOrEqualExpression);
        }
        else if (op == "??")
        {
            program.emplace_back(SyntaxKind::CoalesceExpression);
        }
        else
        {
            output = "Unknown binary expression: " + op;
            return E_INVALIDARG;
        }
        return S_OK;
    }

    // Roslyn Unary Group: UnaryPlus, UnaryMinus, LogicalNot, BitwiseNot, PreIncrement, PreDecrement
    if (type == "prefix_unary_expression")
    {
        const TSNode operand = ts_node_named_child(node, 0);
        const std::string op = std::string(GetNodeText(ts_node_child(node, 0), source)); // Operator is first child
        IfFailRet(GenerateExecutionSteps(operand, source, program, output));

        if (op == "+")
        {
            program.emplace_back(SyntaxKind::UnaryPlusExpression);
        }
        else if (op == "-")
        {
            program.emplace_back(SyntaxKind::UnaryMinusExpression);
        }
        else if (op == "!")
        {
            program.emplace_back(SyntaxKind::LogicalNotExpression);
        }
        else if (op == "~")
        {
            program.emplace_back(SyntaxKind::BitwiseNotExpression);
        }
        else if (op == "++")
        {
            // TODO implement in evalstackmachine.cpp before uncomment this code

            // program.emplace_back(SyntaxKind::PreIncrementExpression);

            output = "Pre increment expression not implemented.";
            return E_NOTIMPL;
        }
        else if (op == "--")
        {
            // TODO implement in evalstackmachine.cpp before uncomment this code

            // program.emplace_back(SyntaxKind::PreDecrementExpression);

            output = "Pre decrement expression not implemented.";
            return E_NOTIMPL;
        }
        return S_OK;
    }

    // Roslyn Unary Group: PostIncrement, PostDecrement
    if (type == "postfix_unary_expression")
    {
        const TSNode operand = ts_node_named_child(node, 0);
        const std::string op = std::string(GetNodeText(ts_node_child(node, 1), source));
        IfFailRet(GenerateExecutionSteps(operand, source, program, output));

        if (op == "++")
        {
            // TODO implement in evalstackmachine.cpp before uncomment this code

            // program.emplace_back(SyntaxKind::PostIncrementExpression);

            output = "Post increment expression not implemented.";
            return E_NOTIMPL;
        }
        else if (op == "--")
        {
            // TODO implement in evalstackmachine.cpp before uncomment this code

            // program.emplace_back(SyntaxKind::PostDecrementExpression);

            output = "Post decrement expression not implemented.";
            return E_NOTIMPL;
        }
        return S_OK;
    }

    // Roslyn: CastExpression (e.g., (int)a or (Object)this)
    if (type == "cast_expression")
    {
        // TODO implement in evalstackmachine.cpp before uncomment this code

        // In Tree-sitter C#, cast_expression structure:
        // child 0: "("
        // child 1: type node
        // child 2: ")"
        // child 3: target expression node (can be anonymous)
        // const TSNode typeNode = ts_node_child(node, 1);
        // const TSNode exprNode = ts_node_child(node, 3);

        // IfFailRet(GenerateExecutionSteps(exprNode, source, program, output));
        // IfFailRet(GenerateExecutionSteps(typeNode, source, program, output));
        // program.emplace_back(SyntaxKind::CastExpression);
        // return S_OK;

        output = "Cast expression not implemented.";
        return E_NOTIMPL;
    }

    // Roslyn: AsExpression (e.g., a as Foo or this as Foo)
    if (type == "as_expression")
    {
        // TODO implement in evalstackmachine.cpp before uncomment this code

        // child 0: left expression node (can be anonymous)
        // child 1: "as" keyword
        // child 2: type node
        // const TSNode exprNode = ts_node_child(node, 0);
        // const TSNode typeNode = ts_node_child(node, 2);

        // IfFailRet(GenerateExecutionSteps(exprNode, source, program, output));
        // IfFailRet(GenerateExecutionSteps(typeNode, source, program, output));
        // program.emplace_back(SyntaxKind::AsExpression);
        // return S_OK;

        output = "AS expression not implemented.";
        return E_NOTIMPL;
    }

    // Roslyn: IsExpression (e.g., a is Foo or null is Foo)
    if (type == "is_expression")
    {
        // TODO implement in evalstackmachine.cpp before uncomment this code

        // child 0: left expression node (can be anonymous)
        // child 1: "is" keyword
        // child 2: type or pattern node
        // const TSNode exprNode = ts_node_child(node, 0);
        // const TSNode typeNode = ts_node_child(node, 2);

        // IfFailRet(GenerateExecutionSteps(exprNode, source, program, output));
        // IfFailRet(GenerateExecutionSteps(typeNode, source, program, output));
        // program.emplace_back(SyntaxKind::IsExpression);
        // return S_OK;

        output = "IS expression not implemented.";
        return E_NOTIMPL;
    }

    // Roslyn: SizeOfExpression (e.g., sizeof(int))
    if (type == "sizeof_expression")
    {
        const TSNode typeNode = ts_node_named_child(node, 0);
        IfFailRet(GenerateExecutionSteps(typeNode, source, program, output));
        program.emplace_back(SyntaxKind::SizeOfExpression);
        return S_OK;
    }

    // Roslyn: TypeOfExpression (e.g., typeof(List<int>))
    if (type == "typeof_expression")
    {
        // TODO implement in evalstackmachine.cpp before uncomment this code

        // child 0: "typeof", child 1: "(", child 2: type node, child 3: ")"
        // const TSNode typeNode = ts_node_child(node, 2);
        // IfFailRet(GenerateExecutionSteps(typeNode, source, program, output));
        // program.emplace_back(SyntaxKind::TypeOfExpression);
        // return S_OK;

        output = "TypeOf expression not implemented.";
        return E_NOTIMPL;
    }

    // Roslyn: ConditionalExpression (e.g., a ? b : c)
    if (type == "conditional_expression")
    {
        // TODO implement in evalstackmachine.cpp before uncomment this code

        // In Tree-sitter C#, a conditional expression has exactly 5 children:
        // index 0: condition node
        // index 1: "?" token
        // index 2: true branch node (can be anonymous like null or this)
        // index 3: ":" token
        // index 4: false branch node (can be anonymous like null or this)
        // const TSNode condition = ts_node_child(node, 0);
        // const TSNode conTrue = ts_node_child(node, 2);
        // const TSNode conFalse = ts_node_child(node, 4);

        // IfFailRet(GenerateExecutionSteps(condition, source, program, output));
        // "JUMP_IF_FALSE_TO_ELSE"
        // IfFailRet(GenerateExecutionSteps(conTrue, source, program, output));
        // "JUMP_TO_END"
        // IfFailRet(GenerateExecutionSteps(conFalse, source, program, output));
        // "MARK_END"
        // return S_OK;

        output = "Conditional expression not implemented.";
        return E_NOTIMPL;
    }

    // Roslyn: CheckedExpression AND UncheckedExpression (e.g., checked(a + b) or unchecked(x * y))
    if (type == "checked_expression")
    {
        // TODO implement in evalstackmachine.cpp before uncomment this code

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
        //     // "SET_ARITHMETIC_CHECKED(true)"
        //     IfFailRet(GenerateExecutionSteps(innerExpr, source, program, output));
        //     // "RESTORE_ARITHMETIC_CHECKED"
        // }
        // else
        // {
        //     // "SET_ARITHMETIC_CHECKED(false)"
        //     IfFailRet(GenerateExecutionSteps(innerExpr, source, program, output));
        //     // "RESTORE_ARITHMETIC_CHECKED"
        // }
        // return S_OK;

        output = "Checked and Unchecked expressions not implemented.";
        return E_NOTIMPL;
    }

    // Roslyn: ParenthesizedExpression (e.g., "(a + b)")
    if (type == "parenthesized_expression")
    {
        // child 0: "("
        // child 1: core inner expression node
        // child 2: ")"
        const TSNode innerExpr = ts_node_child(node, 1);

        // Just drill straight into the parenthesized code.
        // The tree shape guarantees mathematical ordering automatically.
        IfFailRet(GenerateExecutionSteps(innerExpr, source, program, output));
        return S_OK;
    }

    // Roslyn: Argument (Wraps modifiers like 'ref', 'out', 'in' or named args like 'x:')
    if (type == "argument")
    {
        const uint32_t childCount = ts_node_child_count(node);
        const TSNode exprNode = ts_node_child(node, childCount - 1); // The core value expression is always last

        IfFailRet(GenerateExecutionSteps(exprNode, source, program, output));

        // Scan for ref/out modifiers to tell the debugger how to pass the variable descriptor
        std::string flags = "none";
        for (uint32_t i = 0; i < childCount - 1; ++i)
        {
            const std::string_view txt = GetNodeText(ts_node_child(node, i), source);
            if (txt == "ref")
            {
                flags = "ref";
            }
            if (txt == "out")
            {
                flags = "out";
            }
            if (txt == "in")
            {
                flags = "in";
            }
        }

        // TODO
        // "MARK_ARGUMENT_MODIFIER(" + flags + ")"
        if (flags != "none")
        {
            output = "ref/out/in modifiers not implemented.";
            return E_NOTIMPL;
        }

        return S_OK;
    }

    output = "Failed parse type: '" + std::string(type) + "' expression: '" + std::string(rawText) + "'";
    return E_INVALIDARG;
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
