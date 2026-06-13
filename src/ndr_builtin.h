// SPDX-FileCopyrightText: 2026 Chimera-NAS Project Contributors
//
// SPDX-License-Identifier: LGPL-2.1-only

/*
 * ndrzcc runtime support.
 *
 * This header is embedded verbatim into every generated .h file (as a C string
 * constant), so it must be entirely self-contained: it depends only on libc.
 *
 * It implements the DCE/RPC NDR transfer syntax (NDR32,
 * 8a885d04-1ceb-11c9-9fe8-08002b104860): little-endian primitives with natural
 * alignment relative to the start of the stub octet stream, referent pointers,
 * conformant/varying arrays and conformant-varying UTF-16 strings.
 *
 * Marshalling follows the two-pass model: every constructed type is visited
 * once with NDR_SCALARS (emitting fixed fields and referent ids inline) and
 * once with NDR_BUFFERS (emitting the deferred pointer/array bodies, which
 * recurse).  Doing both passes over a parameter list in turn yields exactly
 * NDR's "all fixed parts first, then all deferred bodies" ordering.
 */

#pragma once

#ifndef NDR_BUILTIN_H
#define NDR_BUILTIN_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#ifndef NDR_LIKELY
#define NDR_LIKELY(x)   __builtin_expect(!!(x), 1)
#define NDR_UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif /* ifndef NDR_LIKELY */

/* Marshalling passes (bitmask, may be combined for a full single-shot walk). */
#define NDR_SCALARS 0x01
#define NDR_BUFFERS 0x02

/* ----------------------------------------------------------------------------
 * ndr_dbuf: bump-allocator arena that backs everything produced by the pull
 * (unmarshall) path.  Strings, arrays and out-of-line structs point into it, so
 * it must outlive the unmarshalled value and is freed in one shot.
 * --------------------------------------------------------------------------*/
/* A chunk of arena storage; the payload follows the header inline at
 * (uint8_t *)(blk + 1).  Chunks are singly linked newest-first and are never
 * resized or moved once allocated, so every pointer ndr_dbuf_alloc returns
 * stays valid until ndr_dbuf_destroy -- callers (e.g. the RPC dispatcher) hold
 * the in/out structs across many later allocations. */
struct ndr_dbuf_block {
    struct ndr_dbuf_block *next;
    size_t                 cap;
    size_t                 used;
};

struct ndr_dbuf {
    struct ndr_dbuf_block *head;   /* newest block; allocations bump from here */
    size_t                 hint;   /* default capacity for a fresh block */
};

static inline int
ndr_dbuf_init(
    struct ndr_dbuf *d,
    size_t           hint)
{
    if (hint < 256) {
        hint = 256;
    }
    d->head = NULL;   /* blocks are allocated lazily on first ndr_dbuf_alloc */
    d->hint = hint;
    return 0;
} /* ndr_dbuf_init */

static inline void *
ndr_dbuf_alloc(
    struct ndr_dbuf *d,
    size_t           len)
{
    struct ndr_dbuf_block *blk = d->head;
    void                  *p;

    /* 8-byte align every allocation so embedded scalars stay aligned. */
    if (blk) {
        blk->used = (blk->used + 7) & ~((size_t) 7);
    }

    if (NDR_UNLIKELY(!blk || blk->used + len > blk->cap)) {
        /* Grow by linking a NEW block rather than realloc'ing the current one,
         * so previously returned pointers never move.  Outsize requests get a
         * dedicated block sized to fit. */
        size_t cap = d->hint;
        if (cap < len) {
            cap = len;
        }
        blk = malloc(sizeof(*blk) + cap);
        if (NDR_UNLIKELY(!blk)) {
            return NULL;
        }
        blk->cap  = cap;
        blk->used = 0;
        blk->next = d->head;
        d->head   = blk;
    }

    p          = (uint8_t *) (blk + 1) + blk->used;
    blk->used += len;
    memset(p, 0, len);
    return p;
} /* ndr_dbuf_alloc */

static inline void
ndr_dbuf_destroy(struct ndr_dbuf *d)
{
    struct ndr_dbuf_block *blk = d->head, *next;

    while (blk) {
        next = blk->next;
        free(blk);
        blk = next;
    }
    d->head = NULL;
} /* ndr_dbuf_destroy */

