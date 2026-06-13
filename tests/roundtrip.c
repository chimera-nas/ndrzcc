// SPDX-FileCopyrightText: 2026 Chimera-NAS Project Contributors
//
// SPDX-License-Identifier: LGPL-2.1-only

/*
 * Round-trip test: push a struct that mixes a scalar, a fixed byte array, and a
 * [unique][string] (deferred to the BUFFERS pass), then pull it back and check
 * every field survived -- including a non-ASCII name through UTF-8<->UTF-16.
 */

#include "roundtrip_ndr.h"
#include <stdio.h>
#include <string.h>

int
main(void)
{
    uint8_t           buf[256];
    struct ndr_writer w;
    struct ndr_cursor c;
    struct ndr_dbuf   d;
    struct Item       in, out;
    int               n;

    memset(&in, 0, sizeof(in));
    in.id     = 0xdeadbeef;
    in.tag[0] = 1;
    in.tag[1] = 2;
    in.tag[2] = 3;
    in.tag[3] = 4;
    in.name   = "h\xc3\xa9llo";   /* "héllo" in UTF-8 */

    ndr_writer_init(&w, buf, sizeof(buf));
    ndr_push_Item(&w, NDR_SCALARS | NDR_BUFFERS, &in);
    n = ndr_writer_length(&w);
    if (n < 0) {
        printf("FAIL: push error\n");
        return 1;
    }

    ndr_cursor_init(&c, buf, n);
    ndr_dbuf_init(&d, 256);
    memset(&out, 0, sizeof(out));
    ndr_pull_Item(&c, NDR_SCALARS | NDR_BUFFERS, &out, &d);
    if (c.error) {
        printf("FAIL: pull error\n");
        return 1;
    }

    if (out.id != in.id) {
        printf("FAIL: id %08x != %08x\n", out.id, in.id);
        return 1;
    }
    if (memcmp(out.tag, in.tag, 4) != 0) {
        printf("FAIL: tag mismatch\n");
        return 1;
    }
    if (!out.name || strcmp(out.name, in.name) != 0) {
        printf("FAIL: name '%s' != '%s'\n", out.name ? out.name : "(null)", in.name);
        return 1;
    }

    printf("PASS: Item round-tripped in %d bytes (name='%s')\n", n, out.name);
    ndr_dbuf_destroy(&d);
    return 0;
}
