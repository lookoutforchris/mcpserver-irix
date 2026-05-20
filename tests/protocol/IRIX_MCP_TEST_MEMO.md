# IRIX MCP Server Testing Memo

Date: 2026-05-19

## Context

This memo summarizes live testing performed against the `irix-octane2`
MCP server running on the SGI Octane2 IRIX host.

The Windows-side repo contains `.mcp.json` with this configured MCP server:

```json
{
  "mcpServers": {
    "irix-octane2": {
      "type": "stdio",
      "command": "ssh",
      "args": [
        "-T",
        "-o", "BatchMode=yes",
        "root@speed.siliconsurf.net",
        "/home/chris/src/mcpserver-irix/mcpserver",
        "stdio"
      ]
    }
  }
}
```

The exposed Octane project root is:

```text
/home/chris/src/mcpserver-irix
```

This is the same folder used for the development loop:

1. edit code on Windows
2. sync/pull on IRIX
3. run `make` on IRIX
4. run the resulting binaries from that same folder
5. interact with the live daemon through `mcpserver stdio`

No server implementation files were changed during this testing pass.
The only new artifact is the test harness:

```text
tests/protocol/irix_ssh_stdio_harness.ps1
```

## Server Reachability Findings

The Octane MCP server is reachable manually through the configured SSH stdio
bridge.

Direct CLI checks over SSH reported:

```text
mcpserver 0.1.0
Boot:   disabled
Daemon: running (pid 8049)
```

The server configuration initially showed the project root as read-only:

```text
test                  read-only   /home/chris/src/mcpserver-irix
```

After the MCP server permissions were changed to read-write, the harness
confirmed write tools were accepted and functional inside the disposable test
folder.

## Multi-Server Codex Observation

Codex had a callable MCP namespace for the Galaxy/Synology server, but did not
surface `irix-octane2` as a first-class callable MCP namespace in the active
tool list.

The Galaxy server identified itself as:

```text
server: openai-mcp
project_name: galaxy
project_root: /volume1/docker
boundaries_file: /config/boundaries.json
```

The Octane server was still reachable by manually running the `.mcp.json` SSH
stdio command from the Windows shell. This suggests the Octane configuration
exists on disk and works, but was not mounted as an active Codex MCP tool
surface in this session.

## MCP Handshake Findings

Manual JSON-RPC initialization over the SSH stdio bridge succeeded:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "protocolVersion": "2024-11-05",
    "capabilities": {
      "tools": {}
    },
    "serverInfo": {
      "name": "irix-mcpserver",
      "version": "0.1.0"
    }
  }
}
```

`tools/list` succeeded when sent after `initialize` without first sending
`notifications/initialized`.

The server advertised 15 tools:

```text
ping
path_exists
stat_path
list_directory
read_text_file
tail_text_file
search_text
read_text_around_pattern
safe_json_preview
run_inspect_command
create_text_file
replace_text_file
make_directory
delete_text_file
rename_path
```

## Initial Directory Listing

The Octane server successfully listed the project root:

```text
.git/
.gitattributes
.gitignore
AGENTS.md
README.md
docs/
src/
tests/
scripts/
.claude/
.mcp.json
Makefile
mcpserverd.o
mcpserverd
realpath.o
fnmatch.o
json.o
policy.o
protocol.o
tools_fs.o
tools_text.o
tools_write.o
tools_exec.o
ipc.o
stdio_bridge.o
mcpserver.o
mcpserver
```

## Test Harness

A PowerShell protocol harness was added:

```text
tests/protocol/irix_ssh_stdio_harness.ps1
```

It:

- reads the `irix-octane2` command from `.mcp.json`
- searches upward for `.mcp.json`, so it works from repo root or
  `tests/protocol`
- starts the configured SSH stdio process
- sends newline-delimited JSON-RPC requests
- performs MCP `initialize`
- calls `tools/list`
- exercises read/path/search/json/command/write tools
- confines all write/delete activity to:

```text
/home/chris/src/mcpserver-irix/.mcp-test
```

Normal run:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tests\protocol\irix_ssh_stdio_harness.ps1
```

Optional bridge rough-edge probe:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tests\protocol\irix_ssh_stdio_harness.ps1 -IncludeBridgeRoughEdges
```

## Current Harness Results

After changing the Octane MCP project root to read-write, the harness result
was:

```text
20 passed, 1 failed, 0 skipped
```

Passing areas:

- MCP initialize
- tools/list
- ping
- list_directory
- stat_path
- read_text_file
- tail_text_file
- search_text
- read_text_around_pattern
- safe_json_preview
- denied path non-disclosure
- nonexistent allowed path behavior
- command metacharacter rejection
- make_directory in `.mcp-test`
- create_text_file in `.mcp-test`
- replace_text_file in `.mcp-test`
- read_text_file of updated fixture
- rename_path in `.mcp-test`
- delete_text_file in `.mcp-test`
- invalid method JSON-RPC error

## Main Failure: run_inspect_command Times Out

The one normal harness failure is:

```text
FAIL  run_inspect_command pwd is constrained
      error=command timed out
