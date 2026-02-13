// A Bison parser, made by GNU Bison 3.8.2.

// Skeleton implementation for Bison LALR(1) parsers in C++

// Copyright (C) 2002-2015, 2018-2021 Free Software Foundation, Inc.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

// As a special exception, you may create a larger work that contains
// part or all of the Bison parser skeleton and distribute that work
// under terms of your choice, so long as that work isn't itself a
// parser generator using the skeleton or a modified version thereof
// as a parser skeleton.  Alternatively, if you modify or redistribute
// the parser skeleton itself, you may (at your option) remove this
// special exception, which will cause the skeleton and the resulting
// Bison output files to be licensed under the GNU General Public
// License without this special exception.

// This special exception was added by the Free Software Foundation in
// version 2.2 of Bison.

// DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
// especially those whose name start with YY_ or yy_.  They are
// private implementation details that can be changed or removed.





#include "parser.auto.h"


// Unqualified %code blocks.
#line 63 "flex-bison/specification.y"

  #include <iostream>
  #include <cstdlib>
  #include <fstream>

  #include "../include/ir_compilation_unit.h"
  #include "../include/scanner.h"

#undef yylex
#define yylex scanner.yylex

#line 58 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"


#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> // FIXME: INFRINGES ON USER NAME SPACE.
#   define YY_(msgid) dgettext ("bison-runtime", msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(msgid) msgid
# endif
#endif


// Whether we are compiled with exception support.
#ifndef YY_EXCEPTIONS
# if defined __GNUC__ && !defined __EXCEPTIONS
#  define YY_EXCEPTIONS 0
# else
#  define YY_EXCEPTIONS 1
# endif
#endif

#define YYRHSLOC(Rhs, K) ((Rhs)[K].location)
/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

# ifndef YYLLOC_DEFAULT
#  define YYLLOC_DEFAULT(Current, Rhs, N)                               \
    do                                                                  \
      if (N)                                                            \
        {                                                               \
          (Current).begin  = YYRHSLOC (Rhs, 1).begin;                   \
          (Current).end    = YYRHSLOC (Rhs, N).end;                     \
        }                                                               \
      else                                                              \
        {                                                               \
          (Current).begin = (Current).end = YYRHSLOC (Rhs, 0).end;      \
        }                                                               \
    while (false)
# endif


// Enable debugging if requested.
#if YYDEBUG

// A pseudo ostream that takes yydebug_ into account.
# define YYCDEBUG if (yydebug_) (*yycdebug_)

# define YY_SYMBOL_PRINT(Title, Symbol)         \
  do {                                          \
    if (yydebug_)                               \
    {                                           \
      *yycdebug_ << Title << ' ';               \
      yy_print_ (*yycdebug_, Symbol);           \
      *yycdebug_ << '\n';                       \
    }                                           \
  } while (false)

# define YY_REDUCE_PRINT(Rule)          \
  do {                                  \
    if (yydebug_)                       \
      yy_reduce_print_ (Rule);          \
  } while (false)

# define YY_STACK_PRINT()               \
  do {                                  \
    if (yydebug_)                       \
      yy_stack_print_ ();                \
  } while (false)

#else // !YYDEBUG

# define YYCDEBUG if (false) std::cerr
# define YY_SYMBOL_PRINT(Title, Symbol)  YY_USE (Symbol)
# define YY_REDUCE_PRINT(Rule)           static_cast<void> (0)
# define YY_STACK_PRINT()                static_cast<void> (0)

#endif // !YYDEBUG

#define yyerrok         (yyerrstatus_ = 0)
#define yyclearin       (yyla.clear ())

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYRECOVERING()  (!!yyerrstatus_)

#line 5 "flex-bison/specification.y"
namespace piranha {
#line 151 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"

  /// Build a parser object.
  Parser::Parser (Scanner &scanner_yyarg, IrCompilationUnit &driver_yyarg)
#if YYDEBUG
    : yydebug_ (false),
      yycdebug_ (&std::cerr),
#else
    :
#endif
      scanner (scanner_yyarg),
      driver (driver_yyarg)
  {}

  Parser::~Parser ()
  {}

  Parser::syntax_error::~syntax_error () YY_NOEXCEPT YY_NOTHROW
  {}

  /*---------.
  | symbol.  |
  `---------*/

  // basic_symbol.
  template <typename Base>
  Parser::basic_symbol<Base>::basic_symbol (const basic_symbol& that)
    : Base (that)
    , value ()
    , location (that.location)
  {
    switch (this->kind ())
    {
      case symbol_kind::S_attribute: // attribute
        value.copy< piranha::IrAttribute * > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_port_declaration: // port_declaration
      case symbol_kind::S_port_status: // port_status
      case symbol_kind::S_port_value: // port_value
      case symbol_kind::S_port_connection: // port_connection
      case symbol_kind::S_documented_port_definition: // documented_port_definition
        value.copy< piranha::IrAttributeDefinition * > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_port_definitions: // port_definitions
        value.copy< piranha::IrAttributeDefinitionList * > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_connection_block: // connection_block
      case symbol_kind::S_attribute_list: // attribute_list
        value.copy< piranha::IrAttributeList * > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_import_statement: // import_statement
      case symbol_kind::S_import_statement_visibility: // import_statement_visibility
      case symbol_kind::S_import_statement_short_name: // import_statement_short_name
        value.copy< piranha::IrImportStatement * > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_node: // node
      case symbol_kind::S_node_member: // node_member
      case symbol_kind::S_inline_node_member: // inline_node_member
      case symbol_kind::S_inline_node: // inline_node
        value.copy< piranha::IrNode * > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_node_name: // node_name
      case symbol_kind::S_node_inline: // node_inline
      case symbol_kind::S_node_shadow: // node_shadow
      case symbol_kind::S_node_definition: // node_definition
      case symbol_kind::S_specific_node_definition: // specific_node_definition
      case symbol_kind::S_immediate_node_definition: // immediate_node_definition
      case symbol_kind::S_node_decorator: // node_decorator
        value.copy< piranha::IrNodeDefinition * > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_node_list: // node_list
        value.copy< piranha::IrNodeList * > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_type_name_namespace: // type_name_namespace
        value.copy< piranha::IrTokenInfoSet<std::string, 2> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_BOOL: // BOOL
        value.copy< piranha::IrTokenInfo_bool > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_FLOAT: // FLOAT
        value.copy< piranha::IrTokenInfo_float > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_INT: // INT
        value.copy< piranha::IrTokenInfo_int > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_CHAR: // CHAR
      case symbol_kind::S_IMPORT: // IMPORT
      case symbol_kind::S_AS: // AS
      case symbol_kind::S_NODE: // NODE
      case symbol_kind::S_INLINE: // INLINE
      case symbol_kind::S_ALIAS: // ALIAS
      case symbol_kind::S_INPUT: // INPUT
      case symbol_kind::S_OUTPUT: // OUTPUT
      case symbol_kind::S_MODIFY: // MODIFY
      case symbol_kind::S_TOGGLE: // TOGGLE
      case symbol_kind::S_LABEL: // LABEL
      case symbol_kind::S_STRING: // STRING
      case symbol_kind::S_DECORATOR: // DECORATOR
      case symbol_kind::S_PUBLIC: // PUBLIC
      case symbol_kind::S_PRIVATE: // PRIVATE
      case symbol_kind::S_BUILTIN_POINTER: // BUILTIN_POINTER
      case symbol_kind::S_NAMESPACE_POINTER: // NAMESPACE_POINTER
      case symbol_kind::S_UNRECOGNIZED: // UNRECOGNIZED
      case symbol_kind::S_OPERATOR: // OPERATOR
      case symbol_kind::S_MODULE: // MODULE
      case symbol_kind::S_AUTO: // AUTO
      case symbol_kind::S_27_: // '='
      case symbol_kind::S_28_: // '+'
      case symbol_kind::S_29_: // '-'
      case symbol_kind::S_30_: // '/'
      case symbol_kind::S_31_: // '*'
      case symbol_kind::S_32_: // '('
      case symbol_kind::S_33_: // ')'
      case symbol_kind::S_34_: // '{'
      case symbol_kind::S_35_: // '}'
      case symbol_kind::S_36_: // '['
      case symbol_kind::S_37_: // ']'
      case symbol_kind::S_38_: // ':'
      case symbol_kind::S_39_: // ';'
      case symbol_kind::S_40_: // ','
      case symbol_kind::S_41_: // '.'
      case symbol_kind::S_42_: // '^'
      case symbol_kind::S_type_name: // type_name
      case symbol_kind::S_standard_operator: // standard_operator
      case symbol_kind::S_string: // string
        value.copy< piranha::IrTokenInfo_string > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_label_value: // label_value
      case symbol_kind::S_value: // value
      case symbol_kind::S_constant: // constant
      case symbol_kind::S_atomic_value: // atomic_value
      case symbol_kind::S_primary_exp: // primary_exp
      case symbol_kind::S_data_access: // data_access
      case symbol_kind::S_unary_exp: // unary_exp
      case symbol_kind::S_mul_exp: // mul_exp
      case symbol_kind::S_add_exp: // add_exp
        value.copy< piranha::IrValue * > (YY_MOVE (that.value));
        break;

      default:
        break;
    }

  }




  template <typename Base>
  Parser::symbol_kind_type
  Parser::basic_symbol<Base>::type_get () const YY_NOEXCEPT
  {
    return this->kind ();
  }


  template <typename Base>
  bool
  Parser::basic_symbol<Base>::empty () const YY_NOEXCEPT
  {
    return this->kind () == symbol_kind::S_YYEMPTY;
  }

  template <typename Base>
  void
  Parser::basic_symbol<Base>::move (basic_symbol& s)
  {
    super_type::move (s);
    switch (this->kind ())
    {
      case symbol_kind::S_attribute: // attribute
        value.move< piranha::IrAttribute * > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_port_declaration: // port_declaration
      case symbol_kind::S_port_status: // port_status
      case symbol_kind::S_port_value: // port_value
      case symbol_kind::S_port_connection: // port_connection
      case symbol_kind::S_documented_port_definition: // documented_port_definition
        value.move< piranha::IrAttributeDefinition * > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_port_definitions: // port_definitions
        value.move< piranha::IrAttributeDefinitionList * > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_connection_block: // connection_block
      case symbol_kind::S_attribute_list: // attribute_list
        value.move< piranha::IrAttributeList * > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_import_statement: // import_statement
      case symbol_kind::S_import_statement_visibility: // import_statement_visibility
      case symbol_kind::S_import_statement_short_name: // import_statement_short_name
        value.move< piranha::IrImportStatement * > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_node: // node
      case symbol_kind::S_node_member: // node_member
      case symbol_kind::S_inline_node_member: // inline_node_member
      case symbol_kind::S_inline_node: // inline_node
        value.move< piranha::IrNode * > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_node_name: // node_name
      case symbol_kind::S_node_inline: // node_inline
      case symbol_kind::S_node_shadow: // node_shadow
      case symbol_kind::S_node_definition: // node_definition
      case symbol_kind::S_specific_node_definition: // specific_node_definition
      case symbol_kind::S_immediate_node_definition: // immediate_node_definition
      case symbol_kind::S_node_decorator: // node_decorator
        value.move< piranha::IrNodeDefinition * > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_node_list: // node_list
        value.move< piranha::IrNodeList * > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_type_name_namespace: // type_name_namespace
        value.move< piranha::IrTokenInfoSet<std::string, 2> > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_BOOL: // BOOL
        value.move< piranha::IrTokenInfo_bool > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_FLOAT: // FLOAT
        value.move< piranha::IrTokenInfo_float > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_INT: // INT
        value.move< piranha::IrTokenInfo_int > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_CHAR: // CHAR
      case symbol_kind::S_IMPORT: // IMPORT
      case symbol_kind::S_AS: // AS
      case symbol_kind::S_NODE: // NODE
      case symbol_kind::S_INLINE: // INLINE
      case symbol_kind::S_ALIAS: // ALIAS
      case symbol_kind::S_INPUT: // INPUT
      case symbol_kind::S_OUTPUT: // OUTPUT
      case symbol_kind::S_MODIFY: // MODIFY
      case symbol_kind::S_TOGGLE: // TOGGLE
      case symbol_kind::S_LABEL: // LABEL
      case symbol_kind::S_STRING: // STRING
      case symbol_kind::S_DECORATOR: // DECORATOR
      case symbol_kind::S_PUBLIC: // PUBLIC
      case symbol_kind::S_PRIVATE: // PRIVATE
      case symbol_kind::S_BUILTIN_POINTER: // BUILTIN_POINTER
      case symbol_kind::S_NAMESPACE_POINTER: // NAMESPACE_POINTER
      case symbol_kind::S_UNRECOGNIZED: // UNRECOGNIZED
      case symbol_kind::S_OPERATOR: // OPERATOR
      case symbol_kind::S_MODULE: // MODULE
      case symbol_kind::S_AUTO: // AUTO
      case symbol_kind::S_27_: // '='
      case symbol_kind::S_28_: // '+'
      case symbol_kind::S_29_: // '-'
      case symbol_kind::S_30_: // '/'
      case symbol_kind::S_31_: // '*'
      case symbol_kind::S_32_: // '('
      case symbol_kind::S_33_: // ')'
      case symbol_kind::S_34_: // '{'
      case symbol_kind::S_35_: // '}'
      case symbol_kind::S_36_: // '['
      case symbol_kind::S_37_: // ']'
      case symbol_kind::S_38_: // ':'
      case symbol_kind::S_39_: // ';'
      case symbol_kind::S_40_: // ','
      case symbol_kind::S_41_: // '.'
      case symbol_kind::S_42_: // '^'
      case symbol_kind::S_type_name: // type_name
      case symbol_kind::S_standard_operator: // standard_operator
      case symbol_kind::S_string: // string
        value.move< piranha::IrTokenInfo_string > (YY_MOVE (s.value));
        break;

      case symbol_kind::S_label_value: // label_value
      case symbol_kind::S_value: // value
      case symbol_kind::S_constant: // constant
      case symbol_kind::S_atomic_value: // atomic_value
      case symbol_kind::S_primary_exp: // primary_exp
      case symbol_kind::S_data_access: // data_access
      case symbol_kind::S_unary_exp: // unary_exp
      case symbol_kind::S_mul_exp: // mul_exp
      case symbol_kind::S_add_exp: // add_exp
        value.move< piranha::IrValue * > (YY_MOVE (s.value));
        break;

      default:
        break;
    }

    location = YY_MOVE (s.location);
  }

