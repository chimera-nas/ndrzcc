/*
 * SPDX-FileCopyrightText: 2026 Chimera-NAS Project Contributors
 * SPDX-License-Identifier: LGPL-2.1-only
 */

/* %code requires lands in the generated y.tab.h, so types carried in the
 * semantic-value %union must be declared here (the lexer includes y.tab.h). */
%code requires {
#include <stdint.h>
#include "ndr.h"
}

%{

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ndr.h"

extern int yylex(void);
extern int line_num;
void yyerror(const char *s);

char *ndr_strdup(const char *str);

/* ----- parser-side scratch types ----- */
struct attr_set {
    int                in, out;
    enum ndr_ptr_class ptr;
    int                is_string;
    int                string_nonul;
    int                context_handle;
    char              *size_is;
    char              *length_is;
    char              *switch_is;
    char              *switch_type;
    int                v1_enum;
    int                has_uuid;
    uint8_t            uuid[16];
    int                has_version;
    uint16_t           vmaj, vmin;
    int                has_opnum;
    uint32_t           opnum;
};

/* ----- AST roots (read by ndrzcc.c) ----- */
struct ndr_interface *g_iface;
struct ndr_struct    *g_structs;
struct ndr_union     *g_unions;
struct ndr_enum      *g_enums;
struct ndr_typedef   *g_typedefs;
struct ndr_const     *g_consts;

static uint32_t       g_next_opnum;

/* ----- helpers ----- */
char *
ndr_strdup(const char *str)
{
    return str ? strdup(str) : NULL;
}

static char *
join3(const char *a, const char *b, const char *c)
{
    size_t n = strlen(a) + strlen(b) + strlen(c) + 1;
    char  *s = malloc(n);
    snprintf(s, n, "%s%s%s", a, b, c);
    return s;
}

static struct attr_set *
attr_new(void)
{
    return calloc(1, sizeof(struct attr_set));
}

static void
parse_uuid(struct attr_set *a, const char *u)
{
    unsigned int b[16];
    int          n;

    n = sscanf(u, "%2x%2x%2x%2x-%2x%2x-%2x%2x-%2x%2x-%2x%2x%2x%2x%2x%2x",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7],
               &b[8], &b[9], &b[10], &b[11], &b[12], &b[13], &b[14], &b[15]);
    if (n != 16) {
        yyerror("malformed uuid");
        return;
    }
    for (int i = 0; i < 16; i++) {
        a->uuid[i] = (uint8_t) b[i];
    }
    a->has_uuid = 1;
}

static void
attr_apply(struct attr_set *a, const char *name, char *arg)
{
    if (!strcmp(name, "in")) {
        a->in = 1;
    } else if (!strcmp(name, "out")) {
        a->out = 1;
    } else if (!strcmp(name, "nonul")) {
        a->string_nonul = 1;
    } else if (!strcmp(name, "string")) {
        a->is_string = 1;
    } else if (!strcmp(name, "unique")) {
        a->ptr = NDR_PTR_UNIQUE;
    } else if (!strcmp(name, "ref")) {
        a->ptr = NDR_PTR_REF;
    } else if (!strcmp(name, "ptr") || !strcmp(name, "full")) {
        a->ptr = NDR_PTR_FULL;
    } else if (!strcmp(name, "context_handle")) {
        a->context_handle = 1;
    } else if (!strcmp(name, "size_is") || !strcmp(name, "max_is")) {
        a->size_is = arg;
    } else if (!strcmp(name, "length_is")) {
        a->length_is = arg;
    } else if (!strcmp(name, "switch_is")) {
        a->switch_is = arg;
    } else if (!strcmp(name, "switch_type")) {
        a->switch_type = arg;
    } else if (!strcmp(name, "v1_enum")) {
        a->v1_enum = 1;
    } else if (!strcmp(name, "uuid")) {
        parse_uuid(a, arg);
    } else if (!strcmp(name, "version")) {
        sscanf(arg, "%hu.%hu", &a->vmaj, &a->vmin);
        a->has_version = 1;
    } else if (!strcmp(name, "opnum")) {
        a->opnum     = (uint32_t) strtoul(arg, NULL, 0);
        a->has_opnum = 1;
    } else {
        /* pointer_default, helpstring, public, etc -- ignored in v1 */
    }
}

