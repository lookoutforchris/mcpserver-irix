---
name: release
description: Cut a full versioned release of mcpserver-irix to GitHub — bumps version everywhere, builds and packages all three IRIX targets, attaches tardists to a GitHub Release, installs on both machines, and smoke-tests. Use when the user says "release vX.Y.Z", "ship it", or "cut a release". Takes a version argument (e.g., "0.3.2").
---

# Release Workflow

End-to-end release procedure. The authoritative reference is `docs/RELEASE_WORKFLOW.md` — consult it for any case this skill doesn't cover, and update it if you learn something new.

## Preconditions

Before starting, confirm:
- Argument given: target version (e.g., `0.3.2`). If missing, ask.
- Both MCP servers (`irix-octane2`, `irix-indy53`) are reachable. `ping` both.
- IRIS emulator is running (PuTTY serial console at port 8881).
- Octane2 reachable via `ssh root@speed.siliconsurf.net`.
- Working tree is clean except for intended release changes. Run `git status` and confirm.
- Current `MCPSERVER_VERSION` in `src/compat/compat.h` matches `Makefile`'s `PKG_VERSION` and the three spec `id` strings. If they're already inconsistent, fix that as a separate commit before continuing.

## Steps

### 1. Version bump on Windows

Edit these files to the new version `X.Y.Z`:
- `src/compat/compat.h` → `#define MCPSERVER_VERSION "X.Y.Z"`
- `Makefile` → `PKG_VERSION = X.Y.Z`
- `packaging/irix65/mcpserver.spec` → `id "MCP Server for IRIX 6.5 X.Y.Z"`
- `packaging/irix62/mcpserver.spec` → `id "MCP Server for IRIX 6.2 X.Y.Z"`
- `packaging/irix53/mcpserver.spec` → `id "MCP Server for IRIX 5.3 X.Y.Z"`
- `README.md` → `Status: vX.Y.Z` line

Commit and push:
```
git add -A && git commit -m "vX.Y.Z: <one-line summary>" && git push origin main
```

### 2. Build irix65 + irix62 on Octane2

**Always wrap multi-step Octane2 SSH commands in `sh -c '...'`** — default shell is csh and `2>&1` fails there.

```
ssh root@speed.siliconsurf.net "sh -c 'cd /home/work/projects/mcpserver-irix && git pull && rm -f mcpserverd mcpserver && make irix65 2>&1 | tail -8'"
```

The `rm -f` is critical — make checks timestamps and skips work if binaries are newer than sources.

Verify the ISA:
```
ssh root@speed.siliconsurf.net "sh -c 'cd /home/work/projects/mcpserver-irix && file mcpserverd mcpserver'"
```
Expect `N32 MIPS-IV`. Stage to `stage/irix65/`, then build irix62:

```
ssh root@speed.siliconsurf.net "sh -c 'cd /home/work/projects/mcpserver-irix && mkdir -p stage/irix65 stage/irix62 stage/irix53 && cp mcpserverd mcpserver stage/irix65/ && make irix62 2>&1 | tail -8'"
```

Expect `N32 MIPS-III` for irix62. Stage to `stage/irix62/`.

### 3. Package irix65 + irix62 on Octane2

The IDB files reference binaries at the project root, so copy from stage before each tardist:

```
ssh root@speed.siliconsurf.net "sh -c 'cd /home/work/projects/mcpserver-irix && cp stage/irix65/mcpserverd stage/irix65/mcpserver . && make tardist 2>&1 | tail -5'"
ssh root@speed.siliconsurf.net "sh -c 'cd /home/work/projects/mcpserver-irix && cp stage/irix62/mcpserverd stage/irix62/mcpserver . && make irix62-tardist 2>&1 | tail -5'"
```

Pull the tardists to Windows:
```
scp root@speed.siliconsurf.net:/home/work/projects/mcpserver-irix/mcpserver-X.Y.Z-irix65.tardist .
scp root@speed.siliconsurf.net:/home/work/projects/mcpserver-irix/mcpserver-X.Y.Z-irix62.tardist .
```

### 4. Build and package irix53 on IRIS

**The irix53 tardist MUST be built on IRIX 5.3.** gendist on 6.5 produces a format that 5.3's `inst` rejects.