  // by_kind.
  Parser::by_kind::by_kind () YY_NOEXCEPT
    : kind_ (symbol_kind::S_YYEMPTY)
  {}

#if 201103L <= YY_CPLUSPLUS
  Parser::by_kind::by_kind (by_kind&& that) YY_NOEXCEPT
    : kind_ (that.kind_)
  {
    that.clear ();
  }
#endif

  Parser::by_kind::by_kind (const by_kind& that) YY_NOEXCEPT
    : kind_ (that.kind_)
  {}

  Parser::by_kind::by_kind (token_kind_type t) YY_NOEXCEPT
    : kind_ (yytranslate_ (t))
  {}



  void
  Parser::by_kind::clear () YY_NOEXCEPT
  {
    kind_ = symbol_kind::S_YYEMPTY;
  }

  void
  Parser::by_kind::move (by_kind& that)
  {
    kind_ = that.kind_;
    that.clear ();
  }

  Parser::symbol_kind_type
  Parser::by_kind::kind () const YY_NOEXCEPT
  {
    return kind_;
  }


  Parser::symbol_kind_type
  Parser::by_kind::type_get () const YY_NOEXCEPT
  {
    return this->kind ();
  }



  // by_state.
  Parser::by_state::by_state () YY_NOEXCEPT
    : state (empty_state)
  {}

  Parser::by_state::by_state (const by_state& that) YY_NOEXCEPT
    : state (that.state)
  {}

  void
  Parser::by_state::clear () YY_NOEXCEPT
  {
    state = empty_state;
  }

  void
  Parser::by_state::move (by_state& that)
  {
    state = that.state;
    that.clear ();
  }

  Parser::by_state::by_state (state_type s) YY_NOEXCEPT
    : state (s)
  {}

  Parser::symbol_kind_type
  Parser::by_state::kind () const YY_NOEXCEPT
  {
    if (state == empty_state)
      return symbol_kind::S_YYEMPTY;
    else
      return YY_CAST (symbol_kind_type, yystos_[+state]);
  }

  Parser::stack_symbol_type::stack_symbol_type ()
  {}

  Parser::stack_symbol_type::stack_symbol_type (YY_RVREF (stack_symbol_type) that)
    : super_type (YY_MOVE (that.state), YY_MOVE (that.location))
  {
    switch (that.kind ())
    {
      case symbol_kind::S_attribute: // attribute
        value.YY_MOVE_OR_COPY< piranha::IrAttribute * > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_port_declaration: // port_declaration
      case symbol_kind::S_port_status: // port_status
      case symbol_kind::S_port_value: // port_value
      case symbol_kind::S_port_connection: // port_connection
      case symbol_kind::S_documented_port_definition: // documented_port_definition
        value.YY_MOVE_OR_COPY< piranha::IrAttributeDefinition * > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_port_definitions: // port_definitions
        value.YY_MOVE_OR_COPY< piranha::IrAttributeDefinitionList * > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_connection_block: // connection_block
      case symbol_kind::S_attribute_list: // attribute_list
        value.YY_MOVE_OR_COPY< piranha::IrAttributeList * > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_import_statement: // import_statement
      case symbol_kind::S_import_statement_visibility: // import_statement_visibility
      case symbol_kind::S_import_statement_short_name: // import_statement_short_name
        value.YY_MOVE_OR_COPY< piranha::IrImportStatement * > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_node: // node
      case symbol_kind::S_node_member: // node_member
      case symbol_kind::S_inline_node_member: // inline_node_member
      case symbol_kind::S_inline_node: // inline_node
        value.YY_MOVE_OR_COPY< piranha::IrNode * > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_node_name: // node_name
      case symbol_kind::S_node_inline: // node_inline
      case symbol_kind::S_node_shadow: // node_shadow
      case symbol_kind::S_node_definition: // node_definition
      case symbol_kind::S_specific_node_definition: // specific_node_definition
      case symbol_kind::S_immediate_node_definition: // immediate_node_definition
      case symbol_kind::S_node_decorator: // node_decorator
        value.YY_MOVE_OR_COPY< piranha::IrNodeDefinition * > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_node_list: // node_list
        value.YY_MOVE_OR_COPY< piranha::IrNodeList * > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_type_name_namespace: // type_name_namespace
        value.YY_MOVE_OR_COPY< piranha::IrTokenInfoSet<std::string, 2> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_BOOL: // BOOL
        value.YY_MOVE_OR_COPY< piranha::IrTokenInfo_bool > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_FLOAT: // FLOAT
        value.YY_MOVE_OR_COPY< piranha::IrTokenInfo_float > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_INT: // INT
        value.YY_MOVE_OR_COPY< piranha::IrTokenInfo_int > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_CHAR: // CHAR
      case symbol_kind::S_IMPORT: // IMPORT
      case symbol_kind::S_AS: // AS
      case symbol_kind::S_NODE: // NODE
      case symbol_kind::S_INLINE: // INLINE
      case symbol_kind::S_ALIAS: // ALIAS
      case symbol_kind::S_INPUT: // INPUT
      case symbol_kind::S_OUTPUT: // OUTPUT
      case symbol_kind::S_MODIFY: // MODIFY
      case symbol_kind::S_TOGGLE: // TOGGLE
      case symbol_kind::S_LABEL: // LABEL
      case symbol_kind::S_STRING: // STRING
      case symbol_kind::S_DECORATOR: // DECORATOR
      case symbol_kind::S_PUBLIC: // PUBLIC
      case symbol_kind::S_PRIVATE: // PRIVATE
      case symbol_kind::S_BUILTIN_POINTER: // BUILTIN_POINTER
      case symbol_kind::S_NAMESPACE_POINTER: // NAMESPACE_POINTER
      case symbol_kind::S_UNRECOGNIZED: // UNRECOGNIZED
      case symbol_kind::S_OPERATOR: // OPERATOR
      case symbol_kind::S_MODULE: // MODULE
      case symbol_kind::S_AUTO: // AUTO
      case symbol_kind::S_27_: // '='
      case symbol_kind::S_28_: // '+'
      case symbol_kind::S_29_: // '-'
      case symbol_kind::S_30_: // '/'
      case symbol_kind::S_31_: // '*'
      case symbol_kind::S_32_: // '('
      case symbol_kind::S_33_: // ')'
      case symbol_kind::S_34_: // '{'
      case symbol_kind::S_35_: // '}'
      case symbol_kind::S_36_: // '['
      case symbol_kind::S_37_: // ']'
      case symbol_kind::S_38_: // ':'
      case symbol_kind::S_39_: // ';'
      case symbol_kind::S_40_: // ','
      case symbol_kind::S_41_: // '.'
      case symbol_kind::S_42_: // '^'
      case symbol_kind::S_type_name: // type_name
      case symbol_kind::S_standard_operator: // standard_operator
      case symbol_kind::S_string: // string
        value.YY_MOVE_OR_COPY< piranha::IrTokenInfo_string > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_label_value: // label_value
      case symbol_kind::S_value: // value
      case symbol_kind::S_constant: // constant
      case symbol_kind::S_atomic_value: // atomic_value
      case symbol_kind::S_primary_exp: // primary_exp
      case symbol_kind::S_data_access: // data_access
      case symbol_kind::S_unary_exp: // unary_exp
      case symbol_kind::S_mul_exp: // mul_exp
      case symbol_kind::S_add_exp: // add_exp
        value.YY_MOVE_OR_COPY< piranha::IrValue * > (YY_MOVE (that.value));
        break;

      default:
        break;
    }

#if 201103L <= YY_CPLUSPLUS
    // that is emptied.
    that.state = empty_state;
#endif
  }

