---
name: cleanup
description: Filesystem hygiene check across Windows, Octane2, and IRIS. Detects and proposes removal of stale tardists, stray binaries at filesystem root, scattered .o files, and crash dumps. Use after a release or build session, or when the user says "clean up", "housekeeping", or "any final cleanup". No arguments — always reports before removing.
---

# Cleanup Workflow

After build and release sessions, three locations accumulate stray files. None should ever contain free-floating build artifacts. Report first, await approval before destructive action.

## Locations to check

### 1. Windows project root — `c:\dev\projects\mcpserver-irix\`

Should NOT contain:
- `mcpserver-X.Y.Z-*.tardist` at the project root (they belong in `release-assets/`)
- `mcpserverd` or `mcpserver` binaries (compile happens on IRIX hosts; nothing builds on Windows)
- `*.o` object files
- `/tmp/transfer*.tar` scratch tars (technically in `/tmp`, but worth purging)

Check:
```
ls c:/dev/projects/mcpserver-irix/*.tardist 2>/dev/null
ls c:/dev/projects/mcpserver-irix/mcpserverd c:/dev/projects/mcpserver-irix/mcpserver 2>/dev/null
ls c:/dev/projects/mcpserver-irix/*.o 2>/dev/null
```

### 2. Octane2 project root — `/home/work/projects/mcpserver-irix/`

Should NOT contain:
- `mcpserver-X.Y.Z-*.tardist` at the project root (built during release; not needed after they're on GitHub)
- Old stage/ contents from a previous release (decide with the user — sometimes kept intentionally)

Also check the filesystem root `/` for stray files. This has happened multiple times when a build command was run from `/` instead of the project root.

```
ssh root@speed.siliconsurf.net "sh -c 'ls /home/work/projects/mcpserver-irix/*.tardist 2>/dev/null; ls / | grep -E \"^(mcpserver|packaging|scripts)\" 2>/dev/null'"
```

### 3. IRIS project root — `/usr/people/shared/projects/mcpserver-irix/`

Should NOT contain:
- `*.o` files at the project root (a known limitation of `irix53-native` target — Makefile doesn't redirect to `build/irix53/`)
- `core` (IRIX core dumps from daemon crashes)
- Old tardists

Also check IRIS filesystem root `/` for stray files (same hazard as Octane2).

Use `mcp__irix-indy53__list_directory` on `/usr/people/shared/projects/mcpserver-irix` and on `/`. Look for entries that don't belong.

## Reporting

Produce a table of what was found where. Example:
```
Windows root:    6 stale tardists (0.3.0 and 0.3.1)
Octane2 root:    2 stale tardists (irix65, irix62 — already on GitHub release)
IRIS root:       15 .o files, 1 core dump, 1 tardist
Octane2 /:       clean
IRIS /:          clean
```

Then propose specific `rm` commands. **Wait for approval before deleting.**

## Removal commands

After approval:

**Windows:**
```
rm c:/dev/projects/mcpserver-irix/mcpserver-*.tardist
```

**Octane2:**
```
ssh root@speed.siliconsurf.net "sh -c 'rm /home/work/projects/mcpserver-irix/mcpserver-*.tardist'"
```

**IRIS** (via MCP `run_build_command` with `rm`):
```
rm -f snprintf.o realpath.o fnmatch.o json.o policy.o protocol.o tools_fs.o tools_text.o tools_write.o tools_exec.o tools_build.o ipc.o mcpserverd.o stdio_bridge.o mcpserver.o core mcpserver-*.tardist
```

(work_dir = `/usr/people/shared/projects/mcpserver-irix`)

## Notes

- Never delete `stage/` on Octane2 without asking — sometimes preserved for the next release cycle
- `release-assets/` on Windows is gitignored and is the correct home for tardists
- A `core` file on IRIS is benign (crash dump from old daemon during install) but should still be removed
- Don't touch user files in `/home/chris/` on Octane2 or `/usr/people/shared/` outside the project root
