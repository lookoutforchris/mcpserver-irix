# mcpserver-irix Release Workflow

End-to-end procedure for building and releasing a new version across all three
IRIX targets. Based on the v0.3.0 release cycle (2026-05-29).

---

## Overview

Three machines are involved. Their roles are fixed:

```
Windows workstation   → source of truth, all git commits, GitHub releases
Octane2 (IRIX 6.5)   → builds irix65 + irix62, packages irix65 + irix62 tardists
IRIS emulator (5.3)  → builds irix53-native, packages irix53 tardist
```

**Windows is the only authoritative git repository.**
Octane2 is always a clean clone of GitHub — never commit from Octane2.
IRIS is not git-connected and is never the source of truth for anything.

**Why irix53 must be packaged on IRIX 5.3:**
`gendist` on IRIX 6.5 produces a package format that `inst` on IRIX 5.3
rejects ("bad product"). Always build and package irix53 on the IRIS emulator.

---

## Step 1 — Code changes on Windows

All development in `C:\dev\projects\mcpserver-irix`.

Files to update for every release:
- `src/compat/compat.h` — bump `MCPSERVER_VERSION`
- `packaging/irix65/mcpserver.spec` — update `id` string
- `packaging/irix62/mcpserver.spec` — same
- `packaging/irix53/mcpserver.spec` — same
- `Makefile` — update `PKG_VERSION`
- `README.md` — update version/status line
- `AGENTS.md` — update tool count if changed

If new `.c` files were added, update:
- `CORE_SRCS` in Makefile (irix65 and irix62 compile from this list)
- `irix53-native` compile list in Makefile (individual `cc` lines)
- `irix53-native` link commands in Makefile (add the new `.o`)

---

## Step 2 — Initial commit and push to GitHub

```sh
git add <changed files>
git commit -m "vX.Y.Z: <summary of new features>"
git push origin main
```

This is the first of two commits. The second (housekeeping) comes after
packaging. Do not tag yet.

---

## Step 3 — Build and package irix53 on IRIS emulator

IRIS runs IRIX 5.3 with IDO installed. It is not git-connected.
Source is maintained manually at:

```
/usr/people/shared/projects/mcpserver-irix/src
```

Note: there is no `mcpserver/` subdirectory — the project root is the
src parent directly.

### 3a — Transfer updated source files to IRIS

The irix-indy53 MCP server (`replace_text_file` / `create_text_file`)
can write files directly, but has a content limit (~15 KB per write).
Files larger than ~15 KB or containing many escaped characters must be
transferred via scratch disk.

**MCP write (small files — compat.h, headers, small .c files):**
Use `replace_text_file` or `create_text_file` via the irix-indy53 MCP tools.

**Scratch disk transfer (large files — protocol.c, tools_exec.c, Makefile):**

```sh
# Windows — create tar with paths relative to project root:
tar cf /tmp/transfer.tar src/core/protocol.c Makefile   # add any changed files
dd if=/tmp/transfer.tar of=C:/dev/tools/iris/images/5.3/scratch.raw \
   bs=512 seek=8 conv=notrunc

# IRIS 5.3 (PuTTY session, port 8881 raw TCP):
cd /usr/people/shared/projects/mcpserver-irix
dd if=/dev/rdsk/dks0d2vol bs=512 skip=8 count=<N> | tar xf -
```

Where `<N>` = ceil(tar_size / 512) + 10. The scratch disk can be written
from Windows while IRIS is running — IRIS does not hold an exclusive lock.

**TFTP is NOT reliable for host→guest transfers** on the current IRIS build.
IRIS does not route arbitrary UDP to the host for unconfigured ports.
Use scratch disk instead.

### 3b — Build on IRIS 5.3

```sh
# IRIS 5.3 PuTTY session:
cd /usr/people/shared/projects/mcpserver-irix
make irix53-native SHELL=/bin/sh
file mcpserverd mcpserver
# expected: ELF 32-bit MSB mips-2 dynamic executable ... MIPS
```