  Parser::stack_symbol_type::stack_symbol_type (state_type s, YY_MOVE_REF (symbol_type) that)
    : super_type (s, YY_MOVE (that.location))
  {
    switch (that.kind ())
    {
      case symbol_kind::S_attribute: // attribute
        value.move< piranha::IrAttribute * > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_port_declaration: // port_declaration
      case symbol_kind::S_port_status: // port_status
      case symbol_kind::S_port_value: // port_value
      case symbol_kind::S_port_connection: // port_connection
      case symbol_kind::S_documented_port_definition: // documented_port_definition
        value.move< piranha::IrAttributeDefinition * > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_port_definitions: // port_definitions
        value.move< piranha::IrAttributeDefinitionList * > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_connection_block: // connection_block
      case symbol_kind::S_attribute_list: // attribute_list
        value.move< piranha::IrAttributeList * > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_import_statement: // import_statement
      case symbol_kind::S_import_statement_visibility: // import_statement_visibility
      case symbol_kind::S_import_statement_short_name: // import_statement_short_name
        value.move< piranha::IrImportStatement * > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_node: // node
      case symbol_kind::S_node_member: // node_member
      case symbol_kind::S_inline_node_member: // inline_node_member
      case symbol_kind::S_inline_node: // inline_node
        value.move< piranha::IrNode * > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_node_name: // node_name
      case symbol_kind::S_node_inline: // node_inline
      case symbol_kind::S_node_shadow: // node_shadow
      case symbol_kind::S_node_definition: // node_definition
      case symbol_kind::S_specific_node_definition: // specific_node_definition
      case symbol_kind::S_immediate_node_definition: // immediate_node_definition
      case symbol_kind::S_node_decorator: // node_decorator
        value.move< piranha::IrNodeDefinition * > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_node_list: // node_list
        value.move< piranha::IrNodeList * > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_type_name_namespace: // type_name_namespace
        value.move< piranha::IrTokenInfoSet<std::string, 2> > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_BOOL: // BOOL
        value.move< piranha::IrTokenInfo_bool > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_FLOAT: // FLOAT
        value.move< piranha::IrTokenInfo_float > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_INT: // INT
        value.move< piranha::IrTokenInfo_int > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_CHAR: // CHAR
      case symbol_kind::S_IMPORT: // IMPORT
      case symbol_kind::S_AS: // AS
      case symbol_kind::S_NODE: // NODE
      case symbol_kind::S_INLINE: // INLINE
      case symbol_kind::S_ALIAS: // ALIAS
      case symbol_kind::S_INPUT: // INPUT
      case symbol_kind::S_OUTPUT: // OUTPUT
      case symbol_kind::S_MODIFY: // MODIFY
      case symbol_kind::S_TOGGLE: // TOGGLE
      case symbol_kind::S_LABEL: // LABEL
      case symbol_kind::S_STRING: // STRING
      case symbol_kind::S_DECORATOR: // DECORATOR
      case symbol_kind::S_PUBLIC: // PUBLIC
      case symbol_kind::S_PRIVATE: // PRIVATE
      case symbol_kind::S_BUILTIN_POINTER: // BUILTIN_POINTER
      case symbol_kind::S_NAMESPACE_POINTER: // NAMESPACE_POINTER
      case symbol_kind::S_UNRECOGNIZED: // UNRECOGNIZED
      case symbol_kind::S_OPERATOR: // OPERATOR
      case symbol_kind::S_MODULE: // MODULE
      case symbol_kind::S_AUTO: // AUTO
      case symbol_kind::S_27_: // '='
      case symbol_kind::S_28_: // '+'
      case symbol_kind::S_29_: // '-'
      case symbol_kind::S_30_: // '/'
      case symbol_kind::S_31_: // '*'
      case symbol_kind::S_32_: // '('
      case symbol_kind::S_33_: // ')'
      case symbol_kind::S_34_: // '{'
      case symbol_kind::S_35_: // '}'
      case symbol_kind::S_36_: // '['
      case symbol_kind::S_37_: // ']'
      case symbol_kind::S_38_: // ':'
      case symbol_kind::S_39_: // ';'
      case symbol_kind::S_40_: // ','
      case symbol_kind::S_41_: // '.'
      case symbol_kind::S_42_: // '^'
      case symbol_kind::S_type_name: // type_name
      case symbol_kind::S_standard_operator: // standard_operator
      case symbol_kind::S_string: // string
        value.move< piranha::IrTokenInfo_string > (YY_MOVE (that.value));
        break;

      case symbol_kind::S_label_value: // label_value
      case symbol_kind::S_value: // value
      case symbol_kind::S_constant: // constant
      case symbol_kind::S_atomic_value: // atomic_value
      case symbol_kind::S_primary_exp: // primary_exp
      case symbol_kind::S_data_access: // data_access
      case symbol_kind::S_unary_exp: // unary_exp
      case symbol_kind::S_mul_exp: // mul_exp
      case symbol_kind::S_add_exp: // add_exp
        value.move< piranha::IrValue * > (YY_MOVE (that.value));
        break;

      default:
        break;
    }

    // that is emptied.
    that.kind_ = symbol_kind::S_YYEMPTY;
  }

#if YY_CPLUSPLUS < 201103L
  Parser::stack_symbol_type&
  Parser::stack_symbol_type::operator= (const stack_symbol_type& that)
  {
    state = that.state;
    switch (that.kind ())
    {
      case symbol_kind::S_attribute: // attribute
        value.copy< piranha::IrAttribute * > (that.value);
        break;

      case symbol_kind::S_port_declaration: // port_declaration
      case symbol_kind::S_port_status: // port_status
      case symbol_kind::S_port_value: // port_value
      case symbol_kind::S_port_connection: // port_connection
      case symbol_kind::S_documented_port_definition: // documented_port_definition
        value.copy< piranha::IrAttributeDefinition * > (that.value);
        break;

      case symbol_kind::S_port_definitions: // port_definitions
        value.copy< piranha::IrAttributeDefinitionList * > (that.value);
        break;

      case symbol_kind::S_connection_block: // connection_block
      case symbol_kind::S_attribute_list: // attribute_list
        value.copy< piranha::IrAttributeList * > (that.value);
        break;

      case symbol_kind::S_import_statement: // import_statement
      case symbol_kind::S_import_statement_visibility: // import_statement_visibility
      case symbol_kind::S_import_statement_short_name: // import_statement_short_name
        value.copy< piranha::IrImportStatement * > (that.value);
        break;

      case symbol_kind::S_node: // node
      case symbol_kind::S_node_member: // node_member
      case symbol_kind::S_inline_node_member: // inline_node_member
      case symbol_kind::S_inline_node: // inline_node
        value.copy< piranha::IrNode * > (that.value);
        break;

      case symbol_kind::S_node_name: // node_name
      case symbol_kind::S_node_inline: // node_inline
      case symbol_kind::S_node_shadow: // node_shadow
      case symbol_kind::S_node_definition: // node_definition
      case symbol_kind::S_specific_node_definition: // specific_node_definition
      case symbol_kind::S_immediate_node_definition: // immediate_node_definition
      case symbol_kind::S_node_decorator: // node_decorator
        value.copy< piranha::IrNodeDefinition * > (that.value);
        break;

      case symbol_kind::S_node_list: // node_list
        value.copy< piranha::IrNodeList * > (that.value);
        break;

      case symbol_kind::S_type_name_namespace: // type_name_namespace
        value.copy< piranha::IrTokenInfoSet<std::string, 2> > (that.value);
        break;

      case symbol_kind::S_BOOL: // BOOL
        value.copy< piranha::IrTokenInfo_bool > (that.value);
        break;

      case symbol_kind::S_FLOAT: // FLOAT
        value.copy< piranha::IrTokenInfo_float > (that.value);
        break;

      case symbol_kind::S_INT: // INT
        value.copy< piranha::IrTokenInfo_int > (that.value);
        break;

      case symbol_kind::S_CHAR: // CHAR
      case symbol_kind::S_IMPORT: // IMPORT
      case symbol_kind::S_AS: // AS
      case symbol_kind::S_NODE: // NODE
      case symbol_kind::S_INLINE: // INLINE
      case symbol_kind::S_ALIAS: // ALIAS
      case symbol_kind::S_INPUT: // INPUT
      case symbol_kind::S_OUTPUT: // OUTPUT
      case symbol_kind::S_MODIFY: // MODIFY
      case symbol_kind::S_TOGGLE: // TOGGLE
      case symbol_kind::S_LABEL: // LABEL
      case symbol_kind::S_STRING: // STRING
      case symbol_kind::S_DECORATOR: // DECORATOR
      case symbol_kind::S_PUBLIC: // PUBLIC
      case symbol_kind::S_PRIVATE: // PRIVATE
      case symbol_kind::S_BUILTIN_POINTER: // BUILTIN_POINTER
      case symbol_kind::S_NAMESPACE_POINTER: // NAMESPACE_POINTER
      case symbol_kind::S_UNRECOGNIZED: // UNRECOGNIZED
      case symbol_kind::S_OPERATOR: // OPERATOR
      case symbol_kind::S_MODULE: // MODULE
      case symbol_kind::S_AUTO: // AUTO
      case symbol_kind::S_27_: // '='
      case symbol_kind::S_28_: // '+'
      case symbol_kind::S_29_: // '-'
      case symbol_kind::S_30_: // '/'
      case symbol_kind::S_31_: // '*'
      case symbol_kind::S_32_: // '('
      case symbol_kind::S_33_: // ')'
      case symbol_kind::S_34_: // '{'
      case symbol_kind::S_35_: // '}'
      case symbol_kind::S_36_: // '['
      case symbol_kind::S_37_: // ']'
      case symbol_kind::S_38_: // ':'
      case symbol_kind::S_39_: // ';'
      case symbol_kind::S_40_: // ','
      case symbol_kind::S_41_: // '.'
      case symbol_kind::S_42_: // '^'
      case symbol_kind::S_type_name: // type_name
      case symbol_kind::S_standard_operator: // standard_operator
      case symbol_kind::S_string: // string
        value.copy< piranha::IrTokenInfo_string > (that.value);
        break;

      case symbol_kind::S_label_value: // label_value
      case symbol_kind::S_value: // value
      case symbol_kind::S_constant: // constant
      case symbol_kind::S_atomic_value: // atomic_value
      case symbol_kind::S_primary_exp: // primary_exp
      case symbol_kind::S_data_access: // data_access
      case symbol_kind::S_unary_exp: // unary_exp
      case symbol_kind::S_mul_exp: // mul_exp
      case symbol_kind::S_add_exp: // add_exp
        value.copy< piranha::IrValue * > (that.value);
        break;

      default:
        break;
    }

    location = that.location;
    return *this;
  }

  Parser::stack_symbol_type&
  Parser::stack_symbol_type::operator= (stack_symbol_type& that)
  {
    state = that.state;
    switch (that.kind ())
    {
      case symbol_kind::S_attribute: // attribute
        value.move< piranha::IrAttribute * > (that.value);
        break;

      case symbol_kind::S_port_declaration: // port_declaration
      case symbol_kind::S_port_status: // port_status
      case symbol_kind::S_port_value: // port_value
      case symbol_kind::S_port_connection: // port_connection
      case symbol_kind::S_documented_port_definition: // documented_port_definition
        value.move< piranha::IrAttributeDefinition * > (that.value);
        break;

      case symbol_kind::S_port_definitions: // port_definitions
        value.move< piranha::IrAttributeDefinitionList * > (that.value);
        break;

      case symbol_kind::S_connection_block: // connection_block
      case symbol_kind::S_attribute_list: // attribute_list
        value.move< piranha::IrAttributeList * > (that.value);
        break;

      case symbol_kind::S_import_statement: // import_statement
      case symbol_kind::S_import_statement_visibility: // import_statement_visibility
      case symbol_kind::S_import_statement_short_name: // import_statement_short_name
        value.move< piranha::IrImportStatement * > (that.value);
        break;

      case symbol_kind::S_node: // node
      case symbol_kind::S_node_member: // node_member
      case symbol_kind::S_inline_node_member: // inline_node_member
      case symbol_kind::S_inline_node: // inline_node
        value.move< piranha::IrNode * > (that.value);
        break;

      case symbol_kind::S_node_name: // node_name
      case symbol_kind::S_node_inline: // node_inline
      case symbol_kind::S_node_shadow: // node_shadow
      case symbol_kind::S_node_definition: // node_definition
      case symbol_kind::S_specific_node_definition: // specific_node_definition
      case symbol_kind::S_immediate_node_definition: // immediate_node_definition
      case symbol_kind::S_node_decorator: // node_decorator
        value.move< piranha::IrNodeDefinition * > (that.value);
        break;

      case symbol_kind::S_node_list: // node_list
        value.move< piranha::IrNodeList * > (that.value);
        break;

      case symbol_kind::S_type_name_namespace: // type_name_namespace
        value.move< piranha::IrTokenInfoSet<std::string, 2> > (that.value);
        break;

      case symbol_kind::S_BOOL: // BOOL
        value.move< piranha::IrTokenInfo_bool > (that.value);
        break;

      case symbol_kind::S_FLOAT: // FLOAT
        value.move< piranha::IrTokenInfo_float > (that.value);
        break;

      case symbol_kind::S_INT: // INT
        value.move< piranha::IrTokenInfo_int > (that.value);
        break;

      case symbol_kind::S_CHAR: // CHAR
      case symbol_kind::S_IMPORT: // IMPORT
      case symbol_kind::S_AS: // AS
      case symbol_kind::S_NODE: // NODE
      case symbol_kind::S_INLINE: // INLINE
      case symbol_kind::S_ALIAS: // ALIAS
      case symbol_kind::S_INPUT: // INPUT
      case symbol_kind::S_OUTPUT: // OUTPUT
      case symbol_kind::S_MODIFY: // MODIFY
      case symbol_kind::S_TOGGLE: // TOGGLE
      case symbol_kind::S_LABEL: // LABEL
      case symbol_kind::S_STRING: // STRING
      case symbol_kind::S_DECORATOR: // DECORATOR
      case symbol_kind::S_PUBLIC: // PUBLIC
      case symbol_kind::S_PRIVATE: // PRIVATE
      case symbol_kind::S_BUILTIN_POINTER: // BUILTIN_POINTER
      case symbol_kind::S_NAMESPACE_POINTER: // NAMESPACE_POINTER
      case symbol_kind::S_UNRECOGNIZED: // UNRECOGNIZED
      case symbol_kind::S_OPERATOR: // OPERATOR
      case symbol_kind::S_MODULE: // MODULE
      case symbol_kind::S_AUTO: // AUTO
      case symbol_kind::S_27_: // '='
      case symbol_kind::S_28_: // '+'
      case symbol_kind::S_29_: // '-'
      case symbol_kind::S_30_: // '/'
      case symbol_kind::S_31_: // '*'
      case symbol_kind::S_32_: // '('
      case symbol_kind::S_33_: // ')'
      case symbol_kind::S_34_: // '{'
      case symbol_kind::S_35_: // '}'
      case symbol_kind::S_36_: // '['
      case symbol_kind::S_37_: // ']'
      case symbol_kind::S_38_: // ':'
      case symbol_kind::S_39_: // ';'
      case symbol_kind::S_40_: // ','
      case symbol_kind::S_41_: // '.'
      case symbol_kind::S_42_: // '^'
      case symbol_kind::S_type_name: // type_name
      case symbol_kind::S_standard_operator: // standard_operator
      case symbol_kind::S_string: // string
        value.move< piranha::IrTokenInfo_string > (that.value);
        break;

      case symbol_kind::S_label_value: // label_value
      case symbol_kind::S_value: // value
      case symbol_kind::S_constant: // constant
      case symbol_kind::S_atomic_value: // atomic_value
      case symbol_kind::S_primary_exp: // primary_exp
      case symbol_kind::S_data_access: // data_access
      case symbol_kind::S_unary_exp: // unary_exp
      case symbol_kind::S_mul_exp: // mul_exp
      case symbol_kind::S_add_exp: // add_exp
        value.move< piranha::IrValue * > (that.value);
        break;

      default:
        break;
    }

    location = that.location;
    // that is emptied.
    that.state = empty_state;
    return *this;
  }
#endif

  template <typename Base>
  void
  Parser::yy_destroy_ (const char* yymsg, basic_symbol<Base>& yysym) const
  {
    if (yymsg)
      YY_SYMBOL_PRINT (yymsg, yysym);
  }

#if YYDEBUG
  template <typename Base>
  void
  Parser::yy_print_ (std::ostream& yyo, const basic_symbol<Base>& yysym) const
  {
    std::ostream& yyoutput = yyo;
    YY_USE (yyoutput);
    if (yysym.empty ())
      yyo << "empty symbol";
    else
      {
        symbol_kind_type yykind = yysym.kind ();
        yyo << (yykind < YYNTOKENS ? "token" : "nterm")
            << ' ' << yysym.name () << " ("
            << yysym.location << ": ";
        YY_USE (yykind);
        yyo << ')';
      }
  }
#endif

  void
  Parser::yypush_ (const char* m, YY_MOVE_REF (stack_symbol_type) sym)
  {
    if (m)
      YY_SYMBOL_PRINT (m, sym);
    yystack_.push (YY_MOVE (sym));
  }

  void
  Parser::yypush_ (const char* m, state_type s, YY_MOVE_REF (symbol_type) sym)
  {
#if 201103L <= YY_CPLUSPLUS
    yypush_ (m, stack_symbol_type (s, std::move (sym)));
#else
    stack_symbol_type ss (s, sym);
    yypush_ (m, ss);
#endif
  }

  void
  Parser::yypop_ (int n) YY_NOEXCEPT
  {
    yystack_.pop (n);
  }

#if YYDEBUG
  std::ostream&
  Parser::debug_stream () const
  {
    return *yycdebug_;
  }

  void
  Parser::set_debug_stream (std::ostream& o)
  {
    yycdebug_ = &o;
  }


  Parser::debug_level_type
  Parser::debug_level () const
  {
    return yydebug_;
  }

  void
  Parser::set_debug_level (debug_level_type l)
  {
    yydebug_ = l;
  }
#endif // YYDEBUG

