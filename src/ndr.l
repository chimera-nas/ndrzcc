%{

/*
 * SPDX-FileCopyrightText: 2026 Chimera-NAS Project Contributors
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#include "y.tab.h"
int line_num = 1;
int column_num = 1;

char *ndr_strdup(const char *str);

%}

%x C_COMMENT

%%

"/*"            { column_num += yyleng; BEGIN(C_COMMENT); }
<C_COMMENT>"*/" { column_num += yyleng; BEGIN(INITIAL); }
<C_COMMENT>\n   { line_num++; column_num = 1; }
<C_COMMENT>.    { column_num++; }

"//".*          { /* line comment */ }

\n              { line_num++; column_num = 1; }

"interface"     { column_num += yyleng; return INTERFACE; }
"typedef"       { column_num += yyleng; return TYPEDEF; }
"struct"        { column_num += yyleng; return STRUCT; }
"enum"          { column_num += yyleng; return ENUM; }
"union"         { column_num += yyleng; return UNION; }
"switch"        { column_num += yyleng; return SWITCH; }
"case"          { column_num += yyleng; return CASE; }
"default"       { column_num += yyleng; return DEFAULT; }
"const"         { column_num += yyleng; return CONST; }
"void"          { column_num += yyleng; return VOID; }

"["             { column_num++; return LBRACKET; }
"]"             { column_num++; return RBRACKET; }
"{"             { column_num++; return LBRACE; }
"}"             { column_num++; return RBRACE; }
"("             { column_num++; return LPAREN; }
")"             { column_num++; return RPAREN; }
";"             { column_num++; return SEMICOLON; }
","             { column_num++; return COMMA; }
"*"             { column_num++; return STAR; }
"<"             { column_num++; return LANGLE; }
">"             { column_num++; return RANGLE; }
"="             { column_num++; return EQUALS; }
"."             { column_num++; return DOT; }
"/"             { column_num++; return SLASH; }
":"             { column_num++; return COLON; }

[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12} {
                  column_num += yyleng; yylval.str = ndr_strdup(yytext); return UUID; }

0x[0-9A-Fa-f]+  { column_num += yyleng; yylval.str = ndr_strdup(yytext); return NUMBER; }
[0-9]+          { column_num += yyleng; yylval.str = ndr_strdup(yytext); return NUMBER; }
[a-zA-Z_][a-zA-Z0-9_]* { column_num += yyleng; yylval.str = ndr_strdup(yytext); return IDENTIFIER; }

[ \t\r]+        { column_num += yyleng; }

.               { printf("Unknown character: %s at line %d, column %d\n", yytext, line_num, column_num); }

%%

int yywrap(void) {
    return 1;
}
