/**
 * @file C# grammar for tree-sitter
 * @author Max Brunsfeld <maxbrunsfeld@gmail.com>
 * @author Damien Guard <damieng@gmail.com>
 * @author Amaan Qureshi <amaanq12@gmail.com>
 * @license MIT
 * @file C# grammar for tree-sitter (minimized for dncdbg expression evaluation)
 *
 * This grammar was trimmed down from the full tree-sitter-c-sharp grammar to
 * reduce the size of the generated parser.c (and therefore the binary/memory
 * footprint). Only the constructs needed to evaluate a C# expression are kept:
 *
 *   - The fixed wrapper produced by dncdbg's expression parser:
 *       class W { void M() { _ = <expression>; } }
 *     (compilation_unit -> class_declaration -> method_declaration -> block
 *      -> expression_statement -> assignment_expression -> expression).
 *   - Every expression node type dispatched by
 *     src/expressionparser/parser.cpp's syntaxKindHandlerMap.
 *   - The `type` grammar used by casts, typeof/sizeof, as/is, object creation
 *     and generic type arguments.
 *   - Literals, identifiers, contextual-keyword identifiers, and comments.
 *   - String/interpolation/raw-string rules and their external tokens are kept
 *     so that src/scanner.c stays compatible (its TokenType enum must match
 *     the `externals` order). They also remain parseable even though the
 *     evaluator currently rejects them with a clear error.
 *   - LINQ query expressions (query_expression and its clauses) are kept so
 *     `from ... where ... select ...` parses to a query_expression node and
 *     reaches the evaluator's dedicated (currently E_NOTIMPL) handler.
 *
 * Removed: all statements except expression statements, all declaration kinds
 * except class/method, patterns, preprocessor directives,
 * switch/collection/with/anonymous/stackalloc/range expressions, attributes,
 * namespaces, and other constructs not reachable from an expression context.
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const PREC = {
  GENERIC: 19,
  DOT: 18,
  INVOCATION: 18,
  POSTFIX: 18,
  PREFIX: 17,
  UNARY: 17,
  CAST: 17,
  RANGE: 16,
  SWITCH: 15,
  WITH: 14,
  MULT: 13,
  ADD: 12,
  SHIFT: 11,
  REL: 10,
  EQUAL: 9,
  AND: 8,
  XOR: 7,
  OR: 6,
  LOGICAL_AND: 5,
  LOGICAL_OR: 4,
  COALESCING: 3,
  CONDITIONAL: 2,
  ASSIGN: 1,
  SELECT: 0,
};

const decimalDigitSequence = /([0-9][0-9_]*[0-9]|[0-9])/;

const stringEncoding = /(u|U)8/;

export default grammar({
  name: 'c_sharp',

  conflicts: $ => [
    [$._simple_name, $.generic_name],
    [$.lvalue_expression, $._name],
    [$.type, $.nullable_type],
    [$.type, $._array_base_type],
    [$.type, $._pointer_base_type],
    [$.qualified_name, $.member_access_expression],
    [$._reserved_identifier, $.modifier],
    [$._reserved_identifier, $.from_clause],
  ],

  externals: $ => [
    $._optional_semi,
    $.interpolation_regular_start,
    $.interpolation_verbatim_start,
    $.interpolation_raw_start,
    $.interpolation_start_quote,
    $.interpolation_end_quote,
    $.interpolation_open_brace,
    $.interpolation_close_brace,
    $.interpolation_string_content,
    $.raw_string_start,
    $.raw_string_end,
    $.raw_string_content,
  ],

  extras: $ => [
    /[\s\u00A0\uFEFF\u3000]+/,
    $.comment,
  ],

  inline: $ => [
    $._nullable_base_type,
  ],

  supertypes: $ => [
    $.declaration,
    $.expression,
    $.non_lvalue_expression,
    $.lvalue_expression,
    $.literal,
    $.statement,
    $.type,
  ],

  word: $ => $._identifier_token,

  rules: {
    compilation_unit: $ => repeat($._top_level_item),

    _top_level_item: $ => $.class_declaration,

    class_declaration: $ => seq(
      repeat($.modifier),
      'class',
      field('name', $.identifier),
      $._declaration_list_body,
    ),

    _declaration_list_body: $ => choice(
      seq(field('body', $.declaration_list), $._optional_semi),
      ';',
    ),

    declaration_list: $ => seq(
      '{',
      repeat($.declaration),
      '}',
    ),

    declaration: $ => choice(
      $.method_declaration,
    ),

    method_declaration: $ => seq(
      repeat($.modifier),
      field('returns', $.type),
      field('name', $.identifier),
      field('parameters', $.parameter_list),
      $._function_body,
    ),

    modifier: _ => prec.right(choice(
      'abstract',
      'async',
      'const',
      'extern',
      'file',
      'fixed',
      'internal',
      'new',
      'override',
      'partial',
      'private',
      'protected',
      'public',
      'readonly',
      'required',
      'sealed',
      'static',
      'unsafe',
      'virtual',
      'volatile',
    )),

    parameter_list: $ => seq(
      '(',
      sep($.parameter, ','),
      ')',
    ),

    parameter: $ => seq(
      field('type', $.type),
      field('name', $.identifier),
      optional(seq('=', $.expression)),
    ),

    block: $ => seq('{', repeat($.statement), '}'),

    arrow_expression_clause: $ => seq('=>', $.expression),

    _function_body: $ => choice(
      field('body', $.block),
      seq(field('body', $.arrow_expression_clause), ';'),
      ';',
    ),

    statement: $ => prec(1, choice(
      $.expression_statement,
    )),

    expression_statement: $ => seq($._expression_statement_expression, ';'),

    qualified_name: $ => prec(PREC.DOT, seq(
      field('qualifier', $._name),
      '.',
      field('name', $._simple_name),
    )),

    _name: $ => choice(
      $.alias_qualified_name,
      $.qualified_name,
      $._simple_name,
    ),

    alias_qualified_name: $ => seq(
      field('alias', $.identifier),
      '::',
      field('name', $._simple_name),
    ),

    _simple_name: $ => choice(
      $.identifier,
      $.generic_name,
    ),

    generic_name: $ => seq($.identifier, $.type_argument_list),

    type_argument_list: $ => seq(
      '<',
      choice(
        repeat(','),
        commaSep1($.type),
      ),
      '>',
    ),

    type: $ => choice(
      $.array_type,
      $._name,
      $.nullable_type,
      $.pointer_type,
      $.predefined_type,
    ),

    array_type: $ => seq(
      field('type', $._array_base_type),
      field('rank', $.array_rank_specifier),
    ),

    _array_base_type: $ => choice(
      $.array_type,
      $._name,
      $.nullable_type,
      $.pointer_type,
      $.predefined_type,
    ),

    array_rank_specifier: $ => seq(
      '[',
      commaSep(optional($.expression)),
      ']',
    ),

    nullable_type: $ => seq(field('type', $._nullable_base_type), '?'),

    _nullable_base_type: $ => choice(
      $.array_type,
      $._name,
      $.predefined_type,
    ),

    pointer_type: $ => seq(field('type', $._pointer_base_type), '*'),

    _pointer_base_type: $ => choice(
      $._name,
      $.nullable_type,
      $.pointer_type,
      $.predefined_type,
    ),

    predefined_type: _ => token(choice(
      'bool',
      'byte',
      'char',
      'decimal',
      'double',
      'float',
      'int',
      'long',
      'object',
      'sbyte',
      'short',
      'string',
      'uint',
      'ulong',
      'ushort',
      'nint',
      'nuint',
      'void',
    )),

    expression: $ => choice(
      $.non_lvalue_expression,
      $.lvalue_expression,
    ),

    non_lvalue_expression: $ => choice(
      $.binary_expression,
      $.interpolated_string_expression,
      $.conditional_expression,
      $.conditional_access_expression,
      $.literal,
      $._expression_statement_expression,
      $.is_expression,
      $.as_expression,
      $.cast_expression,
      $.checked_expression,
      $.lambda_expression,
      $.sizeof_expression,
      $.typeof_expression,
      $.query_expression,
    ),

    lvalue_expression: $ => choice(
      'this',
      $.member_access_expression,
      $._simple_name,
      $.element_access_expression,
      alias($.bracketed_argument_list, $.element_binding_expression),
      alias($._pointer_indirection_expression, $.prefix_unary_expression),
      alias($._parenthesized_lvalue_expression, $.parenthesized_expression),
    ),

    // Covers error CS0201: Only assignment, call, increment, decrement, await,
    // and new object expressions can be used as a statement.
    _expression_statement_expression: $ => choice(
      $.assignment_expression,
      $.invocation_expression,
      $.postfix_unary_expression,
      $.prefix_unary_expression,
      $.await_expression,
      $.object_creation_expression,
      $.parenthesized_expression,
    ),

    assignment_expression: $ => seq(
      field('left', $.lvalue_expression),
      field('operator',
        choice(
          '=',
          '+=',
          '-=',
          '*=',
          '/=',
          '%=',
          '&=',
          '^=',
          '|=',
          '<<=',
          '>>=',
          '>>>=',
          '??=',
        ),
      ),
      field('right', $.expression),
    ),

    binary_expression: $ => choice(
      ...[
        ['&&', PREC.LOGICAL_AND],
        ['||', PREC.LOGICAL_OR],
        ['>>', PREC.SHIFT],
        ['>>>', PREC.SHIFT],
        ['<<', PREC.SHIFT],
        ['&', PREC.AND],
        ['^', PREC.XOR],
        ['|', PREC.OR],
        ['+', PREC.ADD],
        ['-', PREC.ADD],
        ['*', PREC.MULT],
        ['/', PREC.MULT],
        ['%', PREC.MULT],
        ['<', PREC.REL],
        ['<=', PREC.REL],
        ['==', PREC.EQUAL],
        ['!=', PREC.EQUAL],
        ['>=', PREC.REL],
        ['>', PREC.REL],
      ].map(([operator, precedence]) =>
        prec.left(precedence, seq(
          field('left', $.expression),
          // @ts-ignore
          field('operator', operator),
          field('right', $.expression),
        )),
      ),
      prec.right(PREC.COALESCING, seq(
        field('left', $.expression),
        field('operator', '??'),
        field('right', $.expression),
      )),
    ),

    postfix_unary_expression: $ => prec(PREC.POSTFIX, seq(
      $.expression,
      choice('++', '--', '!'),
    )),

    prefix_unary_expression: $ => prec(PREC.UNARY, seq(
      choice('++', '--', '+', '-', '!', '~', '&', '^'),
      $.expression,
    )),

    _pointer_indirection_expression: $ => prec.right(PREC.UNARY, seq(
      '*',
      $.lvalue_expression,
    )),

    conditional_expression: $ => prec.right(PREC.CONDITIONAL, seq(
      field('condition', $.expression),
      '?',
      field('consequence', $.expression),
      ':',
      field('alternative', $.expression),
    )),

    conditional_access_expression: $ => prec.right(PREC.CONDITIONAL, seq(
      field('condition', $.expression),
      '?',
      choice(
        $.member_binding_expression,
        alias($.bracketed_argument_list, $.element_binding_expression),
      ),
    )),

    as_expression: $ => prec(PREC.REL, seq(
      field('left', $.expression),
      field('operator', 'as'),
      field('right', $.type),
    )),

    is_expression: $ => prec(PREC.REL, seq(
      field('left', $.expression),
      field('operator', 'is'),
      field('right', $.type),
    )),

    cast_expression: $ => prec(PREC.CAST, prec.dynamic(1, seq(
      '(',
      field('type', $.type),
      ')',
      field('value', $.expression),
    ))),

    checked_expression: $ => seq(
      choice('checked', 'unchecked'),
      '(',
      $.expression,
      ')',
    ),

    invocation_expression: $ => prec(PREC.INVOCATION, seq(
      field('function', $.expression),
      field('arguments', $.argument_list),
    )),

    await_expression: $ => prec.right(PREC.UNARY, seq(
      'await',
      $.expression,
    )),

    element_access_expression: $ => prec(PREC.POSTFIX, seq(
      field('expression', $.expression),
      field('subscript', $.bracketed_argument_list),
    )),

    interpolated_string_expression: $ => choice(
      seq(
        alias($.interpolation_regular_start, $.interpolation_start),
        alias($.interpolation_start_quote, '"'),
        repeat($._interpolated_string_content),
        alias($.interpolation_end_quote, '"'),
      ),
      seq(
        alias($.interpolation_verbatim_start, $.interpolation_start),
        alias($.interpolation_start_quote, '"'),
        repeat($._interpolated_verbatim_string_content),
        alias($.interpolation_end_quote, '"'),
      ),
      seq(
        alias($.interpolation_raw_start, $.interpolation_start),
        alias($.interpolation_start_quote, $.interpolation_quote),
        repeat($._interpolated_raw_string_content),
        alias($.interpolation_end_quote, $.interpolation_quote),
      ),
    ),

    _interpolated_string_content: $ => choice(
      alias($.interpolation_string_content, $.string_content),
      $.escape_sequence,
      $.interpolation,
    ),

    _interpolated_verbatim_string_content: $ => choice(
      alias($.interpolation_string_content, $.string_content),
      $.interpolation,
    ),

    _interpolated_raw_string_content: $ => choice(
      alias($.interpolation_string_content, $.string_content),
      $.interpolation,
    ),

    interpolation: $ => seq(
      alias($.interpolation_open_brace, $.interpolation_brace),
      $.expression,
      optional($.interpolation_alignment_clause),
      optional($.interpolation_format_clause),
      alias($.interpolation_close_brace, $.interpolation_brace),
    ),

    interpolation_alignment_clause: $ => seq(',', $.expression),

    interpolation_format_clause: _ => seq(':', /[^}"]+/),

    member_access_expression: $ => prec(PREC.DOT, seq(
      field('expression', choice($.expression, $.predefined_type, $._name)),
      choice('.', '->'),
      field('name', $._simple_name),
    )),

    member_binding_expression: $ => seq(
      '.',
      field('name', $._simple_name),
    ),

    object_creation_expression: $ => prec.right(seq(
      'new',
      field('type', $.type),
      field('arguments', optional($.argument_list)),
      field('initializer', optional($.initializer_expression)),
    )),

    initializer_expression: $ => seq(
      '{',
      commaSep($.expression),
      optional(','),
      '}',
    ),

    parenthesized_expression: $ => seq(
      '(',
      $.non_lvalue_expression,
      ')',
    ),

    _parenthesized_lvalue_expression: $ => seq('(', $.lvalue_expression, ')'),

    lambda_expression: $ => prec(-1, seq(
      $._lambda_expression_init,
      '=>',
      field('body', choice($.block, $.expression)),
    )),

    _lambda_expression_init: $ => prec(-1, seq(
      optional(field('type', $.type)),
      field('parameters', $._lambda_parameters),
    )),

    _lambda_parameters: $ => prec(-1, choice(
      $.parameter_list,
      alias($.identifier, $.implicit_parameter),
    )),

    sizeof_expression: $ => seq(
      'sizeof',
      '(',
      field('type', $.type),
      ')',
    ),

    typeof_expression: $ => seq(
      'typeof',
      '(',
      field('type', $.type),
      ')',
    ),

    query_expression: $ => seq($.from_clause, $._query_body),

    from_clause: $ => seq(
      'from',
      optional(field('type', $.type)),
      field('name', $.identifier),
      'in',
      $.expression,
    ),

    _query_body: $ => prec.right(sep1(
      seq(
        repeat($._query_clause),
        $._select_or_group_clause,
      ),
      seq('into', $.identifier),
    )),

    _query_clause: $ => choice(
      $.from_clause,
      $.join_clause,
      $.let_clause,
      $.order_by_clause,
      $.where_clause,
    ),

    join_clause: $ => seq(
      'join',
      $._join_header,
      $._join_body,
      optional($.join_into_clause),
    ),

    _join_header: $ => seq(optional(field('type', $.type)), $.identifier, 'in', $.expression),

    _join_body: $ => seq('on', $.expression, 'equals', $.expression),

    join_into_clause: $ => seq('into', $.identifier),

    let_clause: $ => seq(
      'let',
      $.identifier,
      '=',
      $.expression,
    ),

    order_by_clause: $ => seq(
      'orderby',
      commaSep1($._ordering),
    ),

    _ordering: $ => seq(
      $.expression,
      optional(choice('ascending', 'descending')),
    ),

    where_clause: $ => seq('where', $.expression),

    _select_or_group_clause: $ => choice(
      $.group_clause,
      $.select_clause,
    ),

    group_clause: $ => seq('group', $.expression, 'by', $.expression),

    select_clause: $ => seq('select', $.expression),

    argument_list: $ => seq('(', commaSep($.argument), ')'),

    argument: $ => prec(1, seq(
      optional(seq(field('name', $.identifier), ':')),
      optional(choice('ref', 'out', 'in')),
      $.expression,
    )),

    bracketed_argument_list: $ => seq(
      '[',
      commaSep1($.argument),
      optional(','),
      ']',
    ),

    literal: $ => choice(
      $.null_literal,
      $.character_literal,
      $.integer_literal,
      $.real_literal,
      $.boolean_literal,
      $.string_literal,
      $.verbatim_string_literal,
      $.raw_string_literal,
    ),

    null_literal: _ => 'null',

    character_literal: $ => seq(
      '\'',
      choice($.character_literal_content, $.escape_sequence),
      '\'',
    ),

    character_literal_content: $ => token.immediate(/[^'\\]/),

    integer_literal: _ => token(seq(
      choice(
        decimalDigitSequence, // Decimal
        (/0[xX][0-9a-fA-F_]*[0-9a-fA-F]+/), // Hex
        (/0[bB][01_]*[01]+/), // Binary
      ),
      optional(/([uU][lL]?|[lL][uU]?)/),
    )),

    real_literal: _ => {
      const suffix = /[fFdDmM]/;
      const exponent = /[eE][+-]?[0-9][0-9_]*/;
      return token(choice(
        seq(
          decimalDigitSequence,
          '.',
          decimalDigitSequence,
          optional(exponent),
          optional(suffix),
        ),
        seq(
          '.',
          decimalDigitSequence,
          optional(exponent),
          optional(suffix),
        ),
        seq(
          decimalDigitSequence,
          exponent,
          optional(suffix),
        ),
        seq(
          decimalDigitSequence,
          suffix,
        ),
      ));
    },

    string_literal: $ => seq(
      '"',
      repeat(choice(
        $.string_literal_content,
        $.escape_sequence,
      )),
      '"',
      optional($.string_literal_encoding),
    ),

    string_literal_content: _ => token.immediate(prec(1, /[^"\\\n]+/)),

    escape_sequence: _ => token(choice(
      /\\x[0-9a-fA-F]{1,4}/,
      /\\u[0-9a-fA-F]{4}/,
      /\\U[0-9a-fA-F]{8}/,
      /\\[abefnrtv'\"\\\?0]/,
    )),

    string_literal_encoding: _ => token.immediate(stringEncoding),

    verbatim_string_literal: _ => token(seq(
      '@"',
      repeat(choice(
        /[^"]/,
        '""',
      )),
      '"',
      optional(stringEncoding),
    )),

    raw_string_literal: $ => seq(
      $.raw_string_start,
      $.raw_string_content,
      $.raw_string_end,
      optional(stringEncoding),
    ),

    boolean_literal: _ => choice('true', 'false'),

    _identifier_token: _ => token(seq(optional('@'), /(\p{XID_Start}|_|\\u[0-9A-Fa-f]{4}|\\U[0-9A-Fa-f]{8})(\p{XID_Continue}|\\u[0-9A-Fa-f]{4}|\\U[0-9A-Fa-f]{8})*/)),
    identifier: $ => choice(
      $._identifier_token,
      $._reserved_identifier,
    ),

    _reserved_identifier: _ => choice(
      'alias',
      'ascending',
      'by',
      'descending',
      'equals',
      'file',
      'from',
      'global',
      'group',
      'into',
      'join',
      'let',
      'notnull',
      'on',
      'orderby',
      'scoped',
      'select',
      'unmanaged',
      'var',
      'when',
      'where',
      'yield',
    ),

    comment: _ => token(choice(
      seq('//', /[^\n\r]*/),
      seq(
        '/*',
        /[^*]*\*+([^/*][^*]*\*+)*/,
        '/',
      ),
    )),
  },
});

/**
 * Creates a rule to match one or more of the rules separated by a comma
 *
 * @param {Rule} rule
 *
 * @returns {SeqRule}
 */
function commaSep1(rule) {
  return seq(rule, repeat(seq(',', rule)));
}

/**
 * Creates a rule to optionally match one or more of the rules separated by a comma
 *
 * @param {Rule} rule
 *
 * @returns {ChoiceRule}
 */
function commaSep(rule) {
  return optional(commaSep1(rule));
}

/**
 * Creates a rule to match one or more of the rules separated by `separator`
 *
 * @param {RuleOrLiteral} rule
 *
 * @param {RuleOrLiteral} separator
 *
 * @returns {SeqRule}
 */
function sep1(rule, separator) {
  return seq(rule, repeat(seq(separator, rule)));
}

/**
 * Creates a rule to optionally match one or more of the rules separated by `separator`
 *
 * @param {RuleOrLiteral} rule
 *
 * @param {RuleOrLiteral} separator
 *
 * @returns {ChoiceRule}
 */
function sep(rule, separator) {
  return optional(sep1(rule, separator));
}