  Parser::state_type
  Parser::yy_lr_goto_state_ (state_type yystate, int yysym)
  {
    int yyr = yypgoto_[yysym - YYNTOKENS] + yystate;
    if (0 <= yyr && yyr <= yylast_ && yycheck_[yyr] == yystate)
      return yytable_[yyr];
    else
      return yydefgoto_[yysym - YYNTOKENS];
  }

  bool
  Parser::yy_pact_value_is_default_ (int yyvalue) YY_NOEXCEPT
  {
    return yyvalue == yypact_ninf_;
  }

  bool
  Parser::yy_table_value_is_error_ (int yyvalue) YY_NOEXCEPT
  {
    return yyvalue == yytable_ninf_;
  }

  int
  Parser::operator() ()
  {
    return parse ();
  }

  int
  Parser::parse ()
  {
    int yyn;
    /// Length of the RHS of the rule being reduced.
    int yylen = 0;

    // Error handling.
    int yynerrs_ = 0;
    int yyerrstatus_ = 0;

    /// The lookahead symbol.
    symbol_type yyla;

    /// The locations where the error started and ended.
    stack_symbol_type yyerror_range[3];

    /// The return value of parse ().
    int yyresult;

#if YY_EXCEPTIONS
    try
#endif // YY_EXCEPTIONS
      {
    YYCDEBUG << "Starting parse\n";


    /* Initialize the stack.  The initial state will be set in
       yynewstate, since the latter expects the semantical and the
       location values to have been already stored, initialize these
       stacks with a primary value.  */
    yystack_.clear ();
    yypush_ (YY_NULLPTR, 0, YY_MOVE (yyla));

  /*-----------------------------------------------.
  | yynewstate -- push a new symbol on the stack.  |
  `-----------------------------------------------*/
  yynewstate:
    YYCDEBUG << "Entering state " << int (yystack_[0].state) << '\n';
    YY_STACK_PRINT ();

    // Accept?
    if (yystack_[0].state == yyfinal_)
      YYACCEPT;

    goto yybackup;


  /*-----------.
  | yybackup.  |
  `-----------*/
  yybackup:
    // Try to take a decision without lookahead.
    yyn = yypact_[+yystack_[0].state];
    if (yy_pact_value_is_default_ (yyn))
      goto yydefault;

    // Read a lookahead token.
    if (yyla.empty ())
      {
        YYCDEBUG << "Reading a token\n";
#if YY_EXCEPTIONS
        try
#endif // YY_EXCEPTIONS
          {
            yyla.kind_ = yytranslate_ (yylex (&yyla.value, &yyla.location));
          }
#if YY_EXCEPTIONS
        catch (const syntax_error& yyexc)
          {
            YYCDEBUG << "Caught exception: " << yyexc.what() << '\n';
            error (yyexc);
            goto yyerrlab1;
          }
#endif // YY_EXCEPTIONS
      }
    YY_SYMBOL_PRINT ("Next token is", yyla);

    if (yyla.kind () == symbol_kind::S_YYerror)
    {
      // The scanner already issued an error message, process directly
      // to error recovery.  But do not keep the error token as
      // lookahead, it is too special and may lead us to an endless
      // loop in error recovery. */
      yyla.kind_ = symbol_kind::S_YYUNDEF;
      goto yyerrlab1;
    }

    /* If the proper action on seeing token YYLA.TYPE is to reduce or
       to detect an error, take that action.  */
    yyn += yyla.kind ();
    if (yyn < 0 || yylast_ < yyn || yycheck_[yyn] != yyla.kind ())
      {
        goto yydefault;
      }

    // Reduce or error.
    yyn = yytable_[yyn];
    if (yyn <= 0)
      {
        if (yy_table_value_is_error_ (yyn))
          goto yyerrlab;
        yyn = -yyn;
        goto yyreduce;
      }

    // Count tokens shifted since error; after three, turn off error status.
    if (yyerrstatus_)
      --yyerrstatus_;

    // Shift the lookahead token.
    yypush_ ("Shifting", state_type (yyn), YY_MOVE (yyla));
    goto yynewstate;


  /*-----------------------------------------------------------.
  | yydefault -- do the default action for the current state.  |
  `-----------------------------------------------------------*/
  yydefault:
    yyn = yydefact_[+yystack_[0].state];
    if (yyn == 0)
      goto yyerrlab;
    goto yyreduce;


  /*-----------------------------.
  | yyreduce -- do a reduction.  |
  `-----------------------------*/
  yyreduce:
    yylen = yyr2_[yyn];
    {
      stack_symbol_type yylhs;
      yylhs.state = yy_lr_goto_state_ (yystack_[yylen].state, yyr1_[yyn]);
      /* Variants are always initialized to an empty instance of the
         correct type. The default '$$ = $1' action is NOT applied
         when using variants.  */
      switch (yyr1_[yyn])
    {
      case symbol_kind::S_attribute: // attribute
        yylhs.value.emplace< piranha::IrAttribute * > ();
        break;

      case symbol_kind::S_port_declaration: // port_declaration
      case symbol_kind::S_port_status: // port_status
      case symbol_kind::S_port_value: // port_value
      case symbol_kind::S_port_connection: // port_connection
      case symbol_kind::S_documented_port_definition: // documented_port_definition
        yylhs.value.emplace< piranha::IrAttributeDefinition * > ();
        break;

      case symbol_kind::S_port_definitions: // port_definitions
        yylhs.value.emplace< piranha::IrAttributeDefinitionList * > ();
        break;

      case symbol_kind::S_connection_block: // connection_block
      case symbol_kind::S_attribute_list: // attribute_list
        yylhs.value.emplace< piranha::IrAttributeList * > ();
        break;

      case symbol_kind::S_import_statement: // import_statement
      case symbol_kind::S_import_statement_visibility: // import_statement_visibility
      case symbol_kind::S_import_statement_short_name: // import_statement_short_name
        yylhs.value.emplace< piranha::IrImportStatement * > ();
        break;

      case symbol_kind::S_node: // node
      case symbol_kind::S_node_member: // node_member
      case symbol_kind::S_inline_node_member: // inline_node_member
      case symbol_kind::S_inline_node: // inline_node
        yylhs.value.emplace< piranha::IrNode * > ();
        break;

      case symbol_kind::S_node_name: // node_name
      case symbol_kind::S_node_inline: // node_inline
      case symbol_kind::S_node_shadow: // node_shadow
      case symbol_kind::S_node_definition: // node_definition
      case symbol_kind::S_specific_node_definition: // specific_node_definition
      case symbol_kind::S_immediate_node_definition: // immediate_node_definition
      case symbol_kind::S_node_decorator: // node_decorator
        yylhs.value.emplace< piranha::IrNodeDefinition * > ();
        break;

      case symbol_kind::S_node_list: // node_list
        yylhs.value.emplace< piranha::IrNodeList * > ();
        break;

      case symbol_kind::S_type_name_namespace: // type_name_namespace
        yylhs.value.emplace< piranha::IrTokenInfoSet<std::string, 2> > ();
        break;

      case symbol_kind::S_BOOL: // BOOL
        yylhs.value.emplace< piranha::IrTokenInfo_bool > ();
        break;

      case symbol_kind::S_FLOAT: // FLOAT
        yylhs.value.emplace< piranha::IrTokenInfo_float > ();
        break;

      case symbol_kind::S_INT: // INT
        yylhs.value.emplace< piranha::IrTokenInfo_int > ();
        break;

      case symbol_kind::S_CHAR: // CHAR
      case symbol_kind::S_IMPORT: // IMPORT
      case symbol_kind::S_AS: // AS
      case symbol_kind::S_NODE: // NODE
      case symbol_kind::S_INLINE: // INLINE
      case symbol_kind::S_ALIAS: // ALIAS
      case symbol_kind::S_INPUT: // INPUT
      case symbol_kind::S_OUTPUT: // OUTPUT
      case symbol_kind::S_MODIFY: // MODIFY
      case symbol_kind::S_TOGGLE: // TOGGLE
      case symbol_kind::S_LABEL: // LABEL
      case symbol_kind::S_STRING: // STRING
      case symbol_kind::S_DECORATOR: // DECORATOR
      case symbol_kind::S_PUBLIC: // PUBLIC
      case symbol_kind::S_PRIVATE: // PRIVATE
      case symbol_kind::S_BUILTIN_POINTER: // BUILTIN_POINTER
      case symbol_kind::S_NAMESPACE_POINTER: // NAMESPACE_POINTER
      case symbol_kind::S_UNRECOGNIZED: // UNRECOGNIZED
      case symbol_kind::S_OPERATOR: // OPERATOR
      case symbol_kind::S_MODULE: // MODULE
      case symbol_kind::S_AUTO: // AUTO
      case symbol_kind::S_27_: // '='
      case symbol_kind::S_28_: // '+'
      case symbol_kind::S_29_: // '-'
      case symbol_kind::S_30_: // '/'
      case symbol_kind::S_31_: // '*'
      case symbol_kind::S_32_: // '('
      case symbol_kind::S_33_: // ')'
      case symbol_kind::S_34_: // '{'
      case symbol_kind::S_35_: // '}'
      case symbol_kind::S_36_: // '['
      case symbol_kind::S_37_: // ']'
      case symbol_kind::S_38_: // ':'
      case symbol_kind::S_39_: // ';'
      case symbol_kind::S_40_: // ','
      case symbol_kind::S_41_: // '.'
      case symbol_kind::S_42_: // '^'
      case symbol_kind::S_type_name: // type_name
      case symbol_kind::S_standard_operator: // standard_operator
      case symbol_kind::S_string: // string
        yylhs.value.emplace< piranha::IrTokenInfo_string > ();
        break;

      case symbol_kind::S_label_value: // label_value
      case symbol_kind::S_value: // value
      case symbol_kind::S_constant: // constant
      case symbol_kind::S_atomic_value: // atomic_value
      case symbol_kind::S_primary_exp: // primary_exp
      case symbol_kind::S_data_access: // data_access
      case symbol_kind::S_unary_exp: // unary_exp
      case symbol_kind::S_mul_exp: // mul_exp
      case symbol_kind::S_add_exp: // add_exp
        yylhs.value.emplace< piranha::IrValue * > ();
        break;

      default:
        break;
    }


      // Default location.
      {
        stack_type::slice range (yystack_, yylen);
        YYLLOC_DEFAULT (yylhs.location, range, yylen);
        yyerror_range[1].location = yylhs.location;
      }

      // Perform the reduction.
      YY_REDUCE_PRINT (yyn);
#if YY_EXCEPTIONS
      try
#endif // YY_EXCEPTIONS
        {
          switch (yyn)
            {
  case 4: // decorator: DECORATOR LABEL ':' string
#line 173 "flex-bison/specification.y"
                                        { /* void */ }
#line 1474 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 5: // decorator_list: decorator
#line 177 "flex-bison/specification.y"
                                        { /* void */ }
#line 1480 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 6: // decorator_list: decorator_list decorator
#line 178 "flex-bison/specification.y"
                                        { /* void */ }
#line 1486 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 7: // statement: node_member
#line 182 "flex-bison/specification.y"
                                        { driver.addNode(yystack_[0].value.as < piranha::IrNode * > ()); }
#line 1492 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 8: // statement: import_statement_short_name
#line 183 "flex-bison/specification.y"
                                        { driver.addImportStatement(yystack_[0].value.as < piranha::IrImportStatement * > ()); }
#line 1498 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 9: // statement: node_decorator
#line 184 "flex-bison/specification.y"
                                        { driver.addNodeDefinition(yystack_[0].value.as < piranha::IrNodeDefinition * > ()); }
#line 1504 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 10: // statement: MODULE '{' decorator_list '}'
#line 185 "flex-bison/specification.y"
                                        { /* void */ }
#line 1510 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 11: // statement_list: statement
#line 189 "flex-bison/specification.y"
                                        { /* void */ }
#line 1516 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 12: // statement_list: statement_list statement
#line 190 "flex-bison/specification.y"
                                        { /* void */ }
#line 1522 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 13: // statement_list: statement_list error
#line 191 "flex-bison/specification.y"
                                        { /* void */ }
#line 1528 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 14: // import_statement: IMPORT string
#line 195 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrImportStatement * > () = TRACK(new IrImportStatement(yystack_[0].value.as < piranha::IrTokenInfo_string > ())); }
#line 1534 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 15: // import_statement: IMPORT LABEL
#line 196 "flex-bison/specification.y"
                                        { 
                                            yylhs.value.as < piranha::IrImportStatement * > () = TRACK(new IrImportStatement(yystack_[0].value.as < piranha::IrTokenInfo_string > ()));

                                            /* The name is a valid label so it can be used as a short name */
                                            yylhs.value.as < piranha::IrImportStatement * > ()->setShortName(yystack_[0].value.as < piranha::IrTokenInfo_string > ()); 
                                        }
#line 1545 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 16: // import_statement_visibility: PUBLIC import_statement
#line 205 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrImportStatement * > () = yystack_[0].value.as < piranha::IrImportStatement * > (); yylhs.value.as < piranha::IrImportStatement * > ()->setVisibility(IrVisibility::Public); }
#line 1551 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 17: // import_statement_visibility: PRIVATE import_statement
#line 206 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrImportStatement * > () = yystack_[0].value.as < piranha::IrImportStatement * > (); yylhs.value.as < piranha::IrImportStatement * > ()->setVisibility(IrVisibility::Private); }
#line 1557 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 18: // import_statement_visibility: import_statement
#line 207 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrImportStatement * > () = yystack_[0].value.as < piranha::IrImportStatement * > (); yylhs.value.as < piranha::IrImportStatement * > ()->setVisibility(IrVisibility::Default); }
#line 1563 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 19: // import_statement_short_name: import_statement_visibility AS LABEL
#line 211 "flex-bison/specification.y"
                                                { yylhs.value.as < piranha::IrImportStatement * > () = yystack_[2].value.as < piranha::IrImportStatement * > (); yylhs.value.as < piranha::IrImportStatement * > ()->setShortName(yystack_[0].value.as < piranha::IrTokenInfo_string > ()); }
#line 1569 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 20: // import_statement_short_name: import_statement_visibility
#line 212 "flex-bison/specification.y"
                                                { yylhs.value.as < piranha::IrImportStatement * > () = yystack_[0].value.as < piranha::IrImportStatement * > (); }
#line 1575 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 21: // type_name: LABEL
#line 216 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrTokenInfo_string > () = yystack_[0].value.as < piranha::IrTokenInfo_string > (); }
#line 1581 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 22: // type_name: OPERATOR standard_operator
#line 217 "flex-bison/specification.y"
                                        {
                                            IrTokenInfo_string info = yystack_[1].value.as < piranha::IrTokenInfo_string > ();
                                            info.combine(&yystack_[0].value.as < piranha::IrTokenInfo_string > ());
                                            info.data = std::string("operator") + yystack_[0].value.as < piranha::IrTokenInfo_string > ().data;
                                            yylhs.value.as < piranha::IrTokenInfo_string > () = info;                                        
                                        }
#line 1592 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 23: // type_name_namespace: type_name
#line 226 "flex-bison/specification.y"
                                        { 
                                            IrTokenInfoSet<std::string, 2> set; 
                                            set.data[1] = yystack_[0].value.as < piranha::IrTokenInfo_string > (); 
                                            yylhs.value.as < piranha::IrTokenInfoSet<std::string, 2> > () = set; 
                                        }
#line 1602 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 24: // type_name_namespace: LABEL NAMESPACE_POINTER type_name
#line 231 "flex-bison/specification.y"
                                        { 
                                            IrTokenInfoSet<std::string, 2> set; 
                                            set.data[0] = yystack_[2].value.as < piranha::IrTokenInfo_string > (); 
                                            set.data[1] = yystack_[0].value.as < piranha::IrTokenInfo_string > (); 
                                            yylhs.value.as < piranha::IrTokenInfoSet<std::string, 2> > () = set; 
                                        }
#line 1613 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 25: // type_name_namespace: NAMESPACE_POINTER type_name
#line 237 "flex-bison/specification.y"
                                        { 
                                            IrTokenInfoSet<std::string, 2> set; 
                                            set.data[0].specified = true; 
                                            set.data[1] = yystack_[0].value.as < piranha::IrTokenInfo_string > (); 
                                            yylhs.value.as < piranha::IrTokenInfoSet<std::string, 2> > () = set; 
                                        }
#line 1624 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 26: // node: type_name_namespace LABEL connection_block
#line 246 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrNode * > () = TRACK(new IrNode(yystack_[2].value.as < piranha::IrTokenInfoSet<std::string, 2> > ().data[1], yystack_[1].value.as < piranha::IrTokenInfo_string > (), yystack_[0].value.as < piranha::IrAttributeList * > (), yystack_[2].value.as < piranha::IrTokenInfoSet<std::string, 2> > ().data[0])); }
#line 1630 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 27: // node: type_name_namespace connection_block
#line 247 "flex-bison/specification.y"
                                                        {
                                                            IrTokenInfo_string name;
                                                            name.specified = false;
                                                            name.data = "";

                                                            yylhs.value.as < piranha::IrNode * > () = TRACK(new IrNode(yystack_[1].value.as < piranha::IrTokenInfoSet<std::string, 2> > ().data[1], name, yystack_[0].value.as < piranha::IrAttributeList * > (), yystack_[1].value.as < piranha::IrTokenInfoSet<std::string, 2> > ().data[0]));
                                                        }
#line 1642 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 28: // node_member: node
#line 257 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrNode * > () = yystack_[0].value.as < piranha::IrNode * > (); }
#line 1648 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 29: // node_member: data_access '.' node
#line 258 "flex-bison/specification.y"
                                                        {
                                                            yylhs.value.as < piranha::IrNode * > () = yystack_[0].value.as < piranha::IrNode * > ();
                                                            yylhs.value.as < piranha::IrNode * > ()->setThis(yystack_[2].value.as < piranha::IrValue * > ());
                                                        }
#line 1657 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 30: // node_list: node_member
#line 265 "flex-bison/specification.y"
                                                        {
                                                            yylhs.value.as < piranha::IrNodeList * > () = TRACK(new IrNodeList());
                                                            yylhs.value.as < piranha::IrNodeList * > ()->add(yystack_[0].value.as < piranha::IrNode * > ());
                                                        }
#line 1666 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 31: // node_list: node_list node_member
#line 269 "flex-bison/specification.y"
                                                        { 
                                                            yylhs.value.as < piranha::IrNodeList * > () = yystack_[1].value.as < piranha::IrNodeList * > ();
                                                            yystack_[1].value.as < piranha::IrNodeList * > ()->add(yystack_[0].value.as < piranha::IrNode * > ());  
                                                        }
#line 1675 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 32: // node_list: node_list error
#line 273 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrNodeList * > () = yystack_[1].value.as < piranha::IrNodeList * > (); }
#line 1681 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 33: // standard_operator: '-'
#line 277 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrTokenInfo_string > () = yystack_[0].value.as < piranha::IrTokenInfo_string > (); }
#line 1687 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 34: // standard_operator: '+'
#line 278 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrTokenInfo_string > () = yystack_[0].value.as < piranha::IrTokenInfo_string > (); }
#line 1693 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 35: // standard_operator: '/'
#line 279 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrTokenInfo_string > () = yystack_[0].value.as < piranha::IrTokenInfo_string > (); }
#line 1699 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 36: // standard_operator: '*'
#line 280 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrTokenInfo_string > () = yystack_[0].value.as < piranha::IrTokenInfo_string > (); }
#line 1705 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 37: // node_name: NODE LABEL
#line 284 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrNodeDefinition * > () = TRACK(new IrNodeDefinition(yystack_[0].value.as < piranha::IrTokenInfo_string > ())); }
#line 1711 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 38: // node_name: NODE OPERATOR standard_operator
#line 285 "flex-bison/specification.y"
                                                        {
                                                            IrTokenInfo_string info = yystack_[1].value.as < piranha::IrTokenInfo_string > ();
                                                            info.combine(&yystack_[0].value.as < piranha::IrTokenInfo_string > ());
                                                            info.data = std::string("operator") + yystack_[0].value.as < piranha::IrTokenInfo_string > ().data;
                                                            yylhs.value.as < piranha::IrNodeDefinition * > () = TRACK(new IrNodeDefinition(info));
                                                        }
#line 1722 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 39: // node_inline: INLINE node_name
#line 294 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrNodeDefinition * > () = yystack_[0].value.as < piranha::IrNodeDefinition * > (); yylhs.value.as < piranha::IrNodeDefinition * > ()->setIsInline(true); }
#line 1728 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 40: // node_inline: node_name
#line 295 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrNodeDefinition * > () = yystack_[0].value.as < piranha::IrNodeDefinition * > (); yylhs.value.as < piranha::IrNodeDefinition * > ()->setIsInline(false); }
#line 1734 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 41: // node_shadow: node_inline BUILTIN_POINTER LABEL
#line 299 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrNodeDefinition * > () = yystack_[2].value.as < piranha::IrNodeDefinition * > (); yylhs.value.as < piranha::IrNodeDefinition * > ()->setBuiltinName(yystack_[0].value.as < piranha::IrTokenInfo_string > ()); yylhs.value.as < piranha::IrNodeDefinition * > ()->setDefinesBuiltin(true); }
#line 1740 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 42: // node_shadow: node_inline
#line 300 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrNodeDefinition * > () = yystack_[0].value.as < piranha::IrNodeDefinition * > (); yylhs.value.as < piranha::IrNodeDefinition * > ()->setDefinesBuiltin(false); }
#line 1746 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 43: // node_definition: node_shadow port_definitions '}'
#line 304 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrNodeDefinition * > () = yystack_[2].value.as < piranha::IrNodeDefinition * > (); yylhs.value.as < piranha::IrNodeDefinition * > ()->setAttributeDefinitionList(yystack_[1].value.as < piranha::IrAttributeDefinitionList * > ()); yylhs.value.as < piranha::IrNodeDefinition * > ()->setBody(nullptr); }
#line 1752 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 44: // node_definition: node_shadow port_definitions node_list '}'
#line 305 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrNodeDefinition * > () = yystack_[3].value.as < piranha::IrNodeDefinition * > (); yylhs.value.as < piranha::IrNodeDefinition * > ()->setAttributeDefinitionList(yystack_[2].value.as < piranha::IrAttributeDefinitionList * > ()); yylhs.value.as < piranha::IrNodeDefinition * > ()->setBody(yystack_[1].value.as < piranha::IrNodeList * > ()); }
#line 1758 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 45: // node_definition: node_shadow error port_definitions '}'
#line 306 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrNodeDefinition * > () = yystack_[3].value.as < piranha::IrNodeDefinition * > (); yylhs.value.as < piranha::IrNodeDefinition * > ()->setAttributeDefinitionList(yystack_[1].value.as < piranha::IrAttributeDefinitionList * > ()); yylhs.value.as < piranha::IrNodeDefinition * > ()->setBody(nullptr); }
#line 1764 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 46: // node_definition: node_shadow error port_definitions node_list '}'
#line 307 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrNodeDefinition * > () = yystack_[4].value.as < piranha::IrNodeDefinition * > (); yylhs.value.as < piranha::IrNodeDefinition * > ()->setAttributeDefinitionList(yystack_[2].value.as < piranha::IrAttributeDefinitionList * > ()); yylhs.value.as < piranha::IrNodeDefinition * > ()->setBody(yystack_[1].value.as < piranha::IrNodeList * > ()); }
#line 1770 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 47: // node_definition: error port_definitions '}'
#line 308 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrNodeDefinition * > () = nullptr; yyerrok; }
#line 1776 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 48: // node_definition: error port_definitions node_list '}'
#line 309 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrNodeDefinition * > () = nullptr; yyerrok; }
#line 1782 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 49: // specific_node_definition: node_definition
#line 313 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrNodeDefinition * > () = yystack_[0].value.as < piranha::IrNodeDefinition * > (); }
#line 1788 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 50: // specific_node_definition: PRIVATE node_definition
#line 314 "flex-bison/specification.y"
                                                        { 
                                                            if (yystack_[0].value.as < piranha::IrNodeDefinition * > () != nullptr) {
                                                                yylhs.value.as < piranha::IrNodeDefinition * > () = yystack_[0].value.as < piranha::IrNodeDefinition * > (); 
                                                                yylhs.value.as < piranha::IrNodeDefinition * > ()->setVisibility(IrVisibility::Private); 
                                                                yylhs.value.as < piranha::IrNodeDefinition * > ()->setScopeToken(yystack_[1].value.as < piranha::IrTokenInfo_string > ());
                                                            }
                                                            else yylhs.value.as < piranha::IrNodeDefinition * > () = nullptr;
                                                        }
#line 1801 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 51: // specific_node_definition: PUBLIC node_definition
#line 322 "flex-bison/specification.y"
                                                        { 
                                                            if (yystack_[0].value.as < piranha::IrNodeDefinition * > () != nullptr) {
                                                                yylhs.value.as < piranha::IrNodeDefinition * > () = yystack_[0].value.as < piranha::IrNodeDefinition * > ();
                                                                yylhs.value.as < piranha::IrNodeDefinition * > ()->setVisibility(IrVisibility::Public);
                                                                yylhs.value.as < piranha::IrNodeDefinition * > ()->setScopeToken(yystack_[1].value.as < piranha::IrTokenInfo_string > ());
                                                            }
                                                            else yylhs.value.as < piranha::IrNodeDefinition * > () = nullptr;
                                                        }
#line 1814 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 52: // immediate_node_definition: AUTO specific_node_definition
#line 333 "flex-bison/specification.y"
                                                        {   
                                                            yylhs.value.as < piranha::IrNodeDefinition * > () = yystack_[0].value.as < piranha::IrNodeDefinition * > ();
                                                            if (yylhs.value.as < piranha::IrNodeDefinition * > () != nullptr) {
                                                                IrNode *newNode = TRACK(new IrNode(*(yystack_[0].value.as < piranha::IrNodeDefinition * > ()->getNameToken()), yystack_[0].value.as < piranha::IrNodeDefinition * > (), TRACK(new IrAttributeList())));
                                                                driver.addNode(newNode);
                                                            }
                                                        }
#line 1826 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 53: // immediate_node_definition: specific_node_definition
#line 340 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrNodeDefinition * > () = yystack_[0].value.as < piranha::IrNodeDefinition * > (); }
#line 1832 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 54: // node_decorator: decorator_list immediate_node_definition
#line 344 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrNodeDefinition * > () = yystack_[0].value.as < piranha::IrNodeDefinition * > (); }
#line 1838 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 55: // node_decorator: immediate_node_definition
#line 345 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrNodeDefinition * > () = yystack_[0].value.as < piranha::IrNodeDefinition * > (); }
#line 1844 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 56: // port_definitions: '{'
#line 349 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrAttributeDefinitionList * > () = TRACK(new IrAttributeDefinitionList()); }
#line 1850 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 57: // port_definitions: port_definitions documented_port_definition ';'
#line 350 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrAttributeDefinitionList * > () = yystack_[2].value.as < piranha::IrAttributeDefinitionList * > (); yylhs.value.as < piranha::IrAttributeDefinitionList * > ()->addDefinition(yystack_[1].value.as < piranha::IrAttributeDefinition * > ()); }
#line 1856 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 58: // port_definitions: port_definitions documented_port_definition error
#line 351 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrAttributeDefinitionList * > () = yystack_[2].value.as < piranha::IrAttributeDefinitionList * > (); yylhs.value.as < piranha::IrAttributeDefinitionList * > ()->addDefinition(yystack_[1].value.as < piranha::IrAttributeDefinition * > ()); }
#line 1862 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 59: // port_definitions: port_definitions error ';'
#line 352 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrAttributeDefinitionList * > () = yystack_[2].value.as < piranha::IrAttributeDefinitionList * > (); }
#line 1868 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 60: // port_declaration: INPUT LABEL
#line 356 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrAttributeDefinition * > () = TRACK(new IrAttributeDefinition(yystack_[1].value.as < piranha::IrTokenInfo_string > (), yystack_[0].value.as < piranha::IrTokenInfo_string > (), IrAttributeDefinition::Direction::Input)); }
#line 1874 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 61: // port_declaration: OUTPUT LABEL
#line 357 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrAttributeDefinition * > () = TRACK(new IrAttributeDefinition(yystack_[1].value.as < piranha::IrTokenInfo_string > (), yystack_[0].value.as < piranha::IrTokenInfo_string > (), IrAttributeDefinition::Direction::Output)); }
#line 1880 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 62: // port_declaration: MODIFY LABEL
#line 358 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrAttributeDefinition * > () = TRACK(new IrAttributeDefinition(yystack_[1].value.as < piranha::IrTokenInfo_string > (), yystack_[0].value.as < piranha::IrTokenInfo_string > (), IrAttributeDefinition::Direction::Modify)); }
#line 1886 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 63: // port_declaration: TOGGLE LABEL
#line 359 "flex-bison/specification.y"
                                                        { yylhs.value.as < piranha::IrAttributeDefinition * > () = TRACK(new IrAttributeDefinition(yystack_[1].value.as < piranha::IrTokenInfo_string > (), yystack_[0].value.as < piranha::IrTokenInfo_string > (), IrAttributeDefinition::Direction::Toggle)); }
#line 1892 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 64: // port_status: ALIAS port_declaration
#line 363 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrAttributeDefinition * > () = yystack_[0].value.as < piranha::IrAttributeDefinition * > (); yylhs.value.as < piranha::IrAttributeDefinition * > ()->setAlias(true); yylhs.value.as < piranha::IrAttributeDefinition * > ()->setAliasToken(yystack_[1].value.as < piranha::IrTokenInfo_string > ()); }
#line 1898 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 65: // port_status: port_declaration
#line 364 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrAttributeDefinition * > () = yystack_[0].value.as < piranha::IrAttributeDefinition * > (); yylhs.value.as < piranha::IrAttributeDefinition * > ()->setAlias(false); }
#line 1904 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 66: // port_value: port_status '[' type_name_namespace ']'
#line 368 "flex-bison/specification.y"
                                                   { yylhs.value.as < piranha::IrAttributeDefinition * > () = yystack_[3].value.as < piranha::IrAttributeDefinition * > (); yylhs.value.as < piranha::IrAttributeDefinition * > ()->setTypeInfo(yystack_[1].value.as < piranha::IrTokenInfoSet<std::string, 2> > ()); }
#line 1910 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 67: // port_value: port_status
#line 369 "flex-bison/specification.y"
                                                   { yylhs.value.as < piranha::IrAttributeDefinition * > () = yystack_[0].value.as < piranha::IrAttributeDefinition * > (); }
#line 1916 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 68: // port_connection: port_value ':' value
#line 373 "flex-bison/specification.y"
                                            { yylhs.value.as < piranha::IrAttributeDefinition * > () = yystack_[2].value.as < piranha::IrAttributeDefinition * > (); yylhs.value.as < piranha::IrAttributeDefinition * > ()->setDefaultValue(yystack_[0].value.as < piranha::IrValue * > ()); }
#line 1922 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 69: // port_connection: port_value
#line 374 "flex-bison/specification.y"
                                            { yylhs.value.as < piranha::IrAttributeDefinition * > () = yystack_[0].value.as < piranha::IrAttributeDefinition * > (); yylhs.value.as < piranha::IrAttributeDefinition * > ()->setDefaultValue(nullptr); }
#line 1928 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 70: // documented_port_definition: decorator_list port_connection
#line 378 "flex-bison/specification.y"
                                            { yylhs.value.as < piranha::IrAttributeDefinition * > () = yystack_[0].value.as < piranha::IrAttributeDefinition * > (); }
#line 1934 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 71: // documented_port_definition: port_connection
#line 379 "flex-bison/specification.y"
                                            { yylhs.value.as < piranha::IrAttributeDefinition * > () = yystack_[0].value.as < piranha::IrAttributeDefinition * > (); }
#line 1940 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 72: // inline_node_member: inline_node
#line 383 "flex-bison/specification.y"
                                            { yylhs.value.as < piranha::IrNode * > () = yystack_[0].value.as < piranha::IrNode * > (); }
#line 1946 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 73: // inline_node_member: data_access '.' inline_node
#line 384 "flex-bison/specification.y"
                                            {
                                                yylhs.value.as < piranha::IrNode * > () = yystack_[0].value.as < piranha::IrNode * > ();
                                                yylhs.value.as < piranha::IrNode * > ()->setThis(yystack_[2].value.as < piranha::IrValue * > ());
                                            }
#line 1955 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 74: // inline_node: type_name_namespace connection_block
#line 391 "flex-bison/specification.y"
                                            { yylhs.value.as < piranha::IrNode * > () = TRACK(new IrNode(yystack_[1].value.as < piranha::IrTokenInfoSet<std::string, 2> > ().data[1], yystack_[0].value.as < piranha::IrAttributeList * > (), yystack_[1].value.as < piranha::IrTokenInfoSet<std::string, 2> > ().data[0])); }
#line 1961 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 75: // connection_block: '(' ')'
#line 395 "flex-bison/specification.y"
                                        {
                                            yylhs.value.as < piranha::IrAttributeList * > () = TRACK(new IrAttributeList());
                                            yylhs.value.as < piranha::IrAttributeList * > ()->registerToken(&yystack_[1].value.as < piranha::IrTokenInfo_string > ());
                                            yylhs.value.as < piranha::IrAttributeList * > ()->registerToken(&yystack_[0].value.as < piranha::IrTokenInfo_string > ());
                                        }
#line 1971 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 76: // connection_block: '(' attribute_list ')'
#line 400 "flex-bison/specification.y"
                                        {
                                            yylhs.value.as < piranha::IrAttributeList * > () = yystack_[1].value.as < piranha::IrAttributeList * > ();
                                            yylhs.value.as < piranha::IrAttributeList * > ()->registerToken(&yystack_[2].value.as < piranha::IrTokenInfo_string > ());
                                            yylhs.value.as < piranha::IrAttributeList * > ()->registerToken(&yystack_[0].value.as < piranha::IrTokenInfo_string > ());
                                        }
#line 1981 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 77: // connection_block: '(' error ')'
#line 405 "flex-bison/specification.y"
                                        { yyerrok; }
#line 1987 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 78: // attribute_list: attribute
#line 409 "flex-bison/specification.y"
                                        { 
                                            yylhs.value.as < piranha::IrAttributeList * > () = TRACK(new IrAttributeList());
                                            yylhs.value.as < piranha::IrAttributeList * > ()->addAttribute(yystack_[0].value.as < piranha::IrAttribute * > ()); 
                                        }
#line 1996 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 79: // attribute_list: attribute_list ',' attribute
#line 413 "flex-bison/specification.y"
                                        {
                                            yystack_[2].value.as < piranha::IrAttributeList * > ()->addAttribute(yystack_[0].value.as < piranha::IrAttribute * > ()); 
                                            yylhs.value.as < piranha::IrAttributeList * > () = yystack_[2].value.as < piranha::IrAttributeList * > (); 
                                        }
#line 2005 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 80: // attribute_list: error ',' attribute
#line 417 "flex-bison/specification.y"
                                        {
                                            yylhs.value.as < piranha::IrAttributeList * > () = TRACK(new IrAttributeList());
                                            yylhs.value.as < piranha::IrAttributeList * > ()->addAttribute(yystack_[0].value.as < piranha::IrAttribute * > ());
                                        }
#line 2014 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 81: // attribute: LABEL ':' value
#line 424 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrAttribute * > () = TRACK(new IrAttribute(yystack_[2].value.as < piranha::IrTokenInfo_string > (), yystack_[0].value.as < piranha::IrValue * > ())); }
#line 2020 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 82: // attribute: value
#line 425 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrAttribute * > () = TRACK(new IrAttribute(yystack_[0].value.as < piranha::IrValue * > ())); }
#line 2026 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 83: // label_value: LABEL
#line 429 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrValue * > () = static_cast<IrValue *>(TRACK(new IrValueLabel(yystack_[0].value.as < piranha::IrTokenInfo_string > ()))); }
#line 2032 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 84: // value: add_exp
#line 433 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrValue * > () = yystack_[0].value.as < piranha::IrValue * > (); }
#line 2038 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 85: // string: STRING
#line 437 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrTokenInfo_string > () = yystack_[0].value.as < piranha::IrTokenInfo_string > (); yylhs.location = yystack_[0].value.as < piranha::IrTokenInfo_string > (); }
#line 2044 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 86: // string: string STRING
#line 438 "flex-bison/specification.y"
                                        { 
                                            yylhs.value.as < piranha::IrTokenInfo_string > () = IrTokenInfo_string();
                                            yylhs.value.as < piranha::IrTokenInfo_string > ().data = yystack_[1].value.as < piranha::IrTokenInfo_string > ().data + yystack_[0].value.as < piranha::IrTokenInfo_string > ().data;
                                            yylhs.value.as < piranha::IrTokenInfo_string > ().combine(&yystack_[1].value.as < piranha::IrTokenInfo_string > ());
                                            yylhs.value.as < piranha::IrTokenInfo_string > ().combine(&yystack_[0].value.as < piranha::IrTokenInfo_string > ());
                                        }
#line 2055 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 87: // constant: INT
#line 447 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrValue * > () = static_cast<IrValue *>(TRACK(new IrValueInt(yystack_[0].value.as < piranha::IrTokenInfo_int > ()))); yylhs.location = yystack_[0].value.as < piranha::IrTokenInfo_int > (); }
#line 2061 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 88: // constant: string
#line 448 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrValue * > () = static_cast<IrValue *>(TRACK(new IrValueString(yystack_[0].value.as < piranha::IrTokenInfo_string > ()))); }
#line 2067 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 89: // constant: FLOAT
#line 449 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrValue * > () = static_cast<IrValue *>(TRACK(new IrValueFloat(yystack_[0].value.as < piranha::IrTokenInfo_float > ()))); yylhs.location = yystack_[0].value.as < piranha::IrTokenInfo_float > (); }
#line 2073 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 90: // constant: BOOL
#line 450 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrValue * > () = static_cast<IrValue *>(TRACK(new IrValueBool(yystack_[0].value.as < piranha::IrTokenInfo_bool > ()))); yylhs.location = yystack_[0].value.as < piranha::IrTokenInfo_bool > (); }
#line 2079 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 91: // atomic_value: label_value
#line 454 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrValue * > () = yystack_[0].value.as < piranha::IrValue * > (); }
#line 2085 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 92: // atomic_value: inline_node_member
#line 455 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrValue * > () = static_cast<IrValue *>(TRACK(new IrValueNodeRef(yystack_[0].value.as < piranha::IrNode * > ()))); }
#line 2091 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 93: // atomic_value: constant
#line 456 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrValue * > () = yystack_[0].value.as < piranha::IrValue * > (); }
#line 2097 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 94: // primary_exp: atomic_value
#line 460 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrValue * > () = yystack_[0].value.as < piranha::IrValue * > (); }
#line 2103 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 95: // primary_exp: '(' value ')'
#line 461 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrValue * > () = yystack_[1].value.as < piranha::IrValue * > (); yylhs.value.as < piranha::IrValue * > ()->registerToken(&yystack_[2].value.as < piranha::IrTokenInfo_string > ()); yylhs.value.as < piranha::IrValue * > ()->registerToken(&yystack_[0].value.as < piranha::IrTokenInfo_string > ()); }
#line 2109 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 96: // primary_exp: '(' error ')'
#line 462 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrValue * > () = nullptr; yyerrok; }
#line 2115 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 97: // data_access: primary_exp
#line 466 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrValue * > () = yystack_[0].value.as < piranha::IrValue * > (); }
#line 2121 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 98: // data_access: data_access '.' label_value
#line 467 "flex-bison/specification.y"
                                        { 
                                            yylhs.value.as < piranha::IrValue * > () = static_cast<IrValue *>(
                                                TRACK(new IrBinaryOperator(IrBinaryOperator::Operator::Dot, yystack_[2].value.as < piranha::IrValue * > (), yystack_[0].value.as < piranha::IrValue * > ())));
                                        }
#line 2130 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 99: // unary_exp: data_access
#line 474 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrValue * > () = yystack_[0].value.as < piranha::IrValue * > (); }
#line 2136 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 100: // unary_exp: '-' data_access
#line 475 "flex-bison/specification.y"
                                        {
                                            yylhs.value.as < piranha::IrValue * > () = static_cast<IrValue *>(
                                                TRACK(new IrUnaryOperator(IrUnaryOperator::Operator::NumericNegate, yystack_[0].value.as < piranha::IrValue * > ())));
                                        }
#line 2145 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 101: // unary_exp: '+' data_access
#line 479 "flex-bison/specification.y"
                                        {
                                            yylhs.value.as < piranha::IrValue * > () = static_cast<IrValue *>(
                                                TRACK(new IrUnaryOperator(IrUnaryOperator::Operator::Positive, yystack_[0].value.as < piranha::IrValue * > ())));
                                        }
#line 2154 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 102: // unary_exp: '!' data_access
#line 483 "flex-bison/specification.y"
                                        {
                                            yylhs.value.as < piranha::IrValue * > () = static_cast<IrValue *>(
                                                TRACK(new IrUnaryOperator(IrUnaryOperator::Operator::BoolNegate, yystack_[0].value.as < piranha::IrValue * > ())));
                                        }
#line 2163 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 103: // mul_exp: unary_exp
#line 490 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrValue * > () = yystack_[0].value.as < piranha::IrValue * > (); }
#line 2169 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 104: // mul_exp: mul_exp '*' unary_exp
#line 491 "flex-bison/specification.y"
                                        {
                                            yylhs.value.as < piranha::IrValue * > () = static_cast<IrValue *>(
                                                TRACK(new IrBinaryOperator(IrBinaryOperator::Operator::Mul, yystack_[2].value.as < piranha::IrValue * > (), yystack_[0].value.as < piranha::IrValue * > ())));
                                        }
#line 2178 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 105: // mul_exp: mul_exp '/' unary_exp
#line 495 "flex-bison/specification.y"
                                        {
                                            yylhs.value.as < piranha::IrValue * > () = static_cast<IrValue *>(
                                                TRACK(new IrBinaryOperator(IrBinaryOperator::Operator::Div, yystack_[2].value.as < piranha::IrValue * > (), yystack_[0].value.as < piranha::IrValue * > ())));
                                        }
#line 2187 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 106: // add_exp: mul_exp
#line 502 "flex-bison/specification.y"
                                        { yylhs.value.as < piranha::IrValue * > () = yystack_[0].value.as < piranha::IrValue * > (); }
#line 2193 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 107: // add_exp: add_exp '+' mul_exp
#line 503 "flex-bison/specification.y"
                                        {
                                            yylhs.value.as < piranha::IrValue * > () = static_cast<IrValue *>(
                                                TRACK(new IrBinaryOperator(IrBinaryOperator::Operator::Add, yystack_[2].value.as < piranha::IrValue * > (), yystack_[0].value.as < piranha::IrValue * > ())));
                                        }
#line 2202 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;

  case 108: // add_exp: add_exp '-' mul_exp
#line 507 "flex-bison/specification.y"
                                        {
                                            yylhs.value.as < piranha::IrValue * > () = static_cast<IrValue *>(
                                                TRACK(new IrBinaryOperator(IrBinaryOperator::Operator::Sub, yystack_[2].value.as < piranha::IrValue * > (), yystack_[0].value.as < piranha::IrValue * > ())));
                                        }
#line 2211 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"
    break;


#line 2215 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"

            default:
              break;
            }
        }
#if YY_EXCEPTIONS
      catch (const syntax_error& yyexc)
        {
          YYCDEBUG << "Caught exception: " << yyexc.what() << '\n';
          error (yyexc);
          YYERROR;
        }
#endif // YY_EXCEPTIONS
      YY_SYMBOL_PRINT ("-> $$ =", yylhs);
      yypop_ (yylen);
      yylen = 0;

      // Shift the result of the reduction.
      yypush_ (YY_NULLPTR, YY_MOVE (yylhs));
    }
    goto yynewstate;


  /*--------------------------------------.
  | yyerrlab -- here on detecting error.  |
  `--------------------------------------*/
  yyerrlab:
    // If not already recovering from an error, report this error.
    if (!yyerrstatus_)
      {
        ++yynerrs_;
        std::string msg = YY_("syntax error");
        error (yyla.location, YY_MOVE (msg));
      }


    yyerror_range[1].location = yyla.location;
    if (yyerrstatus_ == 3)
      {
        /* If just tried and failed to reuse lookahead token after an
           error, discard it.  */

        // Return failure if at end of input.
        if (yyla.kind () == symbol_kind::S_YYEOF)
          YYABORT;
        else if (!yyla.empty ())
          {
            yy_destroy_ ("Error: discarding", yyla);
            yyla.clear ();
          }
      }

    // Else will try to reuse lookahead token after shifting the error token.
    goto yyerrlab1;


  /*---------------------------------------------------.
  | yyerrorlab -- error raised explicitly by YYERROR.  |
  `---------------------------------------------------*/
  yyerrorlab:
    /* Pacify compilers when the user code never invokes YYERROR and
       the label yyerrorlab therefore never appears in user code.  */
    if (false)
      YYERROR;

    /* Do not reclaim the symbols of the rule whose action triggered
       this YYERROR.  */
    yypop_ (yylen);
    yylen = 0;
    YY_STACK_PRINT ();
    goto yyerrlab1;


  /*-------------------------------------------------------------.
  | yyerrlab1 -- common code for both syntax error and YYERROR.  |
  `-------------------------------------------------------------*/
  yyerrlab1:
    yyerrstatus_ = 3;   // Each real token shifted decrements this.
    // Pop stack until we find a state that shifts the error token.
    for (;;)
      {
        yyn = yypact_[+yystack_[0].state];
        if (!yy_pact_value_is_default_ (yyn))
          {
            yyn += symbol_kind::S_YYerror;
            if (0 <= yyn && yyn <= yylast_
                && yycheck_[yyn] == symbol_kind::S_YYerror)
              {
                yyn = yytable_[yyn];
                if (0 < yyn)
                  break;
              }
          }

        // Pop the current state because it cannot handle the error token.
        if (yystack_.size () == 1)
          YYABORT;

        yyerror_range[1].location = yystack_[0].location;
        yy_destroy_ ("Error: popping", yystack_[0]);
        yypop_ ();
        YY_STACK_PRINT ();
      }
    {
      stack_symbol_type error_token;

      yyerror_range[2].location = yyla.location;
      YYLLOC_DEFAULT (error_token.location, yyerror_range, 2);

      // Shift the error token.
      error_token.state = state_type (yyn);
      yypush_ ("Shifting", YY_MOVE (error_token));
    }
    goto yynewstate;


  /*-------------------------------------.
  | yyacceptlab -- YYACCEPT comes here.  |
  `-------------------------------------*/
  yyacceptlab:
    yyresult = 0;
    goto yyreturn;


  /*-----------------------------------.
  | yyabortlab -- YYABORT comes here.  |
  `-----------------------------------*/
  yyabortlab:
    yyresult = 1;
    goto yyreturn;


  /*-----------------------------------------------------.
  | yyreturn -- parsing is finished, return the result.  |
  `-----------------------------------------------------*/
  yyreturn:
    if (!yyla.empty ())
      yy_destroy_ ("Cleanup: discarding lookahead", yyla);

    /* Do not reclaim the symbols of the rule whose action triggered
       this YYABORT or YYACCEPT.  */
    yypop_ (yylen);
    YY_STACK_PRINT ();
    while (1 < yystack_.size ())
      {
        yy_destroy_ ("Cleanup: popping", yystack_[0]);
        yypop_ ();
      }

    return yyresult;
  }
#if YY_EXCEPTIONS
    catch (...)
      {
        YYCDEBUG << "Exception caught: cleaning lookahead and stack\n";
        // Do not try to display the values of the reclaimed symbols,
        // as their printers might throw an exception.
        if (!yyla.empty ())
          yy_destroy_ (YY_NULLPTR, yyla);

        while (1 < yystack_.size ())
          {
            yy_destroy_ (YY_NULLPTR, yystack_[0]);
            yypop_ ();
          }
        throw;
      }
#endif // YY_EXCEPTIONS
  }