The build takes ~2 minutes. Object files land at the project root — this
is a known issue with the `irix53-native` target (cleanup tracked as
future work: move .o files to `build/irix53/`).

### 3c — Package on IRIS 5.3

Transfer the packaging files from Windows to IRIS (spec, idb, config
defaults, init script) via scratch disk if not already present:

```sh
# Windows:
tar cf /tmp/pkg53.tar packaging/irix53/ scripts/mcpserverd.init
dd if=/tmp/pkg53.tar of=C:/dev/tools/iris/images/5.3/scratch.raw \
   bs=512 seek=8 conv=notrunc

# IRIS 5.3:
cd /usr/people/shared/projects/mcpserver-irix
dd if=/dev/rdsk/dks0d2vol bs=512 skip=8 count=<N> | tar xf -
make irix53-native-tardist SHELL=/bin/sh
```

The `irix53-native-tardist` target runs `gendist` using the IRIX 5.3 tools
and produces `mcpserver-X.Y.Z-irix53.tardist` in the project root.
It has no compile dependency — it packages whatever binaries are at the
project root (built by `irix53-native`).

### 3d — Transfer irix53 tardist to Windows

```sh
# IRIS 5.3:
cd /usr/people/shared/projects/mcpserver-irix
tar cf - mcpserver-X.Y.Z-irix53.tardist | dd of=/dev/rdsk/dks0d2s0 bs=512

# Windows (read from scratch.raw):
cd /tmp
dd if=C:/dev/tools/iris/images/5.3/scratch.raw bs=512 skip=8 count=<N> \
   | tar xf -
cp mcpserver-X.Y.Z-irix53.tardist C:/dev/projects/mcpserver-irix/
```

### 3e — Transfer irix53 binaries to Octane2 via SS/Outbox

```sh
# Windows: copy binaries to SS/Outbox (Dropbox-backed)
# First extract them from scratch.raw (same tar as §3d or a separate one)
cp mcpserverd mcpserver S:/Outbox/irix53/
# Wait for Dropbox sync to Galaxy
# On Octane2: /nfs/galaxy/SS/Outbox/irix53/ will have the binaries
```

---

## Step 4 — Build and stage irix65 + irix62 on Octane2

Octane2 must be a clean clone of GitHub at this point:

```sh
# If Octane2 is out of date, wipe and reclone:
ssh root@speed.siliconsurf.net
rm -rf /home/work/projects/mcpserver-irix
cd /home/chris/src
git clone https://github.com/lookoutforchris/mcpserver-irix.git
```

Build and stage — always irix65 first:

```sh
cd /home/work/projects/mcpserver-irix
mkdir -p stage/irix65 stage/irix62 stage/irix53

# irix65 — delete binaries first to force rebuild (make checks timestamps)
rm -f mcpserverd mcpserver
make irix65 > /home/chris/build65.log 2>&1
cp mcpserverd mcpserver stage/irix65/
file stage/irix65/mcpserverd   # must show N32 MIPS-IV

# irix62 (overwrites root-level binaries — irix65 already staged)
make irix62 > /home/chris/build62.log 2>&1
cp mcpserverd mcpserver stage/irix62/
file stage/irix62/mcpserverd   # must show N32 MIPS-III
```

**Use `sh -c '...'` for all SSH commands** — Octane2's default shell is
csh and `2>&1` does not work in csh.

**Always delete root-level binaries before `make irix65`** — if binaries
exist from a previous build, make sees them as newer than the sources and
skips the rebuild entirely, leaving the wrong ISA in the stage directory.

---

## Step 5 — Stage all three binaries

```sh
# On Octane2 — wait for Dropbox sync, then:
cp /nfs/galaxy/SS/Outbox/irix53/mcpserverd stage/irix53/
cp /nfs/galaxy/SS/Outbox/irix53/mcpserver  stage/irix53/
file stage/irix53/mcpserverd   # must show MIPS-II (O32)

# Verify all three:
file stage/irix65/mcpserverd   # N32 MIPS-IV
file stage/irix62/mcpserverd   # N32 MIPS-III
file stage/irix53/mcpserverd   # MIPS-II O32
```