static struct ndr_type *
make_type(struct attr_set *a, char *base, struct decl_tok d)
{
    struct ndr_type *t = calloc(1, sizeof(*t));

    t->name = base;
    t->nptr = d.stars;
    if (a) {
        t->ptr               = a->ptr;
        t->is_string         = a->is_string;
        t->string_nonul      = a->string_nonul;
        t->is_context_handle = a->context_handle;
        t->size_is           = a->size_is;
        t->conformant        = a->size_is ? 1 : 0;
        t->length_is         = a->length_is;
        t->varying           = a->length_is ? 1 : 0;
        t->switch_is         = a->switch_is;
    }
    if (d.fixed) {
        t->fixed      = 1;
        t->fixed_size = d.fixed_size;
    }
    t->is_sid = (!strcmp(base, "RPC_SID") || !strcmp(base, "dom_sid"));
    return t;
}

static struct ndr_struct *
struct_register(char *name, struct ndr_member *members)
{
    struct ndr_struct *s = calloc(1, sizeof(*s));
    struct ndr_member *m;

    s->name    = name;
    s->members = members;

    /* The conformant member (if any) must be last; remember it for hoisting. */
    NDR_FOREACH(m, members) {
        if (m->type->conformant) {
            s->has_conformant    = 1;
            s->conformant_member = m;
        }
    }
    NDR_APPEND(g_structs, s);
    return s;
}

static void
iface_begin(struct attr_set *a, char *name)
{
    g_iface       = calloc(1, sizeof(*g_iface));
    g_iface->name = name;
    if (a && a->has_uuid) {
        memcpy(g_iface->uuid, a->uuid, 16);
    }
    if (a && a->has_version) {
        g_iface->ver_major = a->vmaj;
        g_iface->ver_minor = a->vmin;
    }
    g_next_opnum = 0;
}

%}

%union {
    char                  *str;
    int                    ival;
    struct ndr_type       *type;
    struct attr_set       *attrs;
    struct ndr_member     *member;
    struct ndr_union_case *ucase;
    struct ndr_param      *param;
    struct ndr_enum_entry *eentry;
    struct attr_tok        atok;
    struct decl_tok        dtok;
}

%token INTERFACE TYPEDEF STRUCT ENUM UNION SWITCH CASE DEFAULT CONST VOID
%token LBRACKET RBRACKET LBRACE RBRACE LPAREN RPAREN SEMICOLON COMMA
%token STAR LANGLE RANGLE EQUALS DOT SLASH COLON
%token <str> IDENTIFIER NUMBER UUID

%type <attrs> attr_list_opt attr_list attrs
%type <atok>  attr_one
%type <str>   attr_arg type_ref ret_type
%type <ival>  stars
%type <dtok>  declarator
%type <member> member member_list
%type <param>  param param_list param_list_opt
%type <ucase>  ucase ucase_list
%type <eentry> enum_entry enum_list

%%

file
    : items
    ;

items
    : items item
    | /* empty */
    ;

item
    : interface_def
    | typedef_decl
    | struct_decl
    | enum_decl
    | union_decl
    | const_decl
    ;

interface_def
    : attr_list_opt INTERFACE IDENTIFIER LBRACE { iface_begin($1, $3); } body RBRACE opt_semi
    ;

opt_semi
    : SEMICOLON
    | /* empty */
    ;

body
    : body body_decl
    | /* empty */
    ;

body_decl
    : typedef_decl
    | struct_decl
    | enum_decl
    | union_decl
    | const_decl
    | op_decl
    ;

