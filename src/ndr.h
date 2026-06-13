// SPDX-FileCopyrightText: 2026 Chimera-NAS Project Contributors
//
// SPDX-License-Identifier: LGPL-2.1-only

/*
 * Abstract syntax tree for the ndrzcc MS-IDL subset.
 *
 * Modelled on xdrzcc's xdr.h, extended for the constructs NDR needs that XDR
 * does not: pointer classes (ref/unique/ptr), per-parameter direction,
 * size_is/length_is/switch_is attribute bindings to sibling fields, and
 * interface/operation nodes carrying a UUID and opnums.
 */

#pragma once

#ifndef NDR_H
#define NDR_H

#include <stdint.h>

/* Minimal doubly-linked-list append (tail tracked via head->prev). */
#define NDR_APPEND(head, node)             \
        do {                                   \
            (node)->next = NULL;               \
            if (!(head)) {                     \
                (node)->prev = (node);         \
                (head)       = (node);         \
            } else {                           \
                (node)->prev       = (head)->prev; \
                (head)->prev->next = (node);   \
                (head)->prev       = (node);   \
            }                                  \
        } while (0)

#define NDR_FOREACH(var, head) \
        for ((var) = (head); (var); (var) = (var)->next)

/* Parser-internal scratch types carried in the Bison semantic-value union;
 * declared here so both the generated header and the parser prologue see them. */
struct attr_tok { char *name; char *arg; };
struct decl_tok { int stars; char *name; int fixed; char *fixed_size; };

enum ndr_ptr_class {
    NDR_PTR_NONE = 0,
    NDR_PTR_REF,
    NDR_PTR_UNIQUE,
    NDR_PTR_FULL,
};

/* A use of a type: the base type name plus all the IDL decoration that applies
 * at the point of use (pointer class, array shape, attribute bindings). */
struct ndr_type {
    char *name;                       /* base type name */
    int   builtin;                    /* one of NDR_BUILTIN_* (0 = named type) */
    int   nptr;                       /* number of '*' pointer levels */
    enum ndr_ptr_class ptr;           /* pointer decoration */
    int   is_string;                  /* [string] -> conformant-varying wchar */
    int   is_context_handle;
    int   is_sid;                     /* base type is the well-known RPC_SID */

    /* arrays */
    int   fixed;                      /* fixed [N] array */
    char *fixed_size;                 /* N (literal or const name) */
    int   conformant;                 /* [size_is] present */
    char *size_is;                    /* conformant max_count expr (sibling) */
    int   varying;                    /* [length_is] present */
    char *length_is;                  /* varying actual_count expr (sibling) */

    /* union discriminant binding for a [switch_is] member */
    char *switch_is;
};

#define NDR_BUILTIN_NONE   0
#define NDR_BUILTIN_UINT8  1
#define NDR_BUILTIN_UINT16 2
#define NDR_BUILTIN_UINT32 3
#define NDR_BUILTIN_UINT64 4
#define NDR_BUILTIN_INT8   5
#define NDR_BUILTIN_INT16  6
#define NDR_BUILTIN_INT32  7
#define NDR_BUILTIN_INT64  8
#define NDR_BUILTIN_BOOL   9
#define NDR_BUILTIN_WCHAR  10
#define NDR_BUILTIN_VOID   11

struct ndr_const {
    char             *name;
    char             *value;
    struct ndr_const *prev;
    struct ndr_const *next;
};

struct ndr_enum_entry {
    char                  *name;
    char                  *value;
    struct ndr_enum_entry *prev;
    struct ndr_enum_entry *next;
};

struct ndr_enum {
    char                  *name;
    int                    v1_enum;   /* 32-bit on the wire instead of 16-bit */
    struct ndr_enum_entry *entries;
    struct ndr_enum       *prev;
    struct ndr_enum       *next;
};

struct ndr_member {
    struct ndr_type   *type;
    char              *name;
    struct ndr_member *prev;
    struct ndr_member *next;
};

struct ndr_struct {
    char              *name;
    int                has_conformant;        /* trailing conformant member */
    struct ndr_member *conformant_member;     /* the one, if any */
    struct ndr_member *members;
    struct ndr_struct *prev;
    struct ndr_struct *next;
};

struct ndr_union_case {
    char                  *label;     /* case value, or NULL for default */
    int                    is_default;
    int                    voided;    /* empty arm */
    struct ndr_type       *type;
    char                  *name;
    struct ndr_union_case *prev;
    struct ndr_union_case *next;
};

struct ndr_union {
    char                  *name;
    int                    encapsulated;   /* discriminant emitted inline */
    char                  *switch_type;    /* discriminant type name */
    struct ndr_union_case *cases;
    struct ndr_union      *prev;
    struct ndr_union      *next;
};

struct ndr_param {
    struct ndr_type  *type;
    char             *name;
    int               in;
    int               out;
    struct ndr_param *prev;
    struct ndr_param *next;
};

struct ndr_op {
    char             *name;
    uint32_t          opnum;
    struct ndr_type  *return_type;
    struct ndr_param *params;
    struct ndr_op    *prev;
    struct ndr_op    *next;
};

struct ndr_interface {
    char          *name;
    uint8_t        uuid[16];
    uint16_t       ver_major;
    uint16_t       ver_minor;
    struct ndr_op *ops;
};

/* A typedef binds a name to a type-or-aggregate.  When the right-hand side is a
 * struct/union/enum definition, the corresponding pointer is set. */
struct ndr_typedef {
    char               *name;
    struct ndr_type    *type;     /* simple typedef target, or NULL */
    struct ndr_typedef *prev;
    struct ndr_typedef *next;
};

#endif /* NDR_H */