  void
  Parser::error (const syntax_error& yyexc)
  {
    error (yyexc.location, yyexc.what ());
  }

#if YYDEBUG || 0
  const char *
  Parser::symbol_name (symbol_kind_type yysymbol)
  {
    return yytname_[yysymbol];
  }
#endif // #if YYDEBUG || 0









  const signed char Parser::yypact_ninf_ = -106;

  const signed char Parser::yytable_ninf_ = -75;

  const short
  Parser::yypact_[] =
  {
     278,  -106,   -19,    65,    31,    55,     6,  -106,  -106,  -106,
    -106,    24,   139,   139,    53,   144,    49,    69,    85,    58,
    -106,    61,  -106,   305,  -106,    86,  -106,  -106,    -5,  -106,
    -106,  -106,    73,    20,  -106,  -106,  -106,  -106,  -106,  -106,
    -106,    80,  -106,  -106,  -106,    62,  -106,   147,  -106,    80,
    -106,   144,  -106,    53,    70,  -106,  -106,  -106,  -106,  -106,
    -106,  -106,  -106,  -106,  -106,  -106,    97,   146,   146,  -106,
     116,   359,   359,   359,   110,   121,   129,  -106,    81,    91,
    -106,  -106,  -106,  -106,   -19,  -106,   183,   110,    17,   157,
     192,   -19,   175,  -106,   105,   167,   191,   195,   196,   209,
     210,  -106,   221,  -106,   109,  -106,   188,   198,  -106,    13,
    -106,  -106,   217,    22,  -106,   129,   129,   129,  -106,  -106,
     105,   319,   319,   319,   319,  -106,  -106,   -20,    52,  -106,
      -4,  -106,  -106,  -106,   203,  -106,   227,  -106,  -106,  -106,
    -106,  -106,  -106,  -106,  -106,  -106,  -106,  -106,  -106,  -106,
     108,   319,  -106,  -106,    80,  -106,  -106,  -106,    81,    81,
    -106,   339,   319,  -106,   339,  -106,   251,  -106,   204,   200,
    -106,  -106,  -106,  -106,  -106,  -106
  };

