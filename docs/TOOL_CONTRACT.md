# IRIX MCP Server — Tool Contract

This document is the authoritative specification for all MCP tools exposed by the IRIX MCP server. It is the behavioral agreement across all IRIX target implementations, Codex/Claude expectations, and regression tests.

**Tool profile:** The server operates in one of two profiles, set at daemon startup via config:
- `readonly` — write tools are not advertised in `tools/list`; execution tools are advertised but reject calls at runtime
- `full` — all tools advertised and callable (default)

The current implementation reports `17` tools in the `full` profile (`12` in `readonly`).

---

## 1. Tool List

### 1.1 Read / Inspect Tools (advertised in all profiles)

| Tool | Description |
|---|---|
| `ping` | Server liveness and identity |
| `path_exists` | Check if a path is allowed and exists |
| `stat_path` | File metadata (kind, size) |
| `list_directory` | Directory contents |
| `read_text_file` | Read a slice of a text file |
| `tail_text_file` | Read the last N lines of a text file |
| `search_text` | Substring/pattern search under a root |
| `read_text_around_pattern` | Context lines around a pattern match |
| `safe_json_preview` | Preview JSON structure without full content |

### 1.2 Execution Tools (advertised in all profiles; callable only in `full`)

| Tool | Description |
|---|---|
| `run_inspect_command` | Whitelisted inspection commands (ls, find, grep, hinv, man, etc.) |
| `run_build_command` | Whitelisted build/packaging tools (cc, make, ar, tar, gendist, inst, etc.) |
| `run_program` | Run a compiled binary or test executable within a policy root |

### 1.3 Write / Path Tools (full profile only)

| Tool | Description |
|---|---|
| `create_text_file` | Create a new text file |
| `replace_text_file` | Overwrite an existing text file |
| `make_directory` | Create a single directory |
| `delete_text_file` | Delete a text file |
| `rename_path` | Rename a file or directory within policy roots |

---

## 2. Tool Specifications

### `ping()`

Returns server identity and confirms the daemon is reachable.

**Parameters:** none

**Returns:**
```json
{
  "ok": true,
  "server": "irix-mcpserver",
  "version": "0.3.1",
  "profile": "full"
}
```

The `version` field tracks `MCPSERVER_VERSION` in `src/compat/compat.h` and is bumped each release.

---

### `path_exists(path)`

Checks whether a path is within an allowed root and whether it exists on disk.

**Parameters:**
- `path` (string, required) — absolute path to check

**Returns:**
```json
{
  "allowed": true,
  "exists": true,
  "path": "/work/myproject/src/main.c"
}
```

- `allowed: false` means the path is outside all allowed roots or matches a deny rule. The `exists` field is always `false` when `allowed` is `false` (do not leak information about denied paths).

---

### `stat_path(path)`

Returns metadata for a path within an allowed root.

**Parameters:**
- `path` (string, required)

**Returns:**
```json
{
  "allowed": true,
  "exists": true,
  "path": "/work/myproject/src/main.c",
  "kind": "file",
  "size_bytes": 4096
}
```

- `kind`: `"file"` | `"directory"` | `"other"` (symlinks, devices, etc.)
- `size_bytes`: null for directories and non-files
- `exists: false` when path does not exist (but is allowed)

---

### `list_directory(path)`

Lists immediate children of a directory within an allowed root.

**Parameters:**
- `path` (string, required)

**Returns:**
```json
{
  "allowed": true,
  "exists": true,
  "path": "/work/myproject/src",
  "entries": [
    {"name": "main.c", "kind": "file"},
    {"name": "utils", "kind": "directory"}
  ]
}
```

- Entries are not recursed. Entries matching deny rules are silently omitted.
- Maximum 500 entries returned. If truncated, `"truncated": true` is included.

---

### `read_text_file(path, start_line, max_lines)`

Reads a slice of a text file.

**Parameters:**
- `path` (string, required)
- `start_line` (integer, default 1) — 1-based line number to start from
- `max_lines` (integer, default 200, max 500)

**Returns:**
```json
{
  "allowed": true,
  "exists": true,
  "path": "/work/myproject/src/main.c",
  "content": "...",
  "start_line": 1,
  "lines_returned": 200,
  "truncated": false,
  "error": null
}
```