```

The harness calls:

```json
{
  "name": "run_inspect_command",
  "arguments": {
    "command": "pwd",
    "args": []
  }
}
```

Expected behavior:

```text
allowed=true
exit_code=0
stdout contains working directory
error=null
```

Actual behavior:

```text
allowed=true
exit_code=-1
error="command timed out"
```

A targeted `ls` probe produced more detail. The command was:

```json
{
  "name": "run_inspect_command",
  "arguments": {
    "command": "ls",
    "args": ["-1", "/home/chris/src/mcpserver-irix"]
  }
}
```

Actual result shape:

```json
{
  "allowed": true,
  "command": "ls",
  "args": [],
  "stdout": "AGENTS.md\nMakefile\nREADME.md\n...",
  "stderr": "",
  "exit_code": -1,
  "truncated": false,
  "error": "command timed out"
}
```

Interpretation:

- command validation appears to work
- command allowlisting appears to work
- path argument authorization appears to work
- the child command can execute
- stdout can be captured
- normal completion/exit-status detection appears to fail
- the tool reports timeout even after useful output was captured

Secondary observation:

- `run_inspect_command` reports `"args":[]` even when arguments were supplied.

This looks like an execution/capture/waiting issue rather than a policy issue.

## Bridge Rough Edge: notifications/initialized

The optional rough-edge probe fails:

```text
FAIL  bridge handles notification before next request
      timed out waiting for response to tools/list
```

Sequence:

1. send `initialize`
2. receive initialize response
3. send `notifications/initialized`
4. send `tools/list`
5. wait for response

Expected behavior:

- notification should not produce a response
- bridge should continue reading
- `tools/list` should return normally

Actual behavior:

- `tools/list` times out after the notification

Earlier manual testing also showed that piping several JSON-RPC lines into the
SSH stdio bridge can exit without printing expected responses. Keeping the SSH
stdio process open interactively and reading after each request works.

Likely area to inspect:

- stdio bridge request/response loop behavior around JSON-RPC notifications
- stdin readability handling while waiting for daemon responses
- assumptions that every inbound JSON-RPC line has a response

## Read/Write Tool Behavior

Once the root was changed to read-write, the write tools passed in the
disposable test folder:

```text
PASS  make_directory creates disposable test root
PASS  create_text_file creates fixture
PASS  replace_text_file updates fixture
PASS  read_text_file sees updated fixture
PASS  rename_path renames fixture
PASS  delete_text_file removes renamed fixture
```

The harness leaves the `.mcp-test` directory itself in place, but deletes the
test file it creates and renames.

All destructive activity is intentionally scoped to:

```text
/home/chris/src/mcpserver-irix/.mcp-test
```

## Security/Boundary Behavior Observed

Denied path probe:

```json
{
  "name": "path_exists",
  "arguments": {
    "path": "/etc/passwd"
  }
}
```

Observed:

```text
allowed=false
exists=false
```

This is good: the denied path does not reveal existence.

Nonexistent path under the allowed root:

```text
/home/chris/src/mcpserver-irix/.definitely-not-present-mcp-harness
```

Observed:

```text
allowed=true
exists=false
```

This is also good: allowed-but-missing and denied paths are distinguishable only
when the path is inside the authorized root.

Command argument rejection worked for shell metacharacters:

```json
{
  "command": "ls",
  "args": ["/home/chris/src/mcpserver-irix;id"]
}
```

Observed result was rejected, as expected.

## Suggested Next Tests

These are testing-only suggestions:

1. Add more `run_inspect_command` cases once the timeout bug is fixed:
   `cat`, `grep`, `head`, `tail`, `wc`, `sed`, denied paths, invalid flags.
2. Add path traversal probes:
   `/home/chris/src/mcpserver-irix/../mcpserver-irix/README.md`,
   attempts to escape through `..`, and symlink escapes if safe fixtures can be
   created outside the root.
3. Add write-deny tests:
   `.pem`, `.key`, `.secret`, object files, extensionless executable names.
4. Add text edge cases in `.mcp-test`:
   empty file, missing final newline, long line, many-line file, binary-ish
   bytes if supported by test tooling.
5. Add concurrent bridge sessions:
   two SSH stdio processes calling read tools at the same time.
6. Add daemon restart behavior tests:
   status before/after restart, stale socket behavior, active bridge failure
   mode during restart.

## Bottom Line

The live Octane MCP server is broadly functional for core read/path/write MCP
tools against the exposed project root.

The two main rough edges found so far are:

1. `run_inspect_command` can execute and capture stdout but reports timeout
   instead of normal completion.
2. The stdio bridge appears to mishandle JSON-RPC notifications, especially
   `notifications/initialized`, causing the next request to time out.

The PowerShell harness now provides a repeatable way to verify fixes from the
Windows workstation against the real IRIX daemon without modifying existing
project files outside the dedicated `.mcp-test` folder.
