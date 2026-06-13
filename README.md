<!--
SPDX-FileCopyrightText: 2026 Chimera-NAS Project Contributors
SPDX-License-Identifier: LGPL-2.1-only
-->

# ndrzcc

`ndrzcc` compiles a pragmatic subset of the Microsoft Interface Definition
Language (MS-IDL) into C code implementing **DCE/RPC NDR** (Network Data
Representation) marshalling and unmarshalling, plus a per-operation dispatch
table for a DCE/RPC server.

It is a sibling of [`xdrzcc`](https://github.com/chimera-nas/xdrzcc) and mirrors
its architecture: a Flex/Bison front end builds an AST, a code generator lowers
it to C, and the runtime support is embedded into the generated files so the
output has **no dependency beyond libc**. It exists to give the Chimera SMB
server generated, spec-correct marshalling for the named-pipe RPC services
(LSARPC, SRVSVC, SAMR, WKSSVC) in place of hand-rolled byte pushing.

The implementation is from the public specifications (DCE 1.1 / C706 plus
MS-RPCE, MS-LSAD, MS-SRVS, MS-SAMR, MS-DTYP); it is `LGPL-2.1-only` and contains
no code from other RPC stacks.

## Build

Requires `flex`, `bison`, `cmake`, and `ninja`.

```bash
make            # release build + tests
make debug      # debug build (AddressSanitizer) + tests
make test_release
make syntax     # reformat sources with uncrustify
```

Or directly:

```bash
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -S . -B build/release
ninja -C build/release
```

When pulled into a parent CMake project (e.g. Chimera via `add_subdirectory`),
the compiler path is exported to the parent scope as `${NDRZCC}`.

## Usage

```bash
ndrzcc <input.idl> <output.c> <output.h>
```

Wire it into a build with a custom command, then add the generated `.c` to a
target:

```cmake
add_custom_command(
    OUTPUT  lsa_ndr.c lsa_ndr.h
    COMMAND ${NDRZCC} ${CMAKE_CURRENT_SOURCE_DIR}/lsa.idl lsa_ndr.c lsa_ndr.h
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lsa.idl ${NDRZCC})
```

## IDL subset

```idl
[ uuid(12345778-1234-abcd-ef00-0123456789ab), version(0.0) ]
interface lsarpc {
    typedef struct {
        uint32 attributes;
        uint8  uuid[16];
    } LSA_HANDLE;

    [opnum(44)] uint32 LsarOpenPolicy2(
        [in, unique, string] wchar_t    *system_name,
        [in]                 uint32      desired_access,
        [out]                LSA_HANDLE *policy_handle);
}
```

Supported: `interface` with `uuid`/`version`; scalars
(`uint8/16/32/64`, `hyper`, `int*`, `boolean`, `wchar_t`); `typedef`, `struct`,
`enum`, named `union ... switch (...)`; pointer classes `[ref]`, `[unique]`,
`[ptr]`; `[string]` (conformant-varying UTF-16); fixed arrays `[N]`; conformant
arrays `[size_is]` / varying `[length_is]`; `[switch_is]`; `[context_handle]`;
per-parameter `[in]` / `[out]` with opnum assigned by declaration order (or an
explicit `[opnum(n)]`). The well-known `RPC_SID` is handled by the runtime.

## Generated API

For each interface, `ndrzcc` emits:

- C `struct`s for every IDL type, and an `_in` / `_out` struct per operation.
- `ndr_push_<Type>()` / `ndr_pull_<Type>()` for every type, driven by the
  two-pass `NDR_SCALARS` / `NDR_BUFFERS` model.
- `ndr_pull_<Op>_in()` / `ndr_push_<Op>_out()` per operation.
- A dispatch table `<iface>_op_table[]` (+ `<iface>_op_count`) of
  `struct ndr_op_desc { opnum, name, in_size, out_size, pull_in, push_out }`,
  found at runtime with `ndr_find_op()`.
- The interface identity: `<iface>_uuid[16]`, `<iface>_ver_major/minor`.

A server's transceive glue unmarshals the request stub into the `_in` struct,
runs hand-written business logic to fill the `_out` struct, and marshals it.

## Design notes

- **Two-pass marshalling.** Constructed types are visited once with
  `NDR_SCALARS` (fixed fields + referent ids inline) and once with `NDR_BUFFERS`
  (deferred pointer/array bodies, which recurse). Walking a parameter list this
  way reproduces NDR's "all fixed parts first, then all deferred bodies" order.
- **Flat-buffer reader/writer.** `ndr_writer` and `ndr_cursor` track an offset
  from octet 0 of the stub — the NDR alignment origin — so primitive alignment
  is simply `off & (n - 1)`, symmetric on both sides.
- **Self-contained.** Little-endian primitives, natural alignment, referent
  pointers, conformant/varying arrays, conformant-varying UTF-16 strings (with
  an inline UTF-8↔UTF-16LE converter), and `RPC_SID` (big-endian identifier
  authority, little-endian subauthorities) all live in the embedded runtime.
- NDR32 transfer syntax (`8a885d04-1ceb-11c9-9fe8-08002b104860`) only.

## Tests

`tests/` contains golden-byte and round-trip checks wired into CTest, including
an `RPC_SID` golden vector (authority endianness + hoisted conformant count) and
a non-ASCII string round-trip. Run with `make test_debug` or `ctest` in a build
directory.

## License

`LGPL-2.1-only`. See `LICENSE`.