- Content is clipped at 20000 characters regardless of line count.
- Binary files return `"error": "binary file"` with empty content.

---

### `tail_text_file(path, lines)`

Returns the last N lines of a text file.

**Parameters:**
- `path` (string, required)
- `lines` (integer, default 100, max 500)

**Returns:** Same shape as `read_text_file`. `start_line` is the actual line number of the first returned line.

---

### `search_text(root_path, pattern, include_globs, max_results, case_sensitive)`

Searches for a substring or pattern recursively under a root path.

**Parameters:**
- `root_path` (string, required) — must be an allowed root or subdirectory of one
- `pattern` (string, required) — literal substring to search for (no regex in v1)
- `include_globs` (array of strings, default `["*"]`) — file glob filters, e.g. `["*.c", "*.h"]`
- `max_results` (integer, default 50, max 200)
- `case_sensitive` (boolean, default true)

**Returns:**
```json
{
  "allowed": true,
  "root_path": "/work/myproject",
  "pattern": "mcpserverd",
  "matches": [
    {"path": "/work/myproject/src/main.c", "line_number": 12, "line": "    mcpserverd started"}
  ],
  "truncated": false
}
```

- Directories matching deny rules are pruned before recursion (do not descend into denied subtrees).
- Binary files are skipped silently.

---

### `read_text_around_pattern(path, pattern, context_before, context_after, match_index, case_sensitive)`

Returns context lines around a specific occurrence of a pattern in a file.

**Parameters:**
- `path` (string, required)
- `pattern` (string, required)
- `context_before` (integer, default 10)
- `context_after` (integer, default 20)
- `match_index` (integer, default 1) — which occurrence (1-based)
- `case_sensitive` (boolean, default true)

**Returns:**
```json
{
  "allowed": true,
  "exists": true,
  "path": "/work/myproject/src/main.c",
  "found": true,
  "match_line": 42,
  "content": "...",
  "error": null
}
```

- `found: false` if the pattern does not appear (or the requested `match_index` does not exist).

---

### `safe_json_preview(path, top_level_only, max_bytes)`

Reads a JSON file and returns a preview of its structure.

**Parameters:**
- `path` (string, required)
- `top_level_only` (boolean, default true) — if true, return only top-level keys with value type summaries
- `max_bytes` (integer, default 12000, max 50000)

**Returns:**
```json
{
  "allowed": true,
  "exists": true,
  "path": "/work/myproject/config.json",
  "content": "{\"version\": 1, \"projects\": [<3 items>]}",
  "truncated": false,
  "error": null
}
```

- `error` is set if the file is not valid JSON.

---

### `run_inspect_command(command, args)`

Executes a single allowed inspection command with validated arguments. **Full profile only.**

**Parameters:**
- `command` (string, required) — must be in the allowed command list
- `args` (array of strings, required) — per-command validated arguments

**Returns:**
```json
{
  "allowed": true,
  "command": "ls",
  "args": ["-l", "/work/myproject/src"],
  "stdout": "...",
  "stderr": "",
  "exit_code": 0,
  "truncated": false,
  "error": null
}
```

- `allowed: false` if the command is not in the allowed list, arguments fail validation, or any path argument is outside allowed roots.
- Output is clipped at 20000 characters total (stdout + stderr combined). `truncated: true` if clipped.
- No shell interpretation. Executed directly via `execv()` with explicit argument array.
- Pipes, redirects, semicolons, and shell metacharacters in arguments are rejected.

**Allowed commands** (as of v0.3.1) — defined in `src/core/tools_exec.c` `CMD_TABLE`:

| Category | Commands |
|---|---|
| Filesystem inspection | `pwd`, `ls`, `find`, `cat`, `head`, `tail`, `wc`, `stat`, `file`, `du`, `df` |
| Text inspection | `grep`, `sed`, `sort`, `diff`, `strings`, `od` |
| Binary inspection | `nm`, `size`, `ldd`, `ar`, `what` |
| System inspection | `uname`, `hinv`, `versions`, `uptime`, `w`, `ps`, `chkconfig` |
| Network inspection | `netstat`, `ifconfig`, `rpcinfo`, `showmount`, `mount`, `umount` |
| Process control | `kill` (signals only — no PID outside policy) |
| Documentation | `man` (added v0.3.1 — arguments treated as page names, not file paths) |

