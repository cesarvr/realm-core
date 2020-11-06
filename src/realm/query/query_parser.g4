grammar query_parser;

@members { size_t jed; }

query: pred EOF;

pred: and_pred (OR and_pred)*                                          # or;  
and_pred: atom_pred (AND atom_pred)*                                   # and;

atom_pred
    : value op=(EQUAL|NOT_EQUAL) CASEINSENSITIVE? value                # compareEqual
    | value op=(GREATER|GREATER_EQUAL|LESS|LESS_EQUAL) value           # compare
    | value op=(BEGINSWITH|ENDSWITH|CONTAINS|LIKE) CASEINSENSITIVE? 
               (STRING | NULL_VAL)                                     # stringOps
    | '!' atom_pred                                                    # not
    | '(' pred ')'                                                     # parens
    | val=(TRUE_PRED | FALSE_PRED)                                     # trueOrFalse
    ;

value : prop | constant;

prop
    : aggr=(ANY|ALL|NONE)? path  ID ('.' postOp)?                      # property
    | path ID '.' aggrOp '.' ID                                        # propAggr
    | path ID '.' aggrOp                                               # listAggr
    ;

constant : val=(NUMBER|STRING|TIMESTAMP|UUID|TRUE|FALSE|NULL_VAL|ARG);

path: (ID '.')*;

postOp : type=(COUNT | SIZE); 
aggrOp : type=(MAX | MIN | SUM | AVG); 

// Lexer part

NUMBER
    :   SIGN? DIGIT+ '.' DIGIT* EXP?
    |   SIGN? DIGIT* '.' DIGIT+ EXP?
    |   SIGN? DIGIT+ EXP?
    |   SIGN? ('inf' | 'infinity')
    ;

TRUE_PRED : 'TRUEPREDICATE';
FALSE_PRED : 'FALSEPREDICATE';
NOT_PRED : '!' | 'not' | 'NOT';

COUNT : '@count';
SIZE :  '@size';
MAX : '@max';
MIN : '@min';
SUM : '@sum';
AVG : '@avg';

ANY : 'any' | 'ANY' | 'some' | 'SOME';
ALL : 'all' | 'ALL';
NONE : 'none' | 'NONE';

TRUE : 'true' | 'TRUE';
FALSE : 'false' | 'FALSE';
NULL_VAL: 'NULL' | 'NIL' | 'null' | 'nil';

AND : 'and' | 'AND' | '&&';
OR : 'or' | 'OR' |'||';

GREATER: '>';
LESS: '<';
GREATER_EQUAL: '>=' | '=>';
LESS_EQUAL: '<=' | '=<';
EQUAL: '=='| '=' | 'in' | 'IN';
NOT_EQUAL: '!=' | '<>';

BEGINSWITH: 'BEGINSWITH' | 'beginswith';
ENDSWITH: 'ENDSWITH' | 'endswith';
CONTAINS: 'CONTAINS' | 'contains';
LIKE: 'LIKE' | 'like';
CASEINSENSITIVE : '[c]';

ARG    : '$' DIGIT+;
ID     : [a-zA-Z] [a-zA-Z_\-0-9]+;
STRING : SQ_STRING | DQ_STRING;
DQ_STRING :  '"' CHARS* '"' ;
SQ_STRING : '\'' CHARS* '\'';
TIMESTAMP: ('T' NUMBER ':' NUMBER) | ( INT '-' INT '-' INT ('@'|'T') INT ':' INT ':' INT (':' INT)? );
UUID: 'uuid(' HEX8 '-' HEX4 '-' HEX4 '-' HEX4 '-' HEX12 ')';

fragment INT    : DIGIT+;
fragment SIGN   : [+\-'];
fragment DIGIT  : [0-9] ;
fragment EXP    : [eE] SIGN? INT;
fragment CHARS  : ESC | ~["\\];
fragment ESC :   '\\' (["\\/bfnrt] | UNICODE) ;
fragment UNICODE : 'u' HEX4;
fragment HEX12 : HEX4 HEX4 HEX4;
fragment HEX8 : HEX4 HEX4;
fragment HEX4 : HEX HEX HEX HEX;
fragment HEX : [0-9a-fA-F] ;

WS     :  [ \t\r\n] -> skip;

