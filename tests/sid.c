// SPDX-FileCopyrightText: 2026 Chimera-NAS Project Contributors
//
// SPDX-License-Identifier: LGPL-2.1-only

/*
 * Golden-byte test for RPC_SID marshalling: validates the hoisted conformant
 * max_count, the big-endian identifier authority, and little-endian
 * subauthorities -- the highest-risk NDR details.  SID = S-1-5-21-1111-2222-3333.
 */

#include "sid_ndr.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

int
main(void)
{
    uint8_t            buf[64];
    struct ndr_writer  w;
    struct EchoSid_out out;
    int                n;

    /* Expected stub:
     *   04 00 00 00   max_count (hoisted) = sub_authority_count
     *   01            revision
     *   04            sub_authority_count
     *   00 00 00 00 00 05   identifier_authority (BIG-endian, NT=5)
     *   15 00 00 00   21
     *   57 04 00 00   1111
     *   ae 08 00 00   2222
     *   05 0d 00 00   3333
     *   00 00 00 00   status (return value)
     */
    static const uint8_t expect[] = {
        0x04, 0x00, 0x00, 0x00,
        0x01, 0x04,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x05,
        0x15, 0x00, 0x00, 0x00,
        0x57, 0x04, 0x00, 0x00,
        0xae, 0x08, 0x00, 0x00,
        0x05, 0x0d, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };

    memset(&out, 0, sizeof(out));
    out.status                      = 0;
    out.sid.revision                = 1;
    out.sid.sub_authority_count     = 4;
    out.sid.identifier_authority[5] = 5;            /* big-endian: 0x000000000005 */
    out.sid.sub_authority[0]        = 21;
    out.sid.sub_authority[1]        = 1111;
    out.sid.sub_authority[2]        = 2222;
    out.sid.sub_authority[3]        = 3333;

    ndr_writer_init(&w, buf, sizeof(buf));
    n = sidtest_op_table[0].push_out(&w, &out);

    if (n != (int) sizeof(expect)) {
        printf("FAIL: length %d != %zu\n", n, sizeof(expect));
        return 1;
    }
    if (memcmp(buf, expect, sizeof(expect)) != 0) {
        printf("FAIL: byte mismatch\n");
        for (int i = 0; i < n; i++) {
            printf("  [%2d] got %02x want %02x %s\n",
                   i, buf[i], expect[i], buf[i] == expect[i] ? "" : "<<<");
        }
        return 1;
    }

    printf("PASS: SID marshalled to %d bytes, golden match\n", n);
    return 0;
}
