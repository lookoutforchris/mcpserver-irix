---
name: smoke-test
description: Verify both IRIX MCP servers (Octane2 and IRIS) are reachable, running the expected version, and that key features work. Use after a release, after a daemon restart, or any time the user says "smoke test", "sanity check", "are they up?", or "verify both servers". No arguments.
---

# Smoke Test

Quick health check of both IRIX MCP servers.

## Procedure

### 1. Determine the expected version

Read `src/compat/compat.h` for the `MCPSERVER_VERSION` string. Both servers should match this.

### 2. Ping both servers in parallel

Use both ping tools in a single message:
- `mcp__irix-octane2__ping`
- `mcp__irix-indy53__ping`

Expected response shape:
```json
{"ok":true,"server":"irix-mcpserver","version":"X.Y.Z","profile":"full"}
```

Both should report `version` equal to the expected version. If either reports an older version, the most likely cause is the IDE is still holding an SSH session to the old daemon binary — tell the user to reconnect that MCP server in the IDE.

### 3. Verify the `man` command works on both

This was the headline v0.3.1 fix. Run in parallel:
- `mcp__irix-octane2__run_inspect_command` with `command="man"`, `args=["hinv"]`
- `mcp__irix-indy53__run_inspect_command` with `command="man"`, `args=["hinv"]`

Both should return a populated `stdout` with the `hinv` man page (the IRIX 6.5 version has more options than the 5.3 version — both are correct for their respective OS).

If `man` returns `"command does not accept path arguments"` or similar, the daemon is older than v0.3.1 and the `man` fix isn't deployed.

### 4. Spot-check policy roots

List the project root on each system as a quick sanity check:
- Octane2: `mcp__irix-octane2__list_directory` on `/home/work/projects/mcpserver-irix`
- IRIS: `mcp__irix-indy53__list_directory` on `/usr/people/shared/projects/mcpserver-irix`

Should succeed with directory listings.

### 5. Report

Summary line: "Both servers v0.X.Y, man command working, policy roots accessible." Or call out exactly what failed.

## Common failure modes

| Symptom | Likely cause |
|---|---|
| Ping shows old version | IDE holds SSH session to old binary — reconnect MCP server |
| Ping fails with connection error on IRIS | tcp-bridge.ps1 stale; reconnect the MCP server |
| Ping fails with connection error on Octane2 | Daemon died, or SSH key issue; SSH manually to check |
| `man hinv` returns "command not in allowed list" | Pre-v0.3.1 binary — need to install v0.3.1+ |
| list_directory returns "not allowed" | boundaries.json missing or policy roots changed |