---

## Step 6 — Package irix65 and irix62 on Octane2

The IDB files reference binaries as `mcpserverd` / `mcpserver` at the
project root. Copy staged binaries to root before each tardist.

```sh
cd /home/work/projects/mcpserver-irix

# irix65
cp stage/irix65/mcpserverd stage/irix65/mcpserver .
make tardist

# irix62
cp stage/irix62/mcpserverd stage/irix62/mcpserver .
make irix62-tardist
```

The irix53 tardist is already done (built on IRIS in §3).

---

## Step 7 — Bring tardists to Windows

```sh
# From Windows (MSYS2 bash):
cd /c/dev/projects/mcpserver-irix
scp root@speed.siliconsurf.net:/home/work/projects/mcpserver-irix/mcpserver-X.Y.Z-*.tardist .
# irix53.tardist is already here from §3d
```

Verify all three are present:
```sh
ls -lh mcpserver-X.Y.Z-*.tardist
```

---

## Step 8 — Housekeeping commit and tag

Update anything that needs a second pass (packaging spec version strings
are commonly missed in the first commit):

```sh
git add packaging/irix65/mcpserver.spec packaging/irix62/mcpserver.spec \
        packaging/irix53/mcpserver.spec Makefile   # plus any other changes
git commit -m "vX.Y.Z: packaging and release housekeeping"
git tag vX.Y.Z
git push origin main
git push origin vX.Y.Z
```

---

## Step 9 — Create the GitHub Release

Copy tardists to `release-assets/` (gitignored — local only):

```sh
cp mcpserver-X.Y.Z-*.tardist release-assets/
```

Create the formal release with all three tardists attached:

```sh
gh release create vX.Y.Z \
  release-assets/mcpserver-X.Y.Z-irix65.tardist \
  release-assets/mcpserver-X.Y.Z-irix62.tardist \
  release-assets/mcpserver-X.Y.Z-irix53.tardist \
  --title "vX.Y.Z — <one-line summary>" \
  --notes "<release notes>"
```

Verify the release appears at:
`https://github.com/lookoutforchris/mcpserver-irix/releases`

---

## Step 10 — Refresh Octane2 from GitHub

Now that GitHub is authoritative, bring Octane2 into sync:

```sh
ssh root@speed.siliconsurf.net
rm -rf /home/work/projects/mcpserver-irix
cd /home/chris/src
git clone https://github.com/lookoutforchris/mcpserver-irix.git
```

---

## Step 11 — Install on Octane2 and IRIS

**Octane2:**
```sh
cd /home/work/projects/mcpserver-irix
rm -f mcpserverd mcpserver
make irix65
make install
mcpserver version   # confirm new version
```

**IRIS 5.3 (PuTTY session):**

IRIX 5.3 `inst` cannot install directly from a `.tardist` file — it reports
"bad product". Unpack to a directory first, then install:

```sh
mkdir /tmp/mcpinst
cd /tmp/mcpinst
tar xf /usr/people/shared/projects/mcpserver-irix/mcpserver-X.Y.Z-irix53.tardist
inst -f /tmp/mcpinst
# At Inst> prompt: install all → go → quit
/sbin/chkconfig -f mcpserver on
/etc/init.d/mcpserverd start
mcpserver version   # confirm new version
```

If `mcpserver version` still shows the old version after `inst`, existing
binaries were not overwritten (IRIX 5.3 `inst` preserves files already on
disk outside package management). Copy manually:
```sh
cp /usr/people/shared/projects/mcpserver-irix/mcpserverd /usr/sbin/mcpserverd
cp /usr/people/shared/projects/mcpserver-irix/mcpserver  /usr/bin/mcpserver
/etc/init.d/mcpserverd start
```

Note: tcsh is the default shell on IRIX. Use backticks for command substitution,
not `$(...)`. Use `mcpserver stop` to stop the daemon.

