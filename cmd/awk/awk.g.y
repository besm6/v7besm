%token	FIRSTTOKEN	/*must be first*/
%token	FINAL FATAL
%token	LT LE GT GE EQ NE
%token	MATCH NOTMATCH
%token	APPEND
%token	ADD MINUS MULT DIVIDE MOD UMINUS 
%token	ASSIGN ADDEQ SUBEQ MULTEQ DIVEQ MODEQ
%token	JUMP
%token	XBEGIN XEND
%token	NL
%token	PRINT PRINTF SPRINTF SPLIT
%token	IF ELSE WHILE FOR IN NEXT EXIT BREAK CONTINUE
%token	PROGRAM PASTAT PASTAT2

%right	ASGNOP
%left	BOR
%left	AND
%left	NOT
%left	NUMBER VAR ARRAY FNCN SUBSTR LSUBSTR INDEX
%left	GETLINE
%nonassoc RELOP MATCHOP
%left	OR
%left	STRING  DOT CCL NCCL CHAR
%left	'(' '^' '$'
%left	CAT
%left	'+' '-'
%left	'*' '/' '%'
%left	STAR PLUS QUEST
%left	POSTINCR PREINCR POSTDECR PREDECR INCR DECR
%left	FIELD INDIRECT
%token	LASTTOKEN	/* has to be last */

%{
#include "awk.h"
%}
%%

program:
	  begin pa_stats end	{ if (errorflag==0) winner = stat3(PROGRAM, $1, $2, $3); }
	| error			{ yyclearin; yyerror("bailing out"); }
	;

begin:
	  XBEGIN '{' stat_list '}'	{ $$ = $3; }
	| begin NL
	| 	{ $$ = nullstat; }
	;

end:
	  XEND '{' stat_list '}'	{ $$ = $3; }
	| end NL
	|	{ $$ = nullstat; }
	;

compound_conditional:
	  conditional BOR conditional	{ $$ = op2(BOR, $1, $3); }
	| conditional AND conditional	{ $$ = op2(AND, $1, $3); }
	| NOT conditional		{ $$ = op1(NOT, $2); }
	| '(' compound_conditional ')'	{ $$ = $2; }
	;

compound_pattern:
	  pattern BOR pattern	{ $$ = op2(BOR, $1, $3); }
	| pattern AND pattern	{ $$ = op2(AND, $1, $3); }
	| NOT pattern		{ $$ = op1(NOT, $2); }
	| '(' compound_pattern ')'	{ $$ = $2; }
	;

conditional:
	  expr	{ $$ = op2(NE, $1, valtonode(lookup("$zero&null", symtab, 0), CCON)); }
	| rel_expr	
	| lex_expr	
	| compound_conditional
	;

else:
	  ELSE optNL
	;

field:
	  FIELD		{ $$ = valtonode((cell *)$1, CFLD); }
	| INDIRECT term { $$ = op1(INDIRECT, $2); }
	;

if:
	  IF '(' conditional ')' optNL	{ $$ = $3; }
	;

lex_expr:
	  expr MATCHOP regular_expr	{ $$ = op2((int)$2, $1, (node *)makedfa($3)); }
	| '(' lex_expr ')'	{ $$ = $2; }
	;

var:
	  NUMBER	{ $$ = valtonode((cell *)$1, CCON); }
	| STRING 	{ $$ = valtonode((cell *)$1, CCON); }
	| VAR		{ $$ = valtonode((cell *)$1, CVAR); }
	| VAR '[' expr ']'	{ $$ = op2(ARRAY, $1, $3); }
	| field
	;
term:
	  var
	| GETLINE	{ $$ = op1(GETLINE, NULL); }
	| FNCN		{ $$ = op2(FNCN, $1, valtonode(lookup("$record", symtab, 0), CFLD));
			}
	| FNCN '(' ')'	{ $$ = op2(FNCN, $1, valtonode(lookup("$record", symtab, 0), CFLD));
			}
	| FNCN '(' expr ')'	{ $$ = op2(FNCN, $1, $3); }
	| SPRINTF print_list	{ $$ = op1((int)$1, $2); }
	| SUBSTR '(' expr ',' expr ',' expr ')'
			{ $$ = op3(SUBSTR, $3, $5, $7); }
	| SUBSTR '(' expr ',' expr ')'
			{ $$ = op3(SUBSTR, $3, $5, nullstat); }
	| SPLIT '(' expr ',' VAR ',' expr ')'
			{ $$ = op3(SPLIT, $3, $5, $7); }
	| SPLIT '(' expr ',' VAR ')'
			{ $$ = op3(SPLIT, $3, $5, nullstat); }
	| INDEX '(' expr ',' expr ')'
			{ $$ = op2(INDEX, $3, $5); }
	| '(' expr ')'			{$$ = $2; }
	| term '+' term			{ $$ = op2(ADD, $1, $3); }
	| term '-' term			{ $$ = op2(MINUS, $1, $3); }
	| term '*' term			{ $$ = op2(MULT, $1, $3); }
	| term '/' term			{ $$ = op2(DIVIDE, $1, $3); }
	| term '%' term			{ $$ = op2(MOD, $1, $3); }
	| '-' term %prec QUEST		{ $$ = op1(UMINUS, $2); }
	| '+' term %prec QUEST		{ $$ = $2; }
	| INCR var	{ $$ = op1(PREINCR, $2); }
	| DECR var	{ $$ = op1(PREDECR, $2); }
	| var INCR	{ $$= op1(POSTINCR, $1); }
	| var DECR	{ $$= op1(POSTDECR, $1); }
	;