Per-command argument validation rules are in `SECURITY_MODEL.md §4`.

---

### `run_build_command(command, args, work_dir)`

Runs a build, compile, link, or packaging tool inside a configured project root. Captures stdout/stderr with a longer output buffer and timeout than `run_inspect_command`. **Full profile only.** Added in v0.3.0.

**Parameters:**
- `command` (string, required) — must be in the allowed build tool list
- `args` (array of strings, required) — passed verbatim except for shell-metacharacter rejection (`| & ; > < \n \r $ \``)
- `work_dir` (string, optional) — working directory; must be within a configured policy root. Defaults to the daemon's cwd.

**Returns:**
```json
{
  "allowed": true,
  "command": "make",
  "args": ["irix65"],
  "work_dir": "/home/work/projects/mcpserver-irix",
  "stdout": "...",
  "stderr": "...",
  "exit_code": 0,
  "truncated": false,
  "error": null
}
```

- Output buffer: 64 KB (vs 20 KB for `run_inspect_command`).
- Timeout: 300 seconds.
- Unlike `run_inspect_command`, arguments are NOT path-validated (compilers and `make` legitimately reference paths outside policy roots, e.g. `/usr/include`, `/opt/MIPSpro/lib`). Shell metacharacters are still rejected.
- `work_dir` IS validated against policy roots.

**Allowed commands** (as of v0.3.1) — defined in `src/core/tools_build.c` `BUILD_TABLE`:

| Category | Commands |
|---|---|
| Compilers | `cc`, `c++`, `CC` |
| Build systems | `make`, `gmake` |
| Linkers / archivers | `ld`, `ar`, `ranlib` |
| Binary tooling | `strip`, `elfdump`, `dis` |
| Filesystem | `chmod`, `chown`, `cp`, `mv`, `ln`, `mkdir`, `rm`, `install` |
| Archive | `tar`, `gzip`, `compress`, `uncompress` |
| Packaging | `makedist`, `swpkg`, `inst`, `gendist` |

---

### `run_program(program, args, work_dir)`

Executes a compiled program by absolute path within a policy root. Used for running test binaries, smoke-test helpers, or any locally-built executable. **Full profile only.** Added in v0.3.0.

**Parameters:**
- `program` (string, required) — absolute path; must be within a configured policy root
- `args` (array of strings, required) — passed verbatim except for shell-metacharacter rejection
- `work_dir` (string, optional) — working directory; must be within a configured policy root

**Returns:**
```json
{
  "allowed": true,
  "program": "/home/work/projects/myproj/build/test_runner",
  "args": ["--verbose"],
  "work_dir": "/home/work/projects/myproj",
  "stdout": "...",
  "stderr": "",
  "exit_code": 0,
  "truncated": false,
  "error": null
}
```

- Output buffer: 64 KB. Timeout: 300 seconds.
- The program path is validated against policy roots before execution. Programs outside roots cannot be invoked (this prevents running arbitrary system binaries via this tool — use `run_inspect_command` or `run_build_command` for whitelisted system tools).
- The file must exist and be executable (`stat` + `x` permission check) before fork/exec.

---

### `create_text_file(path, content)`

Creates a new text file. **Full profile only.**

**Parameters:**
- `path` (string, required) — must be in a read-write root, extension must be in the allowlist
- `content` (string, required)

**Returns:**
```json
{
  "allowed": true,
  "path": "/work/myproject/notes.md",
  "created": true,
  "error": null
}
```

- Fails if the file already exists. Use `replace_text_file` to overwrite.
- `allowed: false` if the path is not in a read-write root, the extension is not allowed, or the path matches a deny rule.

---

### `replace_text_file(path, content)`

Overwrites an existing text file. **Full profile only.**

**Parameters:**
- `path` (string, required)
- `content` (string, required)

**Returns:**
```json
{
  "allowed": true,
  "path": "/work/myproject/notes.md",
  "replaced": true,
  "error": null
}
```