/* ----------------------------------------------------------------------------
 * ndr_writer: marshals into a flat, caller-owned buffer.  off is measured from
 * octet 0 of the stub, which is the NDR alignment origin, so alignment is just
 * `off & (n - 1)`.
 * --------------------------------------------------------------------------*/
struct ndr_writer {
    uint8_t *base;
    size_t   off;
    size_t   cap;
    int      error;       /* sticky: set on overflow */
    uint32_t next_refid;  /* referent-id allocator */
};

static inline void
ndr_writer_init(
    struct ndr_writer *w,
    void              *base,
    size_t             cap)
{
    w->base       = base;
    w->off        = 0;
    w->cap        = cap;
    w->error      = 0;
    w->next_refid = 0x00020000;
} /* ndr_writer_init */

static inline int
ndr_writer_length(const struct ndr_writer *w)
{
    return w->error ? -1 : (int) w->off;
} /* ndr_writer_length */

static inline int
ndr_writer_room(
    struct ndr_writer *w,
    size_t             n)
{
    if (NDR_UNLIKELY(w->off + n > w->cap)) {
        w->error = 1;
        return 0;
    }
    return 1;
} /* ndr_writer_room */

static inline void
ndr_align_write(
    struct ndr_writer *w,
    int                n)
{
    size_t pad = ((size_t) (-(intptr_t) w->off)) & (size_t) (n - 1);

    if (!ndr_writer_room(w, pad)) {
        return;
    }
    while (pad--) {
        w->base[w->off++] = 0;
    }
} /* ndr_align_write */

static inline void
ndr_put_u8(
    struct ndr_writer *w,
    uint8_t            v)
{
    if (!ndr_writer_room(w, 1)) {
        return;
    }
    w->base[w->off++] = v;
} /* ndr_put_u8 */

static inline void
ndr_put_u16(
    struct ndr_writer *w,
    uint16_t           v)
{
    ndr_align_write(w, 2);
    if (!ndr_writer_room(w, 2)) {
        return;
    }
    w->base[w->off++] = (uint8_t) (v & 0xff);
    w->base[w->off++] = (uint8_t) (v >> 8);
} /* ndr_put_u16 */

static inline void
ndr_put_u32(
    struct ndr_writer *w,
    uint32_t           v)
{
    ndr_align_write(w, 4);
    if (!ndr_writer_room(w, 4)) {
        return;
    }
    w->base[w->off++] = (uint8_t) (v & 0xff);
    w->base[w->off++] = (uint8_t) ((v >> 8) & 0xff);
    w->base[w->off++] = (uint8_t) ((v >> 16) & 0xff);
    w->base[w->off++] = (uint8_t) ((v >> 24) & 0xff);
} /* ndr_put_u32 */

static inline void
ndr_put_u64(
    struct ndr_writer *w,
    uint64_t           v)
{
    ndr_align_write(w, 8);
    if (!ndr_writer_room(w, 8)) {
        return;
    }
    for (int i = 0; i < 8; i++) {
        w->base[w->off++] = (uint8_t) ((v >> (8 * i)) & 0xff);
    }
} /* ndr_put_u64 */

static inline void
ndr_put_bytes(
    struct ndr_writer *w,
    const void        *src,
    size_t             len)
{
    if (!ndr_writer_room(w, len)) {
        return;
    }
    memcpy(w->base + w->off, src, len);
    w->off += len;
} /* ndr_put_bytes */

/*
 * Emit a referent id for an embedded pointer: 0 for NULL, otherwise a fresh
 * non-zero id.  (Top-level [ref] pointers emit no id at all and must not call
 * this.)  Returns the id written so the caller can decide whether to emit the
 * referent body in the BUFFERS pass.
 */
static inline uint32_t
ndr_put_ref(
    struct ndr_writer *w,
    const void        *ptr)
{
    uint32_t id = 0;

    if (ptr) {
        id             = w->next_refid;
        w->next_refid += 4;
    }
    ndr_put_u32(w, id);
    return id;
} /* ndr_put_ref */

/* ----------------------------------------------------------------------------
 * ndr_cursor: pull-side reader over a flat stub buffer.  off is stub-relative,
 * matching the writer, so alignment is symmetric.
 * --------------------------------------------------------------------------*/
struct ndr_cursor {
    const uint8_t *base;
    size_t         off;
    size_t         len;
    int            error;  /* sticky: set on underflow */
};

