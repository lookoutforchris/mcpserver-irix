---
name: build
description: Build mcpserver-irix for one or all IRIX targets without packaging or releasing. Use during development iteration when the user says "build", "rebuild", "compile", or names a specific target like "irix65". Takes an optional target argument (irix65, irix62, irix53, or all). Defaults to "all" if omitted.
---

# Build Workflow

Compile the binaries for one or all IRIX targets. No packaging, no release, no install. For a full release see the `release` skill.

## Target matrix

| Target | Where | Command | ISA |
|---|---|---|---|
| `irix65` | Octane2 | `make irix65` | N32 MIPS-IV |
| `irix62` | Octane2 | `make irix62` | N32 MIPS-III |
| `irix53` | IRIS emulator | `make irix53-native SHELL=/bin/sh` | O32 MIPS-II |

## Procedure

### irix65 / irix62 — on Octane2

Wrap all SSH in `sh -c '...'`. Default shell on Octane2 is csh; `2>&1` does not work there.

**Always delete root binaries first** — make checks timestamps and skips work if `mcpserverd`/`mcpserver` are newer than sources.

```
ssh root@speed.siliconsurf.net "sh -c 'cd /home/work/projects/mcpserver-irix && git pull && rm -f mcpserverd mcpserver && make irix65 2>&1 | tail -8 && file mcpserverd mcpserver'"
```

For irix62, build after irix65 has been staged (irix62 will overwrite the root binaries):
```
ssh root@speed.siliconsurf.net "sh -c 'cd /home/work/projects/mcpserver-irix && mkdir -p stage/irix65 && cp mcpserverd mcpserver stage/irix65/ && make irix62 2>&1 | tail -8 && file mcpserverd mcpserver'"
```

Expected `file` output:
- irix65: `ELF 32-bit MSB executable, MIPS, N32 MIPS-IV`
- irix62: `ELF 32-bit MSB executable, MIPS, N32 MIPS-III`

### irix53 — on IRIS

The MIPSpro compiler is not on IRIX 5.3; only IDO `cc`. Cross-compile from 6.5 does not produce installable packages for 5.3 (gendist format incompatibility).

If sources are already current on IRIS, tell the user to run on the PuTTY serial console (port 8881):
```
cd /usr/people/shared/projects/mcpserver-irix
make irix53-native SHELL=/bin/sh
file mcpserverd mcpserver
```

Expected: `ELF 32-bit MSB mips-2 dynamic executable`.

If sources need updating first, use the `iris-transfer` skill to push the changed files via scratch disk. Small files (under ~15 KB) can go through the `replace_text_file` MCP tool directly.

## `build all`

Run irix65 + irix62 on Octane2 (steps above), then prompt the user to run the irix53 build on IRIS.

## Common pitfalls

| Problem | Fix |
|---|---|
| `Nothing to be done for 'irix65'` | `rm -f mcpserverd mcpserver` first |
| csh command-line length error on IRIX 5.3 | `SHELL=/bin/sh` flag |
| `cfe: Warning 835: No prototype for snprintf` etc. on 5.3 | Benign — IDO is strict; these have prototypes via compat.h |
| `Ambiguous output redirect` over SSH | Wrap in `sh -c '...'` |
| irix65 binary has MIPS-III instead of MIPS-IV | `/etc/compiler.defaults` was modified; check Makefile uses `-mips4` |