- Fails if the file does not exist. Use `create_text_file` for new files.

---

### `make_directory(path)`

Creates a single directory. **Full profile only.**

**Parameters:**
- `path` (string, required)

**Returns:**
```json
{
  "allowed": true,
  "path": "/work/myproject/newdir",
  "created": true,
  "error": null
}
```

- Does not create intermediate directories (`mkdir`, not `mkdir -p`).

---

### `delete_text_file(path)`

Deletes a text file. **Full profile only.**

**Parameters:**
- `path` (string, required)

**Returns:**
```json
{
  "allowed": true,
  "path": "/work/myproject/old_notes.md",
  "deleted": true,
  "error": null
}
```

- File extension must be in the write allowlist (same check as create/replace).

---

### `rename_path(source_path, dest_path)`

Renames a file or directory. **Full profile only.**

**Parameters:**
- `source_path` (string, required)
- `dest_path` (string, required)

**Returns:**
```json
{
  "allowed": true,
  "source": "/work/myproject/old.md",
  "dest": "/work/myproject/new.md",
  "renamed": true,
  "error": null
}
```

- Both source and dest must be within the same read-write root.
- Does not move across roots.

---

## 3. Output Limits (Summary)

| Limit | Value |
|---|---|
| `read_text_file` default lines | 200 |
| `read_text_file` max lines | 500 |
| `tail_text_file` default lines | 100 |
| `tail_text_file` max lines | 500 |
| `search_text` default results | 50 |
| `search_text` max results | 200 |
| `list_directory` max entries | 500 |
| `safe_json_preview` default bytes | 12000 |
| `safe_json_preview` max bytes | 50000 |
| Command output clip | 20000 chars |
| Content clip (all text tools) | 20000 chars |

---

## 4. Error Semantics

- **`allowed: false`**: The request is outside policy. No further information is provided (do not leak denied path existence).
- **`exists: false`**: The path is allowed but does not exist on disk.
- **`error: "<message>"`**: An operational error occurred (binary file, JSON parse failure, command not found, I/O error). The `allowed` and `exists` fields are still set correctly.
- **JSON-RPC errors** (protocol-level): Used only for malformed requests (missing required parameters, wrong types). Tool-level failures use the structured result fields above.

---

## 5. Explicitly Out of Scope

These are deliberate design choices, not gaps to be filled. They apply to the current 0.3.x line and any successor release unless explicitly revisited.

### Execution surface
- Arbitrary shell execution. No `sh -c`, no pipes, no redirection, no shell expansion of any kind. Commands always go through `execv()` with an explicit argument array against an allowlisted binary table.
- Argument metacharacters (`| & ; > < \n \r $ \``) are rejected by every execution tool, even for build commands that don't path-validate arguments.

### Filesystem surface
- Binary file read or write. The write tools advertise as `..._text_file` deliberately.
- Recursive directory deletion. `rm -rf` is reachable only through `run_build_command`'s `rm`, where the argument check still blocks shell expansion but permits flag arguments — this is intentional for build workflows but is not a separate tool.
- Symbolic link creation. (Reading a symlink target through `stat_path`/`list_directory` is fine; `kind` reports `"other"`.)
- PDF, image, audio, or other binary-specific tools.

### Protocol surface
- MCP `resources`, `prompts`, `roots`, or any optional MCP capability beyond `initialize`, `tools/list`, and `tools/call`. The server advertises only the `tools` capability.
- Notifications, progress streams, cancellation. All tool calls are synchronous request/response.

### Network and trust surface
- Authentication, access tokens, or any auth model beyond OS-level SSH access to the host.
- Public network exposure. The daemon binds only to `/var/run/mcpserverd.sock` (UNIX domain socket, mode 0600).
- Outbound network calls from the daemon. The server never connects to the internet.

### Things that are NOT out of scope (clarifications)
- Build and packaging workflows ARE in scope as of v0.3.0 (`run_build_command`, `run_program`).
- Running compiled test binaries IS in scope as of v0.3.0 (`run_program`).
- Reading IRIX man pages IS in scope as of v0.3.1 (`run_inspect_command` with `man`).