  const signed char
  Parser::yydefact_[] =
  {
       0,     2,     0,     0,     0,     0,    83,    87,    89,    90,
      85,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       5,     0,    11,     0,    18,    20,     8,    23,     0,    28,
       7,    40,    42,     0,    49,    53,    55,     9,    92,    72,
      91,    88,    93,    94,    97,     0,    56,     0,    15,    14,
      37,     0,    39,     0,     0,    16,    51,    17,    50,    21,
      25,    34,    33,    35,    36,    22,     0,     0,     0,    52,
       0,     0,     0,     0,     0,     0,    99,   103,   106,    84,
       1,     6,    54,     3,    13,    12,     0,     0,     0,    27,
       0,     0,     0,    86,     0,     0,     0,     0,     0,     0,
       0,    47,     0,    30,     0,    65,    67,    69,    71,     0,
      38,    24,     0,     0,    96,   101,   100,   102,    74,    95,
       0,     0,     0,     0,     0,    19,    26,     0,    83,    75,
       0,    78,    82,    41,     0,    43,     0,    29,    73,    98,
      59,    64,    60,    61,    62,    63,    70,    32,    48,    31,
       0,     0,    58,    57,     4,    10,   105,   104,   107,   108,
      77,     0,     0,    76,     0,    45,     0,    44,    21,     0,
      68,    80,    81,    79,    46,    66
  };