/* ---------------- attributes ---------------- */

attr_list_opt
    : attr_list        { $$ = $1; }
    | /* empty */      { $$ = attr_new(); }
    ;

attr_list
    : LBRACKET attrs RBRACKET { $$ = $2; }
    ;

attrs
    : attr_one              { $$ = attr_new(); attr_apply($$, $1.name, $1.arg); }
    | attrs COMMA attr_one  { $$ = $1; attr_apply($$, $3.name, $3.arg); }
    ;

attr_one
    : IDENTIFIER                          { $$.name = $1; $$.arg = NULL; }
    | IDENTIFIER LPAREN attr_arg RPAREN   { $$.name = $1; $$.arg = $3; }
    ;

attr_arg
    : UUID                       { $$ = $1; }
    | NUMBER                     { $$ = $1; }
    | NUMBER DOT NUMBER          { $$ = join3($1, ".", $3); }
    | STAR                       { $$ = ndr_strdup("*"); }
    | IDENTIFIER                 { $$ = $1; }
    | IDENTIFIER SLASH NUMBER    { $$ = join3($1, "/", $3); }
    ;

/* ---------------- types & declarators ---------------- */

type_ref
    : IDENTIFIER { $$ = $1; }
    | VOID       { $$ = ndr_strdup("void"); }
    ;

ret_type
    : IDENTIFIER { $$ = $1; }
    | VOID       { $$ = ndr_strdup("void"); }
    ;

stars
    : stars STAR  { $$ = $1 + 1; }
    | /* empty */ { $$ = 0; }
    ;

declarator
    : stars IDENTIFIER                          { $$.stars = $1; $$.name = $2; $$.fixed = 0; $$.fixed_size = NULL; }
    | stars IDENTIFIER LBRACKET NUMBER RBRACKET { $$.stars = $1; $$.name = $2; $$.fixed = 1; $$.fixed_size = $4; }
    | stars IDENTIFIER LBRACKET STAR RBRACKET   { $$.stars = $1; $$.name = $2; $$.fixed = 0; $$.fixed_size = NULL; }
    | stars IDENTIFIER LBRACKET RBRACKET        { $$.stars = $1; $$.name = $2; $$.fixed = 0; $$.fixed_size = NULL; }
    ;

/* ---------------- typedef / struct / enum / union ---------------- */

typedef_decl
    : TYPEDEF STRUCT LBRACE member_list RBRACE IDENTIFIER SEMICOLON
        { struct_register($6, $4); }
    | TYPEDEF ENUM LBRACE enum_list RBRACE IDENTIFIER SEMICOLON
        { struct ndr_enum *e = calloc(1, sizeof(*e)); e->name = $6; e->entries = $4; NDR_APPEND(g_enums, e); }
    | TYPEDEF type_ref declarator SEMICOLON
        {
            struct ndr_typedef *td = calloc(1, sizeof(*td));
            struct decl_tok     d  = $3;
            td->name = d.name;
            td->type = make_type(NULL, $2, d);
            NDR_APPEND(g_typedefs, td);
        }
    ;

struct_decl
    : STRUCT IDENTIFIER LBRACE member_list RBRACE SEMICOLON
        { struct_register($2, $4); }
    ;

enum_decl
    : ENUM IDENTIFIER LBRACE enum_list RBRACE SEMICOLON
        { struct ndr_enum *e = calloc(1, sizeof(*e)); e->name = $2; e->entries = $4; NDR_APPEND(g_enums, e); }
    ;

enum_list
    : enum_entry                  { $$ = NULL; NDR_APPEND($$, $1); }
    | enum_list COMMA enum_entry  { $$ = $1; NDR_APPEND($$, $3); }
    | enum_list COMMA             { $$ = $1; }
    ;