static inline void
ndr_cursor_init(
    struct ndr_cursor *c,
    const void        *base,
    size_t             len)
{
    c->base  = base;
    c->off   = 0;
    c->len   = len;
    c->error = 0;
} /* ndr_cursor_init */

static inline int
ndr_cursor_avail(
    struct ndr_cursor *c,
    size_t             n)
{
    if (NDR_UNLIKELY(c->off + n > c->len)) {
        c->error = 1;
        return 0;
    }
    return 1;
} /* ndr_cursor_avail */

static inline void
ndr_align_read(
    struct ndr_cursor *c,
    int                n)
{
    size_t pad = ((size_t) (-(intptr_t) c->off)) & (size_t) (n - 1);

    if (ndr_cursor_avail(c, pad)) {
        c->off += pad;
    }
} /* ndr_align_read */

static inline uint8_t
ndr_get_u8(struct ndr_cursor *c)
{
    if (!ndr_cursor_avail(c, 1)) {
        return 0;
    }
    return c->base[c->off++];
} /* ndr_get_u8 */

static inline uint16_t
ndr_get_u16(struct ndr_cursor *c)
{
    uint16_t v;

    ndr_align_read(c, 2);
    if (!ndr_cursor_avail(c, 2)) {
        return 0;
    }
    v       = (uint16_t) c->base[c->off] | ((uint16_t) c->base[c->off + 1] << 8);
    c->off += 2;
    return v;
} /* ndr_get_u16 */

static inline uint32_t
ndr_get_u32(struct ndr_cursor *c)
{
    uint32_t v;

    ndr_align_read(c, 4);
    if (!ndr_cursor_avail(c, 4)) {
        return 0;
    }
    v = (uint32_t) c->base[c->off] |
        ((uint32_t) c->base[c->off + 1] << 8) |
        ((uint32_t) c->base[c->off + 2] << 16) |
        ((uint32_t) c->base[c->off + 3] << 24);
    c->off += 4;
    return v;
} /* ndr_get_u32 */

static inline uint64_t
ndr_get_u64(struct ndr_cursor *c)
{
    uint64_t v = 0;

    ndr_align_read(c, 8);
    if (!ndr_cursor_avail(c, 8)) {
        return 0;
    }
    for (int i = 0; i < 8; i++) {
        v |= (uint64_t) c->base[c->off + i] << (8 * i);
    }
    c->off += 8;
    return v;
} /* ndr_get_u64 */

static inline void
ndr_get_bytes(
    struct ndr_cursor *c,
    void              *dst,
    size_t             len)
{
    if (!ndr_cursor_avail(c, len)) {
        memset(dst, 0, len);
        return;
    }
    memcpy(dst, c->base + c->off, len);
    c->off += len;
} /* ndr_get_bytes */

static inline void
ndr_cursor_skip(
    struct ndr_cursor *c,
    size_t             len)
{
    if (ndr_cursor_avail(c, len)) {
        c->off += len;
    }
} /* ndr_cursor_skip */

/* ----------------------------------------------------------------------------
 * UTF-8 <-> UTF-16LE.  Kept inline and self-contained so the runtime needs no
 * iconv/ICU.  Returns the number of UTF-16 code units produced/consumed, or -1
 * on malformed input.
 * --------------------------------------------------------------------------*/
static inline int
ndr_utf8_to_utf16le(
    const char *src,
    uint16_t   *dst,
    size_t      dstmax)
{
    const uint8_t *s = (const uint8_t *) src;
    size_t         n = 0;

    while (*s) {
        uint32_t cp;
        if (*s < 0x80) {
            cp = *s++;
        } else if ((*s & 0xe0) == 0xc0) {
            cp  = (uint32_t) (*s++ & 0x1f) << 6;
            cp |= (*s++ & 0x3f);
        } else if ((*s & 0xf0) == 0xe0) {
            cp  = (uint32_t) (*s++ & 0x0f) << 12;
            cp |= (uint32_t) (*s++ & 0x3f) << 6;
            cp |= (*s++ & 0x3f);
        } else if ((*s & 0xf8) == 0xf0) {
            cp  = (uint32_t) (*s++ & 0x07) << 18;
            cp |= (uint32_t) (*s++ & 0x3f) << 12;
            cp |= (uint32_t) (*s++ & 0x3f) << 6;
            cp |= (*s++ & 0x3f);
        } else {
            return -1;
        }

        if (cp < 0x10000) {
            if (n + 1 > dstmax) {
                return -1;
            }
            dst[n++] = (uint16_t) cp;
        } else {
            cp -= 0x10000;
            if (n + 2 > dstmax) {
                return -1;
            }
            dst[n++] = (uint16_t) (0xd800 + (cp >> 10));
            dst[n++] = (uint16_t) (0xdc00 + (cp & 0x3ff));
        }
    }
    return (int) n;
} /* ndr_utf8_to_utf16le */