  const short
  Parser::yypgoto_[] =
  {
    -106,  -106,   -17,     3,   222,  -106,   165,  -106,  -106,    -2,
       1,   152,     2,   -81,   197,   242,  -106,  -106,    -3,   233,
     232,  -106,   -28,   158,  -106,  -106,   153,  -106,  -106,   -78,
     -52,  -106,  -105,   -77,   -12,     4,  -106,  -106,  -106,     0,
      59,    71,  -106
  };

  const unsigned char
  Parser::yydefgoto_[] =
  {
       0,    19,    20,   102,    22,    23,    24,    25,    26,    27,
      74,    29,   103,   104,    65,    31,    32,    33,    34,    35,
      36,    37,    47,   105,   106,   107,   108,   109,    38,    39,
      89,   130,   131,    40,   132,    41,    42,    43,    44,    76,
      77,    78,    79
  };

  const short
  Parser::yytable_[] =
  {
      45,    28,    30,    21,    81,    92,    75,    49,    87,    56,
      58,   136,    60,   160,   152,    46,   138,   139,   127,   -21,
     161,    91,   118,    45,    28,    30,    21,    88,    53,   163,
     128,     7,     8,     9,    10,   126,   164,    54,   -21,    14,
      11,    15,   138,   139,    50,    71,    72,    45,    28,    18,
     129,   111,   153,   166,    46,    51,   171,   155,    80,   173,
      73,     4,     2,   134,    56,    58,    59,     4,     5,   113,
       2,   115,   116,   117,    53,     4,     5,    15,    48,    11,
      67,    68,    10,    66,   -21,    81,    70,    17,    67,    68,
     162,    86,    45,    28,    90,    28,    81,    93,     6,     7,
       8,     9,    10,    94,    45,    28,   149,    14,   112,    15,
     147,   121,   122,    71,    72,    11,   154,    18,     6,   123,
     124,   168,     6,     7,     8,     9,    10,    14,    73,    15,
      14,    14,    15,    15,    45,    28,    45,    28,   149,   170,
       2,    18,    88,     3,   148,     4,     5,     2,    95,   114,
     172,   169,     4,     5,   119,    96,    97,    98,    99,   100,
       6,     7,     8,     9,    10,    11,    45,    28,   149,    14,
     120,    15,    61,    62,    63,    64,    95,    55,    57,    18,
     156,   157,   101,    96,    97,    98,    99,   100,     6,     7,
       8,     9,    10,    11,   158,   159,   125,    14,   -74,    15,
      97,    98,    99,   100,    95,   133,   140,    18,   142,   143,
     135,    96,    97,    98,    99,   100,     6,     7,     8,     9,
      10,    11,   144,   145,   150,    14,    53,    15,   147,    96,
      97,    98,    99,   100,    10,    18,   151,   175,   165,    11,
       6,     7,     8,     9,    10,    85,   137,    52,   110,    14,
      69,    15,   147,    82,   141,   146,     0,     0,     0,    18,
       0,     0,   167,     0,     6,     7,     8,     9,    10,     0,
       0,     0,     0,    14,     0,    15,     0,     0,     1,     2,
       0,     0,     3,    18,     4,     5,   174,     0,     0,     0,
       0,     6,     7,     8,     9,    10,    11,    12,    13,     0,
      14,     0,    15,    16,    17,    83,    84,     0,     0,     3,
      18,     4,     5,     0,     0,     0,     0,     0,     6,     7,
       8,     9,    10,    11,    12,    13,     0,    14,     0,    15,
      16,    17,     6,     7,     8,     9,    10,    18,     0,     0,
       0,    14,     0,    15,     0,     0,     0,    71,    72,     0,
       0,    18,   128,     7,     8,     9,    10,     0,     0,     0,
       0,    14,    73,    15,     0,     0,     0,    71,    72,     0,
       0,    18,     6,     7,     8,     9,    10,     0,     0,     0,
       0,    14,    73,    15,     0,     0,     0,     0,     0,     0,
       0,    18
  };

