# IRIX MCP Server — Portability Matrix

This document records the confirmed technical facts for each build target and defines the scope of the compatibility layer. All entries derive from primary SGI documentation reviewed during project setup.

---

## 1. Build Target Matrix

| Target | Compiler | ABI | ISA | C mode | Static link |
|---|---|---|---|---|---|
| IRIX 5.3 | ucode `cc` (IDO) | O32 | MIPS-II | **K&R by default — must use `-ansi`** | Yes |
| IRIX 6.2 | MIPSpro `cc` | N32 | MIPS-III | ANSI by default | Optional |
| IRIX 6.5 | MIPSpro 7.4 | N32 | **MIPS-IV** | ANSI by default | Optional |

**IRIX 6.5 ISA is MIPS-IV, not MIPS-III.** The primary build machine is an Octane2 (R10000). R10000 is a MIPS-IV processor. `/etc/compiler.defaults` on this system reads `-DEFAULT:abi=n32:isa=mips4:proc=r10k`. Using `-mips4` for the `irix65` target matches the native ISA and avoids the startup object mismatch described in §9.

Using `-mips3` for IRIX 6.2 targets (R4000/R5000 hardware) requires the explicit startup object workaround described in §9.

### 1.1 Recommended build commands

```sh
# IRIX 5.3 (IDO ucode compiler; or cross-build on 6.5 with -o32)
/opt/MIPSpro/bin/cc -ansi -o32 -mips2 -O2 -fullwarn -static -o mcpserverd src/...

# IRIX 6.5 native (R10000 Octane2) — N32 MIPS-IV, no startup-object issue
/opt/MIPSpro/bin/cc -n32 -mips4 -O2 -ansi -fullwarn -o mcpserverd src/...

# IRIX 6.2 / MIPS-III compat — requires explicit startup objects; see §9
# (Do not use a single cc invocation; see Makefile irix62 target)
```

### 1.2 Compiler path on the Octane2

MIPSpro 7.4 installed to `/opt/MIPSpro/bin/cc` on the development Octane2.
`/usr/bin/cc` may also resolve correctly depending on PATH. Use the full path
in Makefile rules to avoid ambiguity.

```sh
/opt/MIPSpro/bin/cc -version
# MIPSpro Compilers: Version 7.4
```

### 1.2 Critical compiler flag notes

- **`-ansi` is mandatory for IRIX 5.3.** The ucode compiler defaults to traditional K&R C. Without `-ansi`, implicit function declarations are accepted silently, and C89 features like prototypes may not be enforced.
- **`-fullwarn`** is the MIPSpro equivalent of `-Wall`. Use it on all targets.
- **No `//` comments** anywhere in the source — not valid in K&R or strict C89.
- **No C99 features** — no `//`, no VLAs, no designated initializers, no `_Bool`, no `<stdint.h>`.
- **No MIPSpro pragmas** in code that must compile on the IRIX 5.3 ucode compiler.

---

## 2. Data Type Sizes

| Type | O32 | N32 | N64 | Notes |
|---|---|---|---|---|
| `char` | 8-bit | 8-bit | 8-bit | |
| `short` | 16-bit | 16-bit | 16-bit | |
| `int` | 32-bit | 32-bit | 32-bit | |
| `long` | 32-bit | 32-bit | **64-bit** | — never assume `long` size |
| `long long` | 64-bit | 64-bit | 64-bit | |
| pointer | 32-bit | 32-bit | **64-bit** | |
| `long double` | 64-bit (problematic) | 128-bit | 128-bit | avoid `long double` |

We target O32 and N32 only. `long` and pointer are both 32-bit in both cases — this is safe. Never assume `sizeof(long) == sizeof(pointer)` since N64 breaks this.

---

## 3. ABI-Detecting Compiler Macros

Use these in `compat/compat.h` for conditional code:

| Macro | Meaning |
|---|---|
| `_MIPS_SIM == _MIPS_SIM_ABI32` | O32 or N32 (both are 32-bit pointer models) |
| `_MIPS_SIM == _MIPS_SIM_ABI64` | N64 |
| `_MIPS_SZPTR` | Pointer size in bits (32 or 64) |
| `_MIPS_SZLONG` | `long` size in bits (32 or 64) |
| `_MIPS_SZINT` | `int` size in bits (always 32) |
| `_MIPS_ISA` | Instruction set (MIPS1/2 for O32, MIPS3/4 for N32) |