static inline int
ndr_utf16le_to_utf8(
    const uint16_t *src,
    size_t          srclen,
    char           *dst,
    size_t          dstmax)
{
    size_t i = 0, n = 0;

    while (i < srclen) {
        uint32_t cp = src[i++];
        if (cp >= 0xd800 && cp <= 0xdbff && i < srclen) {
            uint32_t lo = src[i];
            if (lo >= 0xdc00 && lo <= 0xdfff) {
                cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
                i++;
            }
        }
        if (cp < 0x80) {
            if (n + 1 >= dstmax) {
                return -1;
            }
            dst[n++] = (char) cp;
        } else if (cp < 0x800) {
            if (n + 2 >= dstmax) {
                return -1;
            }
            dst[n++] = (char) (0xc0 | (cp >> 6));
            dst[n++] = (char) (0x80 | (cp & 0x3f));
        } else if (cp < 0x10000) {
            if (n + 3 >= dstmax) {
                return -1;
            }
            dst[n++] = (char) (0xe0 | (cp >> 12));
            dst[n++] = (char) (0x80 | ((cp >> 6) & 0x3f));
            dst[n++] = (char) (0x80 | (cp & 0x3f));
        } else {
            if (n + 4 >= dstmax) {
                return -1;
            }
            dst[n++] = (char) (0xf0 | (cp >> 18));
            dst[n++] = (char) (0x80 | ((cp >> 12) & 0x3f));
            dst[n++] = (char) (0x80 | ((cp >> 6) & 0x3f));
            dst[n++] = (char) (0x80 | (cp & 0x3f));
        }
    }
    dst[n] = '\0';
    return (int) n;
} /* ndr_utf16le_to_utf8 */

/* ----------------------------------------------------------------------------
 * Out-of-line declarations implemented in the embedded ndr_builtin.c: the
 * larger helpers (conformant-varying strings, RPC_SID, op dispatch).
 * --------------------------------------------------------------------------*/

/*
 * RPC_SID (MS-DTYP 2.4.2.3).  identifier_authority is a 48-bit value stored
 * BIG-endian on the wire -- the lone big-endian field in an otherwise
 * little-endian stream.  sub_authority entries are little-endian.
 */
#define NDR_SID_MAX_SUB_AUTH 15
struct ndr_sid {
    uint8_t  revision;
    uint8_t  sub_authority_count;
    uint8_t  identifier_authority[6];
    uint32_t sub_authority[NDR_SID_MAX_SUB_AUTH];
};

/*
 * ndr_push_sid/ndr_pull_sid and ndr_push_wstring/ndr_pull_wstring are defined
 * (as static) in the embedded ndr_builtin.c that precedes the generated
 * marshallers in each generated .c, so they need no declaration here and stay
 * internal to each translation unit -- letting several generated *_ndr.c link
 * into one library without colliding.  Conformant-varying UTF-16 string body is
 * [max_count][offset=0][actual_count] then the UTF-16 code units.
 */

/* ----------------------------------------------------------------------------
 * Per-operation dispatch table, consumed by the hand-written transceive glue.
 * --------------------------------------------------------------------------*/
struct ndr_op_desc {
    uint16_t    opnum;
    const char *name;
    size_t      in_size;
    size_t      out_size;
    int         (*pull_in)(
        struct ndr_cursor *c,
        void              *in,
        struct ndr_dbuf   *d);
    int         (*push_out)(
        struct ndr_writer *w,
        const void        *out);
};

/* Inline (header) so external transceive glue can call it without an
 * out-of-line symbol that would collide across generated translation units. */
static inline const struct ndr_op_desc *
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

#endif /* NDR_BUILTIN_H */