expr:
	  term	
	| expr term	{ $$ = op2(CAT, $1, $2); }
	| var ASGNOP expr	{ $$ = stat2((int)$2, $1, $3); }
	;

optNL:
	  NL
	|
	;

pa_stat:
	  pattern	{ $$ = stat2(PASTAT, $1, genprint()); }
	| pattern '{' stat_list '}'	{ $$ = stat2(PASTAT, $1, $3); }
	| pattern ',' pattern		{ $$ = pa2stat($1, $3, genprint()); }
	| pattern ',' pattern '{' stat_list '}'	
					{ $$ = pa2stat($1, $3, $5); }
	| '{' stat_list '}'	{ $$ = stat2(PASTAT, nullstat, $2); }
	;

pa_stats:
	  pa_stats pa_stat st	{ $$ = linkum($1, $2); }
	|	{ $$ = nullstat; }
	| pa_stats pa_stat	{$$ = linkum($1, $2); }
	;

pattern:
	  regular_expr	{ $$ = op2(MATCH, valtonode(lookup("$record", symtab, 0), CFLD), (node *)makedfa($1));
		}
	| rel_expr
	| lex_expr
	| compound_pattern
	;

print_list:
	  expr
	| pe_list
	|		{ $$ = valtonode(lookup("$record", symtab, 0), CFLD); }
	;

pe_list:
	  expr ',' expr	{$$ = linkum($1, $3); }
	| pe_list ',' expr	{$$ = linkum($1, $3); }
	| '(' pe_list ')'		{$$ = $2; }
	;

redir:
	  RELOP
	| '|'
	;

regular_expr:
	  '/'	{ startreg(); }
	  r '/'
		{ $$ = $3; }
	;

r:
	  CHAR		{ $$ = op2(CHAR, (node *) 0, $1); }
	| DOT		{ $$ = op2(DOT, (node *) 0, (node *) 0); }
	| CCL		{ $$ = op2(CCL, (node *) 0, (node *)cclenter((int)$1)); }
	| NCCL		{ $$ = op2(NCCL, (node *) 0, (node *)cclenter((int)$1)); }
	| '^'		{ $$ = op2(CHAR, (node *) 0, (node *)HAT); }
	| '$'		{ $$ = op2(CHAR, (node *) 0 ,(node *) 0); }
	| r OR r	{ $$ = op2(OR, $1, $3); }
	| r r   %prec CAT
			{ $$ = op2(CAT, $1, $2); }
	| r STAR	{ $$ = op2(STAR, $1, (node *) 0); }
	| r PLUS	{ $$ = op2(PLUS, $1, (node *) 0); }
	| r QUEST	{ $$ = op2(QUEST, $1, (node *) 0); }
	| '(' r ')'	{ $$ = $2; }
	;

rel_expr:
	  expr RELOP expr
		{ $$ = op2((int)$2, $1, $3); }
	| '(' rel_expr ')'
		{ $$ = $2; }
	;

st:
	  NL
	| ';'
	;

simple_stat:
	  PRINT print_list redir expr
		{ $$ = stat3((int)$1, $2, $3, $4); }
	| PRINT print_list	
		{ $$ = stat3((int)$1, $2, nullstat, nullstat); }
	| PRINTF print_list redir expr
		{ $$ = stat3((int)$1, $2, $3, $4); }
	| PRINTF print_list	
		{ $$ = stat3((int)$1, $2, nullstat, nullstat); }
	| expr	{ $$ = exptostat($1); }
	|		{ $$ = nullstat; }
	| error		{ yyclearin; yyerror("illegal statement"); }
	;

statement:
	  simple_stat st
	| if statement		{ $$ = stat3(IF, $1, $2, nullstat); }
	| if statement else statement
		{ $$ = stat3(IF, $1, $2, $4); }
	| while statement	{ $$ = stat2(WHILE, $1, $2); }
	| for		
	| NEXT st		{ $$ = stat1(NEXT, NULL); }
	| EXIT st		{ $$ = stat1(EXIT, NULL); }
	| EXIT expr st		{ $$ = stat1(EXIT, $2); }
	| BREAK st		{ $$ = stat1(BREAK, NULL); }
	| CONTINUE st		{ $$ = stat1(CONTINUE, NULL); }
	| '{' stat_list '}'	{ $$ = $2; }
	;

stat_list:
	  stat_list statement	{ $$ = linkum($1, $2); }
	|			{ $$ = nullstat; }
	;

while:
	  WHILE '(' conditional ')' optNL	{ $$ = $3; }
	;

for:
	  FOR '(' simple_stat ';' conditional ';' simple_stat ')' optNL statement
		{ $$ = stat4(FOR, $3, $5, $7, $10); }
	| FOR '(' simple_stat ';'  ';' simple_stat ')' optNL statement
		{ $$ = stat4(FOR, $3, nullstat, $6, $9); }
	| FOR '(' VAR IN VAR ')' optNL statement
		{ $$ = stat3(IN, $3, $5, $8); }
	;

%%