---

## 4. Library Paths by ABI

| ABI | Runtime library path |
|---|---|
| O32 | `/usr/lib/` |
| N32 | `/usr/lib32/` |
| N64 | `/usr/lib64/` (not targeted) |

The compiler selects the correct library path automatically based on the ABI flag. When statically linking, this is moot.

---

## 5. Syscall / Function Availability

All confirmed from IRIX 5.0 Programmer's Reference Manual (007-0602-050, 1990) and IRIX Network Programming Guide (007-0810-110, 2003). IRIX 5.0 availability implies 5.3 availability.

| Function | IRIX 5.3 | IRIX 6.2 | IRIX 6.5 | Notes |
|---|---|---|---|---|
| `socket(AF_UNIX, SOCK_STREAM, 0)` | ✓ | ✓ | ✓ | Confirmed in PRM |
| `bind()`, `listen()`, `accept()`, `connect()` | ✓ | ✓ | ✓ | |
| `select()` on UNIX sockets | ✓ | ✓ | ✓ | Domain-agnostic |
| `fcntl(s, F_SETFL, FNDELAY)` | ✓ | ✓ | ✓ | Non-blocking; use FNDELAY not O_NONBLOCK |
| `syslog()`, `openlog()`, `closelog()` | ✓ | ✓ | ✓ | |
| `fork()`, `exec*()` | ✓ | ✓ | ✓ | |
| `sigaction()` | ✓ | ✓ | ✓ | Prefer over `signal()` |
| `waitpid()`, `wait3()` | ✓ | ✓ | ✓ | |
| `stat()`, `lstat()`, `open()`, `read()`, `write()` | ✓ | ✓ | ✓ | |
| `unlink()` | ✓ | ✓ | ✓ | Required before `bind()` on socket path |
| `realpath()` | **✗** | ? | ✓ | Not in 5.0 docs — implement in compat/ |
| `fnmatch()` | **✗** | ? | ✓ | Not in 5.0 docs — implement in compat/ |
| `<stdint.h>` | **✗** | ? | ✓ | Not on 5.3 — define types in compat.h |
| `EWOULDBLOCK` | = `EAGAIN` | = `EAGAIN` | = `EAGAIN` | Same value; check both |

### 5.1 IRIX-specific behavioral notes

- **`unlink()` before `bind()`**: The IRIX socket file is a real filesystem object. A crashed daemon leaves the socket file behind. Startup must always `unlink()` the socket path before `bind()`.
- **Non-blocking I/O**: Use `fcntl(s, F_SETFL, FNDELAY)`. IRIX documentation uses `FNDELAY`, not `O_NONBLOCK`.
- **`EWOULDBLOCK`**: Defined as the same value as `EAGAIN` on IRIX. Current IRIX returns `EAGAIN`; old versions returned `EWOULDBLOCK`. Handle both identically.
- **`sockaddr_un` bind length**: Use `strlen(addr.sun_path) + sizeof(addr.sun_family)` — null bytes are not counted.
- **Default C mode on 5.3**: K&R, not ANSI. Always compile with `-ansi`.
- **Virtual swap**: Do not speculatively allocate memory to probe availability. IRIX may use deferred memory allocation.

---

## 6. compat/ Layer Scope

Files in `src/compat/` provide portable implementations of functions missing from IRIX 5.3.

### `compat.h` — Type definitions and feature detection

```c
/* Integer types for IRIX 5.3 (no stdint.h) */
#ifndef MCPSERVER_COMPAT_H
#define MCPSERVER_COMPAT_H

#include <sgidefs.h>   /* _MIPS_SZPTR, _MIPS_SZINT etc. */

typedef signed char     int8_t;
typedef unsigned char   uint8_t;
typedef signed short    int16_t;
typedef unsigned short  uint16_t;
typedef signed int      int32_t;
typedef unsigned int    uint32_t;
typedef signed long long   int64_t;
typedef unsigned long long uint64_t;

/* Portable size_t safe max length for config paths */
#define MCPSERVER_PATH_MAX 1024

#endif
```

### `realpath.c` — Path canonicalization