Update `compat.h` on IRIS via the `replace_text_file` MCP tool (it's small enough). Transfer Makefile + spec via scratch disk:

```
# Windows:
tar cf /tmp/transfer53.tar Makefile packaging/irix53/mcpserver.spec
dd if=/tmp/transfer53.tar of=C:/dev/tools/iris/images/5.3/scratch.raw bs=512 seek=8 conv=notrunc
```

Tell the user to run on IRIS (PuTTY at port 8881):
```
cd /usr/people/shared/projects/mcpserver-irix
dd if=/dev/rdsk/dks0d2vol bs=512 skip=8 count=<N> | tar xf -
make irix53-native SHELL=/bin/sh
make irix53-native-tardist SHELL=/bin/sh
tar cf - mcpserver-X.Y.Z-irix53.tardist | dd of=/dev/rdsk/dks0d2s0 bs=512
```

Where `<N>` = ceil(tar_size / 512) + 10. Read the dd output for the actual record count.

When they confirm the dd completed, extract on Windows:
```
dd if=C:/dev/tools/iris/images/5.3/scratch.raw bs=512 skip=8 count=<records> | tar xf -
```

### 5. Create the GitHub release

```
mkdir -p release-assets
cp mcpserver-X.Y.Z-*.tardist release-assets/
gh release create vX.Y.Z \
  release-assets/mcpserver-X.Y.Z-irix65.tardist \
  release-assets/mcpserver-X.Y.Z-irix62.tardist \
  release-assets/mcpserver-X.Y.Z-irix53.tardist \
  --title "vX.Y.Z — <one-line summary>" \
  --notes "<release notes — what changed, install instructions, ABI table>"
```

Standard release notes structure: "What's new" bullets, install commands per target (note IRIX 5.3 needs unpack-first), ABI/ISA table.

### 6. Install on both machines

**Octane2:**
```
ssh root@speed.siliconsurf.net "sh -c 'cd /home/work/projects/mcpserver-irix && git pull && rm -f mcpserverd mcpserver && make irix65 && make install && mcpserver version'"
```

Then restart daemon. **Must use `nohup ... > /dev/null 2>&1 &` via `sh -c`** — bare `&` gets killed when SSH disconnects.
```
ssh root@speed.siliconsurf.net "sh -c 'mcpserver stop; sleep 1; nohup /usr/sbin/mcpserverd > /dev/null 2>&1 &'"
```

**IRIS:** The binaries are already built on IRIS from step 4. Copy them into place via MCP `run_build_command` with `cp`:
```
cp /usr/people/shared/projects/mcpserver-irix/mcpserverd /usr/sbin/mcpserverd
cp /usr/people/shared/projects/mcpserver-irix/mcpserver /usr/bin/mcpserver
```

Tell the user to restart on IRIS via PuTTY:
```
mcpserver stop
/usr/sbin/mcpserverd &
```

The trailing `&` is mandatory — without it the shell hangs. (Memory: `feedback-daemon-start`.)

Ask the user to reconnect both MCP servers in the IDE so the new daemon is used.

### 7. Smoke test

After they reconnect, ping both and check version matches:
```
mcp__irix-octane2__ping
mcp__irix-indy53__ping
```

Both should report `"version":"X.Y.Z"`.

### 8. Cleanup

Run the `cleanup` skill — there will be stale tardists at the Octane2 project root and Windows project root after a release.

## Common pitfalls (encoded from past releases)

| Problem | Fix |
|---|---|
| `2>&1` fails on Octane2 SSH | Wrap entire command in `sh -c '...'` |
| `nohup` ambiguous redirect | Same — use `sh -c '...'` |
| `make irix65` says "Nothing to be done" | `rm -f mcpserverd mcpserver` first |
| irix62 build overwrites irix65 binaries at root | Always stage irix65 first |
| MCP server still reports old version after install | Reconnect the MCP server in the IDE — it holds an SSH session to the old binary |
| `inst -f tardist` fails on IRIX 5.3 | Unpack first: `mkdir /tmp/i && tar xf tardist && inst -f /tmp/i` |
| IRIX 5.3 install didn't overwrite binaries | Copy manually: `cp /path/to/built/mcpserverd /usr/sbin/` |
| Octane2 daemon dies when SSH disconnects | Use `nohup ... > /dev/null 2>&1 &` not bare `&` |
| Scratch disk extract truncated | `count=N` too small — use ceil(bytes/512) + 10 |