---

## Step 12 — Basic testing

**On Octane2** (via irix-octane2 MCP tools or SSH):
```sh
mcpserver ping
mcpserver status
mcpserver version
```

**On IRIS 5.3** (via irix-indy53 MCP tools):
- `ping` — confirms server identity and version string
- `list_directory` on the project root
- `run_inspect_command` with `uname -a`

---

## Common pitfalls

| Problem | Solution |
|---|---|
| irix62 build overwrites irix65 binaries | Always stage irix65 first; delete root binaries before `make irix65` |
| `make irix65` says "Nothing to be done" | Delete `mcpserverd` and `mcpserver` first — make checks timestamps |
| `2>&1` fails in csh on Octane2 | Use `sh -c '...'` for all SSH commands |
| `make irix53-native` fails on csh command line limits | Always pass `SHELL=/bin/sh` |
| Large files fail via MCP `replace_text_file` | Use scratch disk for files > ~15 KB |
| TFTP transfers time out | IRIS does not route unconfigured UDP ports; use scratch disk instead |
| `inst` on IRIX 5.3 says "bad product" for irix53 tardist | Package was built with 6.5 gendist — must use `irix53-native-tardist` on IRIS 5.3 |
| gendist spec says wrong version | Update `id` string in all three `.spec` files (irix65, irix62, irix53) |
| GitHub shows old release as latest | Use `gh release create` — git tag alone does not create a GitHub Release |
| `inst -f file.tardist` says "bad product" on IRIX 5.3 | Unpack tardist to a directory first: `mkdir /tmp/i && cd /tmp/i && tar xf file.tardist && inst -f /tmp/i` |
| `inst` doesn't overwrite existing binaries on IRIX 5.3 | Copy manually from build dir after install |
| `$(...)` fails in tcsh | Use backticks or `mcpserver stop` instead of `kill $(cat ...)` |
| Stray files at filesystem root (`/mcpserver`, `/packaging/`, `/scripts/`) | Build ran from wrong directory — always `cd` to project root first; check `ls /` after any build session |
| NFS `ls` only works once per mount on IRIX 5.3 | Known kernel limitation; access files by name after first ls |
| IRIS won't boot after rebuilding from source | Re-run NVRAM setup: `setenv -f eaddr 08:00:69:de:ad:53` then `rtc save` |

---

## File locations quick reference

| Item | Path |
|---|---|
| Windows project | `C:\dev\projects\mcpserver-irix` |
| Octane2 project | `/home/work/projects/mcpserver-irix` (always a clean clone) |
| IRIS emulator project | `/usr/people/shared/projects/mcpserver-irix` |
| IRIS emulator source | `/usr/people/shared/projects/mcpserver-irix/src` |
| Release assets (local) | `C:\dev\projects\mcpserver-irix\release-assets\` (gitignored) |
| SS/Outbox (Windows) | `S:\Outbox` |
| SS/Outbox (Octane2) | `/nfs/galaxy/SS/Outbox` |
| Scratch disk image | `C:\dev\tools\iris\images\5.3\scratch.raw` |
| Scratch disk (IRIX r/w) | `/dev/rdsk/dks0d2s0` (partition 0, sector 0) |
| Scratch disk (whole vol) | `/dev/rdsk/dks0d2vol` (include VH; skip=8 from Windows) |
| Scratch payload offset | Byte 4096 / sector 8 in scratch.raw |
| TFTP server | `C:\dev\tools\tftpd\tftpd.py --port 6969 --dir <dir>` |
| NFS proxy | `scripts/nfs-proxy.py` (required before IRIX NFS mount) |
| unfsd binary | `C:\dev\tools\unfs3\unfsd.exe` |
| IRIS shared folder | `C:\dev\tools\iris\shared\` |
| Octane2 SSH | `root@speed.siliconsurf.net` |
| IRIS serial console | Raw TCP `localhost:8881` (PuTTY: Raw mode, local echo off) |
| IRIS monitor | `telnet localhost 8888` |