Implements path canonicalization without using `realpath(3)`. Uses `stat()`, `readlink()`, and manual `..` resolution. Must match the behavior expected by the policy engine.

### `fnmatch.c` — Glob pattern matching

Implements a subset of fnmatch matching `*`, `?`, `**` (recursive glob), and `[...]` character classes. Used by the deny glob rules in the boundary engine.

---

## 7. Struct Layout Caution

O32 and N32 have different parameter slot sizes (32-bit vs 64-bit in N32). Do not send raw structs over the IPC socket. All wire formats must be serialized field-by-field as JSON text. This is the correct design regardless of ABI concerns.

---

## 8. Open Questions (Unresolved Portability Items)

| Item | Status | Resolution path |
|---|---|---|
| `sun_path` max length on IRIX 5.3 | Unknown | Check `/usr/include/sys/un.h` on Octane2; use path ≤ 104 bytes to be safe |
| `fnmatch(3)` availability on IRIX 6.2 | Unknown | Implement in compat/ regardless and use always |
| IRIX 6.2 compiler version | Unknown | Verify with `cc -version` on a 6.2 system when available |
| IRIS emulator: verified IRIX 5.3 boot | Pending | Test with Indy IRIX 5.3 image |
| Does `-nostartfiles` drop any crt other than crt1/crtn on IRIX 6.5? | Pending | Verify on Octane2 that only crt1.o and crtn.o are needed |

These items are tracked here. Discoveries go into the appropriate compat/ files or this document.

---

## 9. MIPS-IV Startup Object Issue (Confirmed on Octane2 / IRIX 6.5.30)

**Observed on:** SGI Octane2 / IP30, IRIX64 6.5.30, MIPSpro 7.4.

### What happens

When building with `-n32 -mips3`, the MIPSpro 7.4 driver correctly compiles
object code to the MIPS-III ISA. However, the final link step picks up the
system-default startup objects:

```
/usr/lib32/crt1.o  →  /usr/lib32/mips4/crt1.o  (symlink)
/usr/lib32/crtn.o  →  /usr/lib32/mips4/crtn.o  (symlink)
```

This is because `/etc/compiler.defaults` contains:
```
-DEFAULT:abi=n32:isa=mips4:proc=r10k
```
and the linker resolves startup objects via the generic symlinks regardless
of the `-mips3` flag. The result is an executable that `file(1)` reports as
`N32 MIPS-IV` even though user code was compiled to MIPS-III.

Setting `COMPILER_DEFAULTS_PATH` to a custom file does **not** fix this —
startup object selection bypasses that override.

### Confirmed workaround

Compile source files to object files with `-n32 -mips3`, then link with
`-nostartfiles` and explicit MIPS-III startup objects:

```sh
# compile (MIPS-III codegen)
/opt/MIPSpro/bin/cc -n32 -mips3 -O2 -ansi -c src/daemon/mcpserverd.c \
    -o build/mips3/mcpserverd.o ...

# link with explicit MIPS-III crt objects
/opt/MIPSpro/bin/cc -n32 -mips3 -nostartfiles \
    /usr/lib32/mips3/crt1.o \
    build/mips3/*.o \
    /usr/lib32/mips3/crtn.o \
    -lc -o mcpserverd

file mcpserverd
# ELF 32-bit MSB executable, MIPS, N32 MIPS-III version 1  ✓
```

### Impact on the build matrix

| Target | ISA goal | Startup-object issue? | Strategy |
|---|---|---|---|
| `irix65` | MIPS-IV | No — symlinks already point to mips4/ | Single `cc` invocation, `-n32 -mips4` |
| `irix62` | MIPS-III | **Yes** on R10000 build host | Compile to `.o`, link with explicit `mips3/crt1.o` |
| `irix53` | MIPS-II | No — O32, static, different lib path | Single `cc` invocation, `-o32 -mips2 -static` |

### Do not over-fit to this machine

This behavior is specific to MIPSpro 7.4 systems where the symlinks in
`/usr/lib32/` point to the native processor's ISA. On an R4000 or R5000 IRIX
6.5 system the symlinks likely point to `mips3/`, and `-mips3` would work
without the workaround. The Makefile `irix62` target uses the explicit
startup-object approach unconditionally so it is correct on any build host.
