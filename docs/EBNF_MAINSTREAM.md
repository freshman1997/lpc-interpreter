# LPC EBNF (Mainstream-Oriented Draft)

## Lexical

```ebnf
letter          = "A".."Z" | "a".."z" | "_" ;
digit           = "0".."9" ;
ident           = letter , { letter | digit } ;

int_lit         = digit , { digit } ;
float_lit       = digit , { digit } , "." , digit , { digit } ;
string_lit      = '"' , { ? any char except quote/newline ? } , '"' ;

bool_lit        = "true" | "false" ;
null_lit        = "null" ;
```

## Program

```ebnf
program         = { top_decl } ;

top_decl        = include_decl
                | import_decl
                | inherit_decl
                | var_decl
                | func_decl
                | class_decl ;

include_decl    = "#" , "include" , ( string_lit | "<" , import_path , ">" ) ;
import_decl     = "#" , "import" , import_path , "as" , ident ;
inherit_decl    = "inherit" , string_lit , ";" ;

import_path     = ident , { "." , ident } ;
```

## Types

```ebnf
type            = "void" | "int" | "float" | "bool" | "string"
                | "mapping" | "mixed" | "object" | "fun" | ident ;

decorator       = "static" | "private" ;
```

## Declarations

```ebnf
var_decl        = [ decorator ] , [ "varargs" ] , type , [ "*" ] , ident , [ "=" , expr ] , ";" ;

func_decl       = [ decorator ] , [ "varargs" ] , type , [ "*" ] , ident ,
                  "(" , [ param_list ] , ")" , ( block | ";" ) ;

param_list      = param , { "," , param } ;
param           = [ "varargs" ] , type , [ "*" ] , ident ;

class_decl      = [ "static" ] , "class" , ident , "{" , { var_decl } , "}" ;
```

## Statements

```ebnf
stmt            = block
                | var_decl
                | expr_stmt
                | if_stmt
                | for_stmt
                | foreach_stmt
                | while_stmt
                | do_while_stmt
                | switch_stmt
                | return_stmt
                | break_stmt
                | continue_stmt ;

block           = "{" , { stmt } , "}" ;
expr_stmt       = expr , ";" ;

if_stmt         = "if" , "(" , expr , ")" , stmt , [ "else" , stmt ] ;

for_stmt        = "for" , "(" , [ expr_list ] , ";" , [ expr_list ] , ";" , [ expr_list ] , ")" , stmt ;
expr_list       = expr , { "," , expr } ;

foreach_stmt    = "foreach" , "(" , foreach_decl_list , "in" , expr , ")" , stmt ;
foreach_decl_list = foreach_decl , { "," , foreach_decl } ;
foreach_decl    = type , [ "*" ] , ident ;

while_stmt      = "while" , "(" , expr , ")" , stmt ;
do_while_stmt   = "do" , block , "while" , "(" , expr , ")" , ";" ;

switch_stmt     = "switch" , "(" , expr , ")" , "{" , { case_clause } , [ default_clause ] , "}" ;
case_clause     = "case" , expr , ":" , { stmt } ;
default_clause  = "default" , ":" , { stmt } ;

return_stmt     = "return" , [ expr ] , ";" ;
break_stmt      = "break" , ";" ;
continue_stmt   = "continue" , ";" ;
```

## Expressions

```ebnf
expr            = assignment ;

assignment      = conditional , [ assign_op , assignment ] ;
assign_op       = "=" | "+=" | "-=" | "*=" | "/=" | "%=" ;

conditional     = logical_or , [ "?" , expr , ":" , expr ] ;

logical_or      = logical_and , { "||" , logical_and } ;
logical_and     = bit_or , { "&&" , bit_or } ;
bit_or          = bit_and , { "|" , bit_and } ;
bit_and         = equality , { "&" , equality } ;

equality        = relation , { ( "==" | "!=" ) , relation } ;
relation        = shift , { ( "<" | "<=" | ">" | ">=" ) , shift } ;
shift           = additive , { ( "<<" | ">>" ) , additive } ;
additive        = multiplicative , { ( "+" | "-" ) , multiplicative } ;
multiplicative  = unary , { ( "*" | "/" | "%" ) , unary } ;

unary           = ( "!" | "-" | "++" | "--" ) , unary
                | postfix ;

postfix         = primary , { call_suffix | index_suffix | member_suffix | postfix_incdec } ;
call_suffix     = "(" , [ arg_list ] , ")" ;
arg_list        = expr , { "," , expr } ;
index_suffix    = "[" , expr , [ ".." , expr ] , "]" ;
member_suffix   = "->" , ident ;
postfix_incdec  = "++" | "--" ;

primary         = ident
                | int_lit
                | float_lit
                | string_lit
                | bool_lit
                | null_lit
                | "(" , expr , ")"
                | array_ctor
                | map_ctor
                | lambda_expr
                | new_expr ;

array_ctor      = "[" , [ arg_list ] , "]" ;
map_ctor        = "{" , [ map_item , { "," , map_item } ] , "}" ;
map_item        = expr , ":" , expr ;

lambda_expr     = "fun" , "(" , [ param_list ] , ")" , ( block | "->" , expr ) ;
new_expr        = "new" , ident , [ "(" , [ init_list ] , ")" ] ;
init_list       = init_item , { "," , init_item } ;
init_item       = ident , ":" , expr ;
```