enum_entry
    : IDENTIFIER                  { $$ = calloc(1, sizeof(struct ndr_enum_entry)); $$->name = $1; }
    | IDENTIFIER EQUALS NUMBER    { $$ = calloc(1, sizeof(struct ndr_enum_entry)); $$->name = $1; $$->value = $3; }
    ;

union_decl
    : attr_list_opt UNION IDENTIFIER SWITCH LPAREN type_ref IDENTIFIER RPAREN LBRACE ucase_list RBRACE SEMICOLON
        {
            struct ndr_union *u = calloc(1, sizeof(*u));
            u->name         = $3;
            u->switch_type  = $6;
            u->encapsulated = 1;
            u->cases        = $10;
            NDR_APPEND(g_unions, u);
        }
    ;

ucase_list
    : ucase             { $$ = NULL; NDR_APPEND($$, $1); }
    | ucase_list ucase  { $$ = $1; NDR_APPEND($$, $2); }
    ;

ucase
    : CASE NUMBER COLON member
        { $$ = calloc(1, sizeof(struct ndr_union_case)); $$->label = $2; $$->type = $4->type; $$->name = $4->name; }
    | CASE IDENTIFIER COLON member
        { $$ = calloc(1, sizeof(struct ndr_union_case)); $$->label = $2; $$->type = $4->type; $$->name = $4->name; }
    | DEFAULT COLON member
        { $$ = calloc(1, sizeof(struct ndr_union_case)); $$->is_default = 1; $$->type = $3->type; $$->name = $3->name; }
    | DEFAULT COLON VOID SEMICOLON
        { $$ = calloc(1, sizeof(struct ndr_union_case)); $$->is_default = 1; $$->voided = 1; }
    ;

/* ---------------- members ---------------- */

member_list
    : member              { $$ = NULL; NDR_APPEND($$, $1); }
    | member_list member  { $$ = $1; NDR_APPEND($$, $2); }
    ;

member
    : attr_list_opt type_ref declarator SEMICOLON
        {
            $$ = calloc(1, sizeof(struct ndr_member));
            $$->type = make_type($1, $2, $3);
            $$->name = $3.name;
        }
    ;

/* ---------------- operations ---------------- */

op_decl
    : attr_list_opt ret_type IDENTIFIER LPAREN param_list_opt RPAREN SEMICOLON
        {
            struct ndr_op  *op = calloc(1, sizeof(*op));
            struct decl_tok rd = { 0, NULL, 0, NULL };
            op->name        = $3;
            op->return_type = make_type(NULL, $2, rd);
            op->params      = $5;
            op->opnum       = $1->has_opnum ? $1->opnum : g_next_opnum;
            g_next_opnum    = op->opnum + 1;
            if (g_iface) {
                NDR_APPEND(g_iface->ops, op);
            }
        }
    ;

param_list_opt
    : param_list  { $$ = $1; }
    | VOID        { $$ = NULL; }
    | /* empty */ { $$ = NULL; }
    ;

param_list
    : param                   { $$ = NULL; NDR_APPEND($$, $1); }
    | param_list COMMA param  { $$ = $1; NDR_APPEND($$, $3); }
    ;

param
    : attr_list_opt type_ref declarator
        {
            $$ = calloc(1, sizeof(struct ndr_param));
            $$->type = make_type($1, $2, $3);
            $$->name = $3.name;
            $$->in   = $1->in;
            $$->out  = $1->out;
            if (!$$->in && !$$->out) {
                $$->in = 1;   /* MIDL default */
            }
        }
    ;

/* ---------------- const ---------------- */

const_decl
    : CONST type_ref IDENTIFIER EQUALS NUMBER SEMICOLON
        {
            struct ndr_const *c = calloc(1, sizeof(*c));
            c->name  = $3;
            c->value = $5;
            NDR_APPEND(g_consts, c);
        }
    ;

%%

void
yyerror(const char *s)
{
    fprintf(stderr, "ndrzcc: parse error at line %d: %s\n", line_num, s);
}
