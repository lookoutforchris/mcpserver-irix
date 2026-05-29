# mcpserver-irix Release Workflow

This document describes the end-to-end process for building and releasing a new version
of mcpserver-irix across all three IRIX targets. Written after the v0.3.0 release cycle
(2026-05-28) to avoid repeating avoidable problems.

---

## Overview

The release involves three separate machines and a file transfer step:

```
Windows workstation        → writes code, runs git
Octane2 (IRIX 6.5)         → builds irix65 + irix62, packages all three tardists
IRIS emulator (IRIX 5.3)   → builds irix53-native only
SS/Outbox (Dropbox/Galaxy) → transfer path for irix53 binaries to Octane2
```

**Source of truth: Windows workstation → GitHub.**
The Octane2 git repo may have diverged (see §6). Resolve before tagging.

---

## Step 1 — Code changes on Windows

All development happens in `C:\dev\projects\mcpserver-irix`.

Key files to update for a new release:
- `src/compat/compat.h` — bump `MCPSERVER_VERSION`
- `packaging/irix65/mcpserver.spec` — update `id` string with new version
- `packaging/irix62/mcpserver.spec` — same
- `packaging/irix53/mcpserver.spec` — same
- `Makefile` — update `PKG_VERSION`
- `README.md` — update status line
- `AGENTS.md` — update tool count if changed

Check that any new `.c` files are added to:
- `CORE_SRCS` in Makefile (used by irix65 and irix62 via `$(DAEMON_ALL)`)
- `irix53-native` compile list (individual `cc` lines) AND link commands

---

## Step 2 — Commit and push to GitHub

```sh
git add <changed files>
git commit -m "vX.Y.Z: <summary>"
git push origin main
```

---

## Step 3 — Update IRIS emulator (irix53-native build)

The IRIS emulator runs IRIX 5.3 with IDO installed. Source lives at:
```
/usr/people/shared/projects/mcpserver-irix/mcpserver/src
```

**Update the source files on IRIS** using the irix-indy53 MCP server's
`replace_text_file` / `create_text_file` tools. Write each changed file directly.
No tar transfer needed — the MCP server can write files in place.

Files that typically change each release:
- `src/core/protocol.c`
- `src/compat/compat.h`
- `Makefile`
- Any new `.c`/`.h` files

**Build on IRIS:**
```sh
# From IRIX 5.3 terminal (serial console port 8881, raw TCP)
cd /usr/people/shared/projects/mcpserver-irix/mcpserver/src
make irix53-native SHELL=/bin/sh
```

The build takes ~2 minutes. Output: `mcpserverd` and `mcpserver` in the src directory.

**Transfer irix53 binaries to Octane2 via SS/Outbox:**
```sh
# From IRIX 5.3:
cp mcpserverd mcpserver /path/to/SS/Outbox/irix53/
# The Outbox syncs via Dropbox to Windows, then via Galaxy NFS to Octane2
# On Octane2: copy from /nfs/galaxy/SS/Outbox/irix53/ to project staging dir
```

---

## Step 4 — Update Octane2 source

The Octane2 git repo at `/home/chris/src/mcpserver-irix` may have diverged from GitHub.

**Update via scp from Windows:**
```sh
# From Windows (MSYS2 bash):
scp src/core/tools_build.c src/core/tools_build.h \
    src/core/protocol.c src/compat/compat.h \
    root@speed.siliconsurf.net:/home/chris/src/mcpserver-irix/src/core/
scp src/compat/compat.h root@speed.siliconsurf.net:/home/chris/src/mcpserver-irix/src/compat/
```

**Update Makefile on Octane2:** The Octane2 Makefile has extra targets (irix62-tardist,
irix53-tardist) not on Windows. Update ONLY the CORE_SRCS and PKG_VERSION lines.
Use `/usr/sgug/bin/perl` for scripted replacements (IRIX sh is too limited for complex sed).

---

## Step 5 — Build irix65 and irix62 on Octane2

```sh
ssh root@speed.siliconsurf.net
cd /home/chris/src/mcpserver-irix

# Build irix65 first and stage binaries
make irix65 > /home/chris/build65.log 2>&1
cp mcpserverd mcpserver stage/irix65/
file stage/irix65/mcpserverd   # must show N32 MIPS-IV

# Build irix62 and stage binaries  
make irix62 > /home/chris/build62.log 2>&1
cp mcpserverd mcpserver stage/irix62/
file stage/irix62/mcpserverd   # must show N32 MIPS-III
```

