---
name: daemon-restart
description: Restart mcpserverd on Octane2 and/or IRIS after a binary update or policy change. Encodes the non-obvious incantations needed (sh -c wrapper, nohup, ampersand) that have repeatedly tripped past sessions. Use when the user says "restart the daemon", "reload mcpserverd", or after install. Takes optional argument: "octane2", "iris", or "both" (default).
---

# Daemon Restart

Restarting `mcpserverd` correctly requires several non-obvious details. This skill encodes them.

## Octane2

The default shell on Octane2 is csh. Several things fail there:
- `2>&1` is parsed as ambiguous redirect
- A bare `&` backgrounded process is killed when SSH disconnects (HUP)

The pattern that works:
```
ssh root@speed.siliconsurf.net "sh -c 'mcpserver stop; sleep 1; nohup /usr/sbin/mcpserverd > /dev/null 2>&1 &'"
```

- `sh -c '...'` so the redirections work
- `nohup` so SIGHUP from SSH disconnect doesn't kill the daemon
- Redirect stdout+stderr to `/dev/null` so nohup doesn't try to log to a file in cwd
- Trailing `&` to background within the shell

The `sleep 1` between stop and start is to let the old daemon release the socket file.

To verify:
```
mcp__irix-octane2__ping
```

If the response still shows the old version, the IDE's MCP server connection is holding an SSH session to the old binary. The user must reconnect the MCP server in the IDE.

## IRIS

There is no SSH into IRIS. The only ways to restart the daemon are:
- The user typing into the PuTTY serial console (port 8881)
- Sending a SIGHUP via MCP (only reloads policy, doesn't replace the binary)
- Killing PID via MCP `run_inspect_command` with `kill -TERM <pid>`, then starting via MCP

The simplest reliable path is to ask the user to run on PuTTY:
```
mcpserver stop
/usr/sbin/mcpserverd &
```

The trailing `&` is mandatory — without it the shell hangs (no SSH disconnect to worry about here, but the daemon doesn't self-daemonize). See memory `feedback-daemon-start`.

Then ask them to reconnect the irix-indy53 MCP server in the IDE.

To verify:
```
mcp__irix-indy53__ping
```

## After restart on either system

Always remind the user that the MCP server connection in the IDE may still be using the OLD daemon. The ping result is the source of truth — if version doesn't match, ask for a reconnect.

## Common pitfalls

| Symptom | Fix |
|---|---|
| `Ambiguous output redirect` on Octane2 | Wrap in `sh -c '...'` |
| Daemon dies after SSH disconnects | Use `nohup` and redirect to /dev/null |
| Old version still shows after restart | IDE holds old SSH connection — user must reconnect MCP server |
| `mcpserver stop` segfaults (old daemon) | Harmless if you're replacing it; the new daemon will start clean |
| IRIS shell hangs after typing daemon command | Missing `&` — kill the shell, retype with `&` |
| Socket bind error on start | Old socket file persists; `rm /var/run/mcpserverd.sock` then start |
