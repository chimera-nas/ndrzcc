// SPDX-FileCopyrightText: 2026 Chimera-NAS Project Contributors
//
// SPDX-License-Identifier: LGPL-2.1-only

/*
 * ndrzcc runtime support (out-of-line helpers).
 *
 * Embedded verbatim into every generated .c file, so it depends only on libc
 * and the declarations in ndr_builtin.h (also embedded).
 */

#ifndef NDR_BUILTIN_H
/* Direct (in-tree) compilation; when embedded into generated code the header
 * content is already present and this include is skipped. */
#include "ndr_builtin.h"
#endif /* ifndef NDR_BUILTIN_H */

/* ----------------------------------------------------------------------------
 * RPC_SID
 * --------------------------------------------------------------------------*/
void
ndr_push_sid(
    struct ndr_writer    *w,
    int                   flags,
    const struct ndr_sid *sid)
{
    /* A SID embeds a conformant array (sub_authority[size_is]); its max_count
     * is hoisted to the front of the structure on the wire. */
    if (flags & NDR_SCALARS) {
        ndr_put_u32(w, sid->sub_authority_count);   /* hoisted conformant count */
        ndr_put_u8(w, sid->revision);
        ndr_put_u8(w, sid->sub_authority_count);
        /* 6-byte identifier authority, BIG-endian -- copied as-is, callers
         * store it most-significant-byte-first. */
        ndr_put_bytes(w, sid->identifier_authority, 6);
        for (uint8_t i = 0; i < sid->sub_authority_count; i++) {
            ndr_put_u32(w, sid->sub_authority[i]);
        }
    }
} /* ndr_push_sid */

void
ndr_pull_sid(
    struct ndr_cursor *c,
    int                flags,
    struct ndr_sid    *sid)
{
    if (flags & NDR_SCALARS) {
        uint32_t max_count = ndr_get_u32(c);   /* hoisted conformant count */
        (void) max_count;
        sid->revision            = ndr_get_u8(c);
        sid->sub_authority_count = ndr_get_u8(c);
        ndr_get_bytes(c, sid->identifier_authority, 6);
        if (sid->sub_authority_count > NDR_SID_MAX_SUB_AUTH) {
            c->error = 1;
            return;
        }
        for (uint8_t i = 0; i < sid->sub_authority_count; i++) {
            sid->sub_authority[i] = ndr_get_u32(c);
        }
    }
} /* ndr_pull_sid */

/* ----------------------------------------------------------------------------
 * Conformant-varying UTF-16 string body ([string] wchar_t *).
 * --------------------------------------------------------------------------*/
void
ndr_push_wstring(
    struct ndr_writer *w,
    const char        *utf8)
{
    uint16_t buf[2048];
    int      n;
    uint32_t count;

    if (!utf8) {
        utf8 = "";
    }

    n = ndr_utf8_to_utf16le(utf8, buf, sizeof(buf) / sizeof(buf[0]) - 1);
    if (n < 0) {
        w->error = 1;
        return;
    }

    /* [string] arrays carry the terminating NUL in both counts. */
    buf[n] = 0;
    count  = (uint32_t) n + 1;

    ndr_put_u32(w, count);   /* max_count   */
    ndr_put_u32(w, 0);       /* offset      */
    ndr_put_u32(w, count);   /* actual_count */
    for (uint32_t i = 0; i < count; i++) {
        ndr_put_u16(w, buf[i]);
    }
} /* ndr_push_wstring */

char *
ndr_pull_wstring(
    struct ndr_cursor *c,
    struct ndr_dbuf   *d)
{
    uint32_t max_count, offset, actual_count;
    uint16_t buf[2048];
    char    *out;
    int      n;

    max_count = ndr_get_u32(c);
    offset    = ndr_get_u32(c);
    (void) max_count;
    actual_count = ndr_get_u32(c);

    if (offset != 0 || actual_count > sizeof(buf) / sizeof(buf[0])) {
        c->error = 1;
        return NULL;
    }

    for (uint32_t i = 0; i < actual_count; i++) {
        buf[i] = ndr_get_u16(c);
    }
    if (c->error) {
        return NULL;
    }

    /* Drop a trailing NUL code unit if present. */
    if (actual_count > 0 && buf[actual_count - 1] == 0) {
        actual_count--;
    }

    out = ndr_dbuf_alloc(d, actual_count * 4 + 1);   /* worst-case UTF-8 expansion */
    if (!out) {
        c->error = 1;
        return NULL;
    }

    n = ndr_utf16le_to_utf8(buf, actual_count, out, actual_count * 4 + 1);
    if (n < 0) {
        c->error = 1;
        return NULL;
    }
    return out;
} /* ndr_pull_wstring */

/* ----------------------------------------------------------------------------
 * Operation lookup
 * --------------------------------------------------------------------------*/
const struct ndr_op_desc *
ndr_find_op(
    const struct ndr_op_desc *table,
    int                       count,
    int                       opnum)
{
    for (int i = 0; i < count; i++) {
        if (table[i].opnum == (uint16_t) opnum) {
            return &table[i];
        }
    }
    return NULL;
} /* ndr_find_op */