  const short
  Parser::yycheck_[] =
  {
       0,     0,     0,     0,    21,    33,    18,     3,    13,    12,
      13,    92,    14,    33,     1,    34,    94,    94,     1,    13,
      40,     1,    74,    23,    23,    23,    23,    32,    22,    33,
      13,    14,    15,    16,    17,    87,    40,    13,    32,    22,
      18,    24,   120,   120,    13,    28,    29,    47,    47,    32,
      33,    53,    39,   134,    34,    24,   161,    35,     0,   164,
      43,     6,     1,    91,    67,    68,    13,     6,     7,    66,
       1,    71,    72,    73,    22,     6,     7,    24,    13,    18,
      19,    20,    17,    34,    32,   102,     1,    26,    19,    20,
      38,     5,    92,    92,    21,    94,   113,    17,    13,    14,
      15,    16,    17,    41,   104,   104,   104,    22,    38,    24,
       1,    30,    31,    28,    29,    18,   112,    32,    13,    28,
      29,    13,    13,    14,    15,    16,    17,    22,    43,    24,
      22,    22,    24,    24,   134,   134,   136,   136,   136,   151,
       1,    32,    32,     4,    35,     6,     7,     1,     1,    33,
     162,   150,     6,     7,    33,     8,     9,    10,    11,    12,
      13,    14,    15,    16,    17,    18,   166,   166,   166,    22,
      41,    24,    28,    29,    30,    31,     1,    12,    13,    32,
     121,   122,    35,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,   123,   124,    13,    22,    41,    24,
       9,    10,    11,    12,     1,    13,    39,    32,    13,    13,
      35,     8,     9,    10,    11,    12,    13,    14,    15,    16,
      17,    18,    13,    13,    36,    22,    22,    24,     1,     8,
       9,    10,    11,    12,    17,    32,    38,    37,    35,    18,
      13,    14,    15,    16,    17,    23,    94,     5,    51,    22,
      17,    24,     1,    21,    96,   102,    -1,    -1,    -1,    32,
      -1,    -1,    35,    -1,    13,    14,    15,    16,    17,    -1,
      -1,    -1,    -1,    22,    -1,    24,    -1,    -1,     0,     1,
      -1,    -1,     4,    32,     6,     7,    35,    -1,    -1,    -1,
      -1,    13,    14,    15,    16,    17,    18,    19,    20,    -1,
      22,    -1,    24,    25,    26,     0,     1,    -1,    -1,     4,
      32,     6,     7,    -1,    -1,    -1,    -1,    -1,    13,    14,
      15,    16,    17,    18,    19,    20,    -1,    22,    -1,    24,
      25,    26,    13,    14,    15,    16,    17,    32,    -1,    -1,
      -1,    22,    -1,    24,    -1,    -1,    -1,    28,    29,    -1,
      -1,    32,    13,    14,    15,    16,    17,    -1,    -1,    -1,
      -1,    22,    43,    24,    -1,    -1,    -1,    28,    29,    -1,
      -1,    32,    13,    14,    15,    16,    17,    -1,    -1,    -1,
      -1,    22,    43,    24,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    32
  };

  const signed char
  Parser::yystos_[] =
  {
       0,     0,     1,     4,     6,     7,    13,    14,    15,    16,
      17,    18,    19,    20,    22,    24,    25,    26,    32,    45,
      46,    47,    48,    49,    50,    51,    52,    53,    54,    55,
      56,    59,    60,    61,    62,    63,    64,    65,    72,    73,
      77,    79,    80,    81,    82,    83,    34,    66,    13,    79,
      13,    24,    59,    22,    13,    50,    62,    50,    62,    13,
      53,    28,    29,    30,    31,    58,    34,    19,    20,    63,
       1,    28,    29,    43,    54,    78,    83,    84,    85,    86,
       0,    46,    64,     0,     1,    48,     5,    13,    32,    74,
      21,     1,    66,    17,    41,     1,     8,     9,    10,    11,
      12,    35,    47,    56,    57,    67,    68,    69,    70,    71,
      58,    53,    38,    47,    33,    83,    83,    83,    74,    33,
      41,    30,    31,    28,    29,    13,    74,     1,    13,    33,
      75,    76,    78,    13,    66,    35,    57,    55,    73,    77,
      39,    67,    13,    13,    13,    13,    70,     1,    35,    56,
      36,    38,     1,    39,    79,    35,    84,    84,    85,    85,
      33,    40,    38,    33,    40,    35,    57,    35,    13,    54,
      78,    76,    78,    76,    35,    37
  };

  const signed char
  Parser::yyr1_[] =
  {
       0,    44,    45,    45,    46,    47,    47,    48,    48,    48,
      48,    49,    49,    49,    50,    50,    51,    51,    51,    52,
      52,    53,    53,    54,    54,    54,    55,    55,    56,    56,
      57,    57,    57,    58,    58,    58,    58,    59,    59,    60,
      60,    61,    61,    62,    62,    62,    62,    62,    62,    63,
      63,    63,    64,    64,    65,    65,    66,    66,    66,    66,
      67,    67,    67,    67,    68,    68,    69,    69,    70,    70,
      71,    71,    72,    72,    73,    74,    74,    74,    75,    75,
      75,    76,    76,    77,    78,    79,    79,    80,    80,    80,
      80,    81,    81,    81,    82,    82,    82,    83,    83,    84,
      84,    84,    84,    85,    85,    85,    86,    86,    86
  };

  const signed char
  Parser::yyr2_[] =
  {
       0,     2,     1,     2,     4,     1,     2,     1,     1,     1,
       4,     1,     2,     2,     2,     2,     2,     2,     1,     3,
       1,     1,     2,     1,     3,     2,     3,     2,     1,     3,
       1,     2,     2,     1,     1,     1,     1,     2,     3,     2,
       1,     3,     1,     3,     4,     4,     5,     3,     4,     1,
       2,     2,     2,     1,     2,     1,     1,     3,     3,     3,
       2,     2,     2,     2,     2,     1,     4,     1,     3,     1,
       2,     1,     1,     3,     2,     2,     3,     3,     1,     3,
       3,     3,     1,     1,     1,     1,     2,     1,     1,     1,
       1,     1,     1,     1,     1,     3,     3,     1,     3,     1,
       2,     2,     2,     1,     3,     3,     1,     3,     3
  };


#if YYDEBUG
  // YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
  // First, the terminals, then, starting at \a YYNTOKENS, nonterminals.
  const char*
  const Parser::yytname_[] =
  {
  "END", "error", "\"invalid token\"", "CHAR", "IMPORT", "AS", "NODE",
  "INLINE", "ALIAS", "INPUT", "OUTPUT", "MODIFY", "TOGGLE", "LABEL", "INT",
  "FLOAT", "BOOL", "STRING", "DECORATOR", "PUBLIC", "PRIVATE",
  "BUILTIN_POINTER", "NAMESPACE_POINTER", "UNRECOGNIZED", "OPERATOR",
  "MODULE", "AUTO", "'='", "'+'", "'-'", "'/'", "'*'", "'('", "')'", "'{'",
  "'}'", "'['", "']'", "':'", "';'", "','", "'.'", "'^'", "'!'", "$accept",
  "sdl", "decorator", "decorator_list", "statement", "statement_list",
  "import_statement", "import_statement_visibility",
  "import_statement_short_name", "type_name", "type_name_namespace",
  "node", "node_member", "node_list", "standard_operator", "node_name",
  "node_inline", "node_shadow", "node_definition",
  "specific_node_definition", "immediate_node_definition",
  "node_decorator", "port_definitions", "port_declaration", "port_status",
  "port_value", "port_connection", "documented_port_definition",
  "inline_node_member", "inline_node", "connection_block",
  "attribute_list", "attribute", "label_value", "value", "string",
  "constant", "atomic_value", "primary_exp", "data_access", "unary_exp",
  "mul_exp", "add_exp", YY_NULLPTR
  };
#endif


#if YYDEBUG
  const short
  Parser::yyrline_[] =
  {
       0,   168,   168,   169,   173,   177,   178,   182,   183,   184,
     185,   189,   190,   191,   195,   196,   205,   206,   207,   211,
     212,   216,   217,   226,   231,   237,   246,   247,   257,   258,
     265,   269,   273,   277,   278,   279,   280,   284,   285,   294,
     295,   299,   300,   304,   305,   306,   307,   308,   309,   313,
     314,   322,   333,   340,   344,   345,   349,   350,   351,   352,
     356,   357,   358,   359,   363,   364,   368,   369,   373,   374,
     378,   379,   383,   384,   391,   395,   400,   405,   409,   413,
     417,   424,   425,   429,   433,   437,   438,   447,   448,   449,
     450,   454,   455,   456,   460,   461,   462,   466,   467,   474,
     475,   479,   483,   490,   491,   495,   502,   503,   507
  };

  void
  Parser::yy_stack_print_ () const
  {
    *yycdebug_ << "Stack now";
    for (stack_type::const_iterator
           i = yystack_.begin (),
           i_end = yystack_.end ();
         i != i_end; ++i)
      *yycdebug_ << ' ' << int (i->state);
    *yycdebug_ << '\n';
  }

  void
  Parser::yy_reduce_print_ (int yyrule) const
  {
    int yylno = yyrline_[yyrule];
    int yynrhs = yyr2_[yyrule];
    // Print the symbols being reduced, and their result.
    *yycdebug_ << "Reducing stack by rule " << yyrule - 1
               << " (line " << yylno << "):\n";
    // The symbols being reduced.
    for (int yyi = 0; yyi < yynrhs; yyi++)
      YY_SYMBOL_PRINT ("   $" << yyi + 1 << " =",
                       yystack_[(yynrhs) - (yyi + 1)]);
  }
#endif // YYDEBUG

  Parser::symbol_kind_type
  Parser::yytranslate_ (int t) YY_NOEXCEPT
  {
    // YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to
    // TOKEN-NUM as returned by yylex.
    static
    const signed char
    translate_table[] =
    {
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    43,     2,     2,     2,     2,     2,     2,
      32,    33,    31,    28,    40,    29,    41,    30,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    38,    39,
       2,    27,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,    36,     2,    37,    42,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    34,     2,    35,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26
    };
    // Last valid token kind.
    const int code_max = 281;

    if (t <= 0)
      return symbol_kind::S_YYEOF;
    else if (t <= code_max)
      return static_cast <symbol_kind_type> (translate_table[t]);
    else
      return symbol_kind::S_YYUNDEF;
  }

#line 5 "flex-bison/specification.y"
} // piranha
#line 2748 "/Users/danielsinclair/vscode/engine-sim-cli/engine-sim/dependencies/submodules/piranha/parser.auto.cpp"

#line 512 "flex-bison/specification.y"


void piranha::Parser::error(const IrTokenInfo &l, const std::string &err_message) {
    CompilationError *err;
    
    if (l.valid) {
        err = TRACK(new CompilationError(l, ErrorCode::UnexpectedToken));
    }
    else {
        err = TRACK(new CompilationError(l, ErrorCode::UnidentifiedToken));
    }

    driver.addCompilationError(err);
}