**Important:** irix62 build overwrites the root-level binaries. Always stage irix65
first, then irix62. The `stage/` directory preserves both.

**Use `sh -c '...'` for all SSH commands** — the Octane2 default shell is csh and
`2>&1` redirection does not work in csh.

---

## Step 6 — Stage all three binaries for packaging

```sh
# On Octane2 — copy irix53 binaries from SS/Outbox
cp /nfs/galaxy/SS/Outbox/irix53/mcpserverd stage/irix53/
cp /nfs/galaxy/SS/Outbox/irix53/mcpserver  stage/irix53/
file stage/irix53/mcpserverd  # must show O32 MIPS-II
```

---

## Step 7 — Update packaging files and run tardist

Update version in spec files if not done already:
- `packaging/irix65/mcpserver.spec`
- `packaging/irix62/mcpserver.spec`
- `packaging/irix53/mcpserver.spec`

The IDB files reference source paths relative to the project root. Binaries are
referenced as `mcpserverd` and `mcpserver` (the root-level binaries). Before each
tardist, copy the correct staged binaries to the project root:

```sh
# irix65 tardist
cp stage/irix65/mcpserverd stage/irix65/mcpserver .
make tardist

# irix62 tardist
cp stage/irix62/mcpserverd stage/irix62/mcpserver .
make irix62-tardist

# irix53 tardist  
cp stage/irix53/mcpserverd stage/irix53/mcpserver .
make irix53-tardist
```

Output: `mcpserver-X.Y.Z-irix65.tardist`, `irix62.tardist`, `irix53.tardist`

---

## Step 8 — Resolve git history and commit

The Octane2's repo was independently `git init`-ed (not cloned from GitHub). Its
history is unrelated to GitHub's. To unify:

```sh
# On Octane2:
git add -A
git commit -m "vX.Y.Z: <message>"

# Force-push to GitHub (since we own both sides and the Octane2 has the
# complete combined state)
git push --force origin main
```

Then on Windows:
```sh
git fetch origin
git reset --hard origin/main
```

---

## Step 9 — Tag and verify

```sh
# On Octane2 (or Windows after sync):
git tag vX.Y.Z
git push origin vX.Y.Z
```

Verify on GitHub: check that the release commit contains all three tardist targets
in the Makefile and the correct PKG_VERSION.

---

## Common pitfalls

| Problem | Solution |
|---|---|
| irix62 build overwrites irix65 binaries | Always stage irix65 first, then irix62 |
| `2>&1` fails in csh on Octane2 | Use `sh -c '...'` for all SSH commands |
| IRIX sh can't handle complex sed patterns | Use `/usr/sgug/bin/perl` for file edits |
| Unicode characters in Python scripts crash on cp1252 | Replace `→` with `->` etc. |
| `make irix53-native` fails on csh command line limits | Always pass `SHELL=/bin/sh` |
| Octane2 MCP tool calls fail silently | Test with ping first; check `mcpserver status` |
| Port 69 (TFTP) needs admin | Use `--port 6969`; IRIX tftp supports custom ports |
| NFS `ls` only works once per mount on IRIX 5.3 | Known IRIX 5.3 kernel limitation; access files by name after first ls |

---

## File locations quick reference

| Item | Path |
|---|---|
| Windows project | `C:\dev\projects\mcpserver-irix` |
| Octane2 project | `/home/chris/src/mcpserver-irix` |
| IRIS emulator source | `/usr/people/shared/projects/mcpserver-irix/mcpserver/src` |
| TFTP server | `C:\dev\tools\tftpd\tftpd.py` |
| NFS proxy | `C:\dev\projects\mcpserver-irix\scripts\nfs-proxy.py` |
| unfsd binary | `C:\dev\tools\unfs3\unfsd.exe` |
| IRIS shared folder | `C:\dev\tools\iris\shared` |
| SS/Outbox (Dropbox) | `S:\Outbox` (Windows) / `/nfs/galaxy/SS/Outbox` (Octane2) |
| Octane2 SSH | `root@speed.siliconsurf.net` |
| IRIS serial console | `telnet localhost 8881` (raw TCP, not telnet protocol) |
| IRIS monitor | `telnet localhost 8888` |
