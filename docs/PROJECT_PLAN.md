# IRIX MCP Server Project Plan

**Working project name:** `irix-mcpserver`  
**Planning baseline:** 2026-05-18  
**Primary development owner:** lookoutforchris  
**Primary coding workflow:** Codex inside VS Code on the local Windows workstation  
**Initial live hardware target:** SGI Octane2 running IRIX 6.5.30  
**Long-range platform target:** IRIX 6.5.x, IRIX 6.2, and IRIX 5.3 where practical and supportable

---

## 1. Executive Summary

This project will create a **native, portable, local-first MCP server for IRIX** so modern AI coding agents can safely inspect, read, write, search, and perform constrained command execution on vintage SGI systems.

The project is intentionally ambitious:

- It should work well on the developer's Octane2 running IRIX 6.5.30.
- It should be designed from the outset so that **IRIX 6.2** and **IRIX 5.3** can be supported with targeted builds and compatibility work.
- It should expose a **consistent tool contract** inspired by the already-successful Galaxy MCP server project, while avoiding Galaxy's public-HTTP/OAuth complexity.
- It should be **local-first**: no public HTTP listener on IRIX, no Auth0, no remote cloud-facing exposure.
- It should eventually ship as clean **native IRIX tardist packages** installable through the normal IRIX software installation experience.
- It should be public on GitHub so the SGI hobbyist community can test, debug, port, and improve it.

The approved architectural direction is:

```text
mcpserverd      persistent IRIX daemon
mcpserver       operator/admin CLI
mcpserver stdio stdio MCP bridge for Claude Code / Codex
chkconfig       boot-time enable/disable integration
```

Clients such as Codex and Claude Code will not talk to a public IRIX HTTP endpoint. Instead, they will use a local stdio MCP command on the Windows workstation, likely wrapping an SSH invocation such as:

```text
local MCP host -> ssh to IRIX -> mcpserver stdio -> local daemon over IRIX IPC
```

This keeps the runtime local, operationally clean, and vintage-system appropriate.

---

## 2. Why This Project Matters

Vintage SGI systems remain compelling machines, but modern development loops around them are slow. Editing, searching, validating, and testing old code on IRIX often requires a lot of manual context switching. A careful MCP server changes that.

The goal is not to turn IRIX into a modern cloud service. The goal is to make it possible for a modern coding agent to interact with a real IRIX workspace in a controlled way:

- inspect a source tree,
- read and search files,
- edit code and text artifacts,
- run deliberately constrained inspection/build commands,
- observe logs and outputs,
- help diagnose portability issues,
- accelerate development for IRIX-native software.

This could materially improve how hobbyists work on SGI software, especially when paired with emulation for rapid iteration and real hardware for final validation.

---

## 3. Decisions Already Made

The following decisions are approved and should be treated as project constraints unless deliberately revisited.

### 3.1 Product Direction

- Build a **real public project**, not a one-off Octane2 helper.
- Host the source on **GitHub**.
- Keep the working project root on the **local Windows workstation**, not on Galaxy.
- Use **VS Code + Codex** as the main development/orchestration loop.
- Do **not** modify the existing Galaxy MCP server during this project. Galaxy becomes a reference design only.

### 3.2 Runtime Architecture

- IRIX MCP server is **local-first**, not public HTTP.
- IRIX runtime is a **daemon** plus **operator CLI** plus **stdio bridge**.
- The daemon should integrate with IRIX startup semantics so the user can ultimately do something equivalent to:

```sh
chkconfig mcpserver on
mcpserver start
mcpserver status
```

- A client-facing `mcpserver stdio` mode should let Claude Code or Codex connect through stdio.
- For remote use from Windows, the stdio process can be reached through SSH, allowing Codex to see a normal stdio MCP server while the actual logic runs on IRIX.

### 3.3 Implementation Strategy

- The final IRIX server should be implemented in **portable C**, not Python or Go.
- The source should be written conservatively enough that 6.5, 6.2, and 5.3 remain realistic targets.
- Development convenience on IRIX 6.5.30 may use SGUG-RSE, but **SGUG-RSE must not be a required runtime dependency** for the final distributable product.
- Do not assume one binary will run across all IRIX releases. Plan for **separate build targets and packages** if required.

### 3.4 Packaging Direction

- Final deliverables should be native IRIX install packages, ideally tardists.
- The final packages should install:
  - daemon binary,
  - operator CLI,
  - stdio bridge mode,
  - default config templates,
  - service/init integration,
  - documentation/man pages where feasible.
- The install should be clean and native-feeling.

---

## 4. What We Reuse from the Galaxy MCP Server

The Galaxy MCP server is not being modified right now, but it provides a very strong **reference design**. We should reuse its behavioral ideas, not necessarily its Python/FastMCP implementation.

### 4.1 Reuse the Tool Philosophy

Galaxy established a useful, bounded tool surface:

- `ping`
- `path_exists`
- `stat_path`
- `list_directory`
- `read_text_file`
- `tail_text_file`
- `search_text`
- `read_text_around_pattern`
- `safe_json_preview`
- write/path helpers such as:
  - `create_text_file`
  - `replace_text_file`
  - `make_directory`
  - `delete_text_file`
  - `rename_path`
- `run_inspect_command`

The IRIX project should begin from the same conceptual toolbox and preserve names/semantics where practical.

### 4.2 Reuse the Policy Model

Galaxy's separation of operator registry and generated boundary policy is excellent:

- `projects.json` as the operator-facing registry,
- `boundaries.json` as the active generated policy.

The IRIX design should keep this idea because it gives us:

- explicit roots,
- read-only vs read-write distinction,
- deny overrides,
- read/write deny rules,
- command allowlists,
- room for predictable config validation.

### 4.3 Reuse the Operator UX

Galaxy's `mcpserver` wrapper proved that the admin workflow should be narrow and explicit:

```text
help
status
logs
start
stop
restart
add
remove
validate
show
preview
apply
```

The IRIX project should intentionally mirror this vocabulary where it makes sense.

### 4.4 Reuse Reliability Habits

Galaxy also established several good patterns worth carrying forward:

- `readonly` and `full` profiles,
- structured return fields,
- early validation and preview before apply,
- timing logs around tool execution,
- smoke tests after changes,
- denied secret-file read tests,
- constrained shell/command exposure instead of arbitrary shell execution.

### 4.5 What We Do Not Reuse

Do **not** port the following Galaxy-specific choices into the IRIX runtime:

- public HTTP transport,
- Auth0/OAuth flow,
- reverse proxy assumptions,
- Docker/container logic,
- Synology filesystem assumptions,
- Galaxy-specific user/group normalization,
- Galaxy's service deployment lifecycle.

Those were right for Galaxy. They are not right for IRIX.

---

## 5. External Technology Context

### 5.1 Codex and VS Code

The project will be developed primarily in VS Code on Windows with Codex. Current Codex documentation supports:

- the VS Code IDE extension,
- local stdio MCP servers,
- shared MCP configuration between Codex CLI and the IDE extension.

This supports the intended development flow:

```text
VS Code + Codex on Windows
-> project repo on the Windows workstation
-> project-scoped docs and instructions
-> optional stdio MCP attachment to emulated or real IRIX systems later
```

### 5.2 MCP Specification Strategy

Target the current stable MCP specification baseline, not an in-flux draft. The project should keep its MCP protocol layer isolated enough to update later.

The v1 implementation should focus on the minimum needed for tool use:

- lifecycle initialization,
- capabilities negotiation,
- `tools/list`,
- `tools/call`,
- correct JSON-RPC message framing,
- clean error handling.

Resources, prompts, advanced extensions, and UI features are explicitly not v1 priorities.

### 5.3 IRIS Emulator

The IRIS emulator appears extremely relevant to this project. Its current documentation says it emulates an SGI Indy, boots IRIX 6.5 and 5.3, supports networking, framebuffer output, headless mode, and port forwarding.

That makes IRIS a strong candidate for:

- rapid 6.5 development loops,
- 5.3 compatibility exploration,
- scripted test runs,
- high-frequency crash/reboot testing without risking real hardware.

Important caveats:

- We should not assume IRIX 6.2 support in IRIS unless verified later.
- The project repo and any public CI must not ship proprietary IRIX installation media or ROM images. Emulator automation must use locally supplied images or legally distributable fixtures.

---

## 6. Proposed Product Architecture

### 6.1 Components

#### `mcpserverd`

A long-running daemon on IRIX that:

- loads active config,
- exposes a private local IPC endpoint,
- handles MCP tool operations requested by bridge clients,
- writes operational logs,
- stays up under normal service supervision.

#### `mcpserver`

An operator/admin command with subcommands such as:

```text
mcpserver help
mcpserver version
mcpserver status
mcpserver start
mcpserver stop
mcpserver restart
mcpserver logs [tail]
mcpserver enable
mcpserver disable
mcpserver validate
mcpserver show
mcpserver preview
mcpserver apply
mcpserver add <name> <root> --rw|--ro [...]
mcpserver remove <name>
mcpserver stdio
```

Some commands may ship later than others, but this is the intended UX.

#### `mcpserver stdio`

A stdio bridge mode that:

- speaks MCP over stdin/stdout to Codex or Claude,
- connects to `mcpserverd` locally via IRIX IPC,
- forwards requests/responses,
- emits logs to stderr if needed,
- exits cleanly when the downstream MCP client disconnects.

This mode is what makes the daemon usable by local MCP hosts without turning the daemon itself into a public network service.

### 6.2 IPC Between Bridge and Daemon

The daemon should use a **local-only IPC abstraction**. The likely first candidate is a UNIX-domain socket, but the implementation should isolate IPC details so we can adapt if one of the older IRIX targets exposes compatibility quirks.

The daemon should not require TCP listening for normal operation.

### 6.3 How Codex Reaches Real IRIX

The eventual Codex connection pattern should be:

```text
Codex MCP config on Windows
-> local command that starts SSH
-> SSH runs `mcpserver stdio` on the IRIX host
-> stdio bridge talks to local IRIX daemon
```

From Codex's perspective, this is still a stdio MCP server. The transport remains local-process-based at the client boundary.

---

## 7. Initial MCP Tool Contract

The IRIX project should define a formal tool contract document early, separate from implementation. That contract becomes the behavioral agreement across:

- Galaxy reference behavior,
- IRIX 6.5 implementation,
- IRIX 6.2 implementation,
- IRIX 5.3 implementation,
- Codex/Claude expectations,
- regression tests.

### 7.1 v1 Tool Candidates

#### Read/inspect

- `ping`
- `path_exists`
- `stat_path`
- `list_directory`
- `read_text_file`
- `tail_text_file`
- `search_text`
- `read_text_around_pattern`
- `safe_json_preview` if JSON support is implemented cleanly early

#### Write/path

- `create_text_file`
- `replace_text_file`
- `make_directory`
- `delete_text_file`
- `rename_path`

#### Constrained command execution

- `run_inspect_command`

### 7.2 v1 Out of Scope

- arbitrary shell execution,
- broad unrestricted file writes,
- public HTTP transport,
- OAuth/Auth0,
- remote internet exposure,
- UI components / MCP Apps,
- generalized binary file editing,
- PDF-specific tools unless later justified.

### 7.3 Output Shape Consistency

Return formats should preserve Galaxy's disciplined style where practical:

- explicit `allowed` fields,
- explicit `exists` where relevant,
- bounded `content`,
- bounded result counts,
- explicit `error` fields rather than vague failures,
- stable keys suitable for machine interpretation.

---

## 8. Security and Guardrails

This project is powerful precisely because it enables write and command operations on old systems. Security boundaries must be a first-class design concern.

### 8.1 Runtime Security Model

The IRIX runtime should rely on:

- local OS user permissions,
- SSH access control for remote invocation,
- explicit MCP tool allowlists,
- explicit path boundary policy,
- no public network service by default.

### 8.2 Filesystem Policy

The boundary engine should support:

- read-write roots,
- read-only roots,
- deny overrides,
- global deny globs,
- configurable text-write allowlists,
- prevention of path traversal / root escape.

The implementation must pay special attention to:

- `..` traversal,
- symlink escapes,
- relative-vs-absolute path normalization,
- race conditions between validation and open/write,
- consistently denying config/secrets paths.

The exact portability-safe path-resolution strategy must be validated on target IRIX releases.

### 8.3 Write Policy

Galaxy's text-only write philosophy should remain, but the IRIX project must support actual coding workflows. Therefore, the project should preserve an **explicit allowlist model** while deciding a sensible IRIX default profile.

Candidate categories for later evaluation:

- prose/config text: `.md`, `.txt`, `.json`,
- C/C++/assembly source: `.c`, `.h`, `.cc`, `.cpp`, `.s`, `.S`,
- shell/build files: `.sh`, `.mk`, `Makefile`, `Imakefile`, possibly other SGI-relevant build artifacts.

This must be a deliberate policy decision, not a casual widening of write access.

### 8.4 Command Policy

`run_inspect_command` must not execute through an interactive shell. It should:

- use an explicit allowed-command table,
- validate arguments per command,
- bound stdout/stderr size,
- apply timeouts where possible,
- reject pipes, redirection, semicolons, and other shell syntax,
- validate path arguments against the same boundary engine.

Likely early command candidates:

- `pwd`
- `ls`
- `find`
- `cat`
- `grep`
- `sed`
- `head`
- `tail`
- `stat` or a portability-safe equivalent if available
- selected compile/build inspection commands later, only after deliberate review.

Exact flags and command behavior must be validated per IRIX generation before exposure.

---

## 9. Configuration Model

### 9.1 Files

The project should carry forward the Galaxy concept of:

```text
projects.json   operator-editable registry
boundaries.json generated active policy
```

The final paths on IRIX are still to be chosen and validated against packaging conventions. Candidate design intent:

- config template shipped with the package,
- operator command validates and generates active policy,
- daemon reads active policy.

### 9.2 Registry Concepts

A project entry should eventually be able to express at least:

- display name,
- root path,
- `read_only` vs `read_write`,
- per-project deny overrides,
- enabled/disabled status if useful,
- possibly a default working directory or tool profile later.

### 9.3 Operator Workflow

The expected admin workflow should mirror Galaxy's explicit style:

```text
mcpserver add ...
mcpserver validate
mcpserver show
mcpserver preview
mcpserver apply
mcpserver restart
```

`apply` should update generated policy. It should not silently widen access or delete user data.

---

## 10. Build and Portability Strategy

### 10.1 Source Language

Use portable C as the long-term implementation language.

### 10.2 Coding Discipline

Code should favor:

- conservative C constructs,
- small modules,
- explicit memory ownership,
- minimal hidden dependencies,
- no assumed availability of modern POSIX conveniences until verified,
- clear compatibility notes when target-specific code is introduced.

### 10.3 Cross-Compilation

Cross-compiling from the Windows workstation to IRIX should be treated as a **research track**, not an assumption.

The project should pursue it if it materially improves iteration speed, but not depend on it for project viability. Early productivity can come from:

- host-native tests on the Windows workstation,
- emulator-based integration loops,
- real Octane2 compilation/testing when needed.

### 10.4 Target Build Matrix

Plan for multiple artifact tracks:

```text
IRIX 6.5.x build
IRIX 6.2 build
IRIX 5.3 build
```

Do not assume one ABI, compiler mode, or binary distribution fits all three. This should be tracked explicitly in a portability matrix once older docs and target systems are available.

---

## 11. Development and Test Strategy

### 11.1 Stage 1: Repository and Specification First

Before much implementation, create the repository with project-defining documents:

- `README.md`
- `AGENTS.md`
- `docs/PROJECT_PLAN.md`
- `docs/ARCHITECTURE.md`
- `docs/TOOL_CONTRACT.md`
- `docs/CONFIG_SCHEMA.md`
- `docs/SECURITY_MODEL.md`
- `docs/TEST_STRATEGY.md`
- `docs/PORTABILITY_MATRIX.md`
- `docs/OPEN_QUESTIONS.md`
- `docs/ROADMAP.md`

This prevents Codex from wandering or re-deciding core architecture every week.

### 11.2 Stage 2: Portable Host-Side Core

Build and test the easiest isolated pieces first:

- JSON request/response representation,
- MCP message dispatcher skeleton,
- config parser,
- boundary-policy evaluator,
- tool result structs,
- canned/golden MCP responses,
- path policy unit tests,
- deny/allow decision tests.

The point is to build confidence in logic before IRIX-specific service plumbing dominates the schedule.

### 11.3 Stage 3: Minimal MCP v0 on a Host Machine

Before the real IRIX daemon is complete, create a minimal host-executable server or test harness that can:

- accept an MCP initialization sequence,
- expose `tools/list`,
- handle a tiny set of fake or sandboxed tools,
- prove the tool contract and JSON-RPC framing are correct.

This is the fastest way to shake out protocol assumptions while working in VS Code.

### 11.4 Stage 4: IRIS Emulator Integration

Use IRIS as the first rapid IRIX integration loop where practical.

Candidate uses:

- copy/compile/run on emulated IRIX 6.5,
- boot testing,
- daemon lifecycle testing,
- config parsing on real IRIX libc/filesystem behavior,
- stdio bridge tests,
- early 5.3 compatibility experiments.

Because IRIS claims headless mode and port forwarding, it may later support semi-automated local test loops. This should be validated experimentally rather than assumed.

### 11.5 Stage 5: Real Octane2 Testing

The Octane2 remains the authoritative 6.5.30 acceptance target. Use it for:

- native build validation,
- daemon start/stop/restart behavior,
- `chkconfig` integration,
- logs and failure recovery,
- real filesystem behavior,
- performance observations,
- actual Codex/Claude remote stdio usage over SSH,
- packaging smoke tests.

The chicken-and-egg issue is manageable: the first interactions with the Octane2 can be manual SSH + file transfer + terminal testing until the MCP server exists.

### 11.6 Stage 6: Older Release Porting

After the 6.5 design is stable enough to avoid constant churn:

- bring up 6.2 build/testing,
- bring up 5.3 build/testing,
- adjust compatibility shims,
- document behavior differences,
- recruit community testers where the developer does not have hardware/images immediately available.

### 11.7 Stage 7: Packaging and Public Beta

Once the daemon/CLI/stdin bridge are coherent:

- define package layout,
- create per-target tardist packaging recipes,
- validate Software Manager install/uninstall behavior,
- create sample config templates,
- add basic man pages or help docs,
- publish a beta release for SGI hobbyists.

---

## 12. Testing Matrix

The project should maintain a living matrix like this:

| Area | Windows host | IRIS 6.5 | IRIS 5.3 | Real IRIX 6.5.30 | IRIX 6.2 target | Notes |
|---|---|---|---|---|---|---|
| Parser unit tests | yes | optional | optional | optional | optional | Host-first |
| Boundary-policy tests | yes | yes | yes | yes | later | Must be deterministic |
| MCP initialize/tools | yes | yes | yes | yes | later | Protocol correctness |
| Daemon lifecycle | no | yes | yes | yes | later | IRIX-specific |
| `chkconfig` integration | no | maybe | maybe | yes | later | Real OS behavior matters |
| Packaging/tardist install | no | maybe | maybe | yes | later | Final acceptance |
| Remote stdio over SSH | yes | yes | yes | yes | later | Core user workflow |

This table should be refined as concrete environments come online.

---

## 13. Proposed GitHub Repository Shape

```text
irix-mcpserver/
  README.md
  LICENSE
  AGENTS.md
  CONTRIBUTING.md
  SECURITY.md
  CHANGELOG.md

  docs/
    PROJECT_PLAN.md
    ARCHITECTURE.md
    TOOL_CONTRACT.md
    CONFIG_SCHEMA.md
    SECURITY_MODEL.md
    TEST_STRATEGY.md
    PORTABILITY_MATRIX.md
    OPEN_QUESTIONS.md
    ROADMAP.md

  src/
    core/
      protocol.c
      protocol.h
      json.c
      json.h
      policy.c
      policy.h
      tools_fs.c
      tools_fs.h
      tools_text.c
      tools_text.h
      tools_exec.c
      tools_exec.h

    daemon/
      mcpserverd.c
      ipc.c
      ipc.h

    cli/
      mcpserver.c
      stdio_bridge.c
      stdio_bridge.h

    compat/
      compat.h
      irix53.c
      irix62.c
      irix65.c

  tests/
    fixtures/
    unit/
    integration/
    protocol/

  packaging/
    irix53/
    irix62/
    irix65/

  examples/
    projects.json
    boundaries.json
```

The exact filenames will evolve, but this makes the separation of responsibilities explicit.

---

## 14. Codex Operating Instructions for This Project

A repo-local `AGENTS.md` should tell Codex the following.

### 14.1 Non-Negotiables

- Do not modify the Galaxy MCP server project.
- Treat this repository as the new source of truth for the IRIX MCP effort.
- Preserve the approved architecture unless the human explicitly revises it.
- Keep the daemon local-only; do not introduce public HTTP/OAuth work.
- Preserve consistent Galaxy-inspired tool semantics where practical.
- Prefer portability-first C over convenience shortcuts that make 5.3/6.2 impossible.
- Do not invent facts about IRIX quirks; record uncertainty in `docs/OPEN_QUESTIONS.md`.

### 14.2 Coding Style

- Small, auditable modules.
- Explicit ownership and error paths.
- Minimal dependencies.
- No clever portability-hostile language features.
- Keep test fixtures and protocol examples in version control.
- Update docs whenever behavior changes.

### 14.3 Workflow Expectations

Codex should generally:

1. read `AGENTS.md`, `docs/PROJECT_PLAN.md`, and `docs/ARCHITECTURE.md`,
2. propose a small scoped task,
3. implement only that task,
4. run or update tests when available,
5. summarize what changed,
6. flag assumptions and unresolved portability questions.

---

## 15. Major Workstreams

### Workstream A: Project Bootstrap

- Create GitHub account.
- Create public repository.
- Clone locally on the Windows workstation.
- Open in VS Code.
- Install/configure Codex extension or local Codex workflow.
- Commit this project plan and foundational docs.

### Workstream B: Tool Contract and Security Contract

- Freeze v1 tool list.
- Freeze v1 response schemas.
- Freeze v1 boundary model.
- Decide v1 default write-extension policy.
- Define command allowlist and argument policy.
- Define error semantics.

### Workstream C: Portable Core

- JSON representation strategy.
- JSON-RPC request/response primitives.
- MCP initialization sequence.
- tools list/call routing.
- config parsing.
- policy checks.
- unit tests and golden fixtures.

### Workstream D: Daemon + Bridge

- daemon process structure,
- local IPC abstraction,
- `mcpserver stdio` bridge,
- service lifecycle commands,
- log strategy.

### Workstream E: Filesystem Tools

- path existence/stat/list,
- bounded reads,
- tail/search/context,
- safe writes,
- directory create/delete/rename under policy.

### Workstream F: Command Inspection Tools

- allowed command table,
- per-command argument validation,
- safe subprocess execution,
- timeout/output clipping,
- IRIX release-specific command behavior notes.

### Workstream G: Emulator and Hardware Testing

- IRIS emulator setup notes,
- test harness over emulator networking,
- Octane2 manual integration loop,
- later 5.3/6.2 validation.

### Workstream H: Packaging

- installer layout,
- init/chkconfig integration,
- default disabled/enable-on-demand posture,
- tardist build recipes,
- install/uninstall validation.

### Workstream I: Community Readiness

- contribution guide,
- issue templates,
- testing report template,
- supported/unsupported matrix,
- beta release notes.

---

## 16. Suggested Milestones

### Milestone 0 — Project Initialized

- GitHub repo created.
- Core docs committed.
- Codex working in VS Code against the repo.

### Milestone 1 — Protocol and Policy Skeleton

- Docs define tool contract and config model.
- Portable C project skeleton exists.
- JSON-RPC/MCP initialization test fixtures exist.
- Basic unit tests run on the dev workstation.

### Milestone 2 — Minimal MCP Tool Server in Development Harness

- `tools/list` works.
- `tools/call` works for `ping` and at least one simple fake/sandboxed tool.
- Codex can connect to the development server locally.

### Milestone 3 — IRIX Daemon/Bridge Skeleton

- `mcpserverd` starts on IRIX 6.5.
- `mcpserver stdio` bridges to daemon.
- Basic `ping` and `path_exists` run in IRIX.

### Milestone 4 — Useful 6.5 Alpha

- Read/search tools usable.
- Safe writes usable within policy roots.
- Constrained inspection commands usable.
- Remote stdio over SSH from Windows works with Codex.
- Basic Octane2 acceptance achieved.

### Milestone 5 — Service Integration and Packaging Alpha

- `mcpserver start|stop|restart|status` coherent.
- `chkconfig`-style boot integration implemented and tested.
- Initial IRIX 6.5 tardist packaging created.

### Milestone 6 — 5.3 / 6.2 Portability Campaign

- 5.3 emulator target explored.
- 6.2 test environment identified.
- Build breaks documented/fixed.
- Compatibility matrix updated.

### Milestone 7 — Public Beta

- GitHub README polished.
- docs usable by community testers.
- first public release artifacts posted.
- issue intake and contributor path established.

---

## 17. Open Questions to Track Explicitly

These should go into `docs/OPEN_QUESTIONS.md` and be resolved over time.

### Protocol / Product

- Exact stable MCP protocol version and compatibility posture to advertise in v1.
- Whether v1 should implement only tools or also selected resources later.
- Whether `safe_json_preview` belongs in first release or second.

### IRIX Compatibility

- Which C compiler versions are realistic on 5.3, 6.2, 6.5.
- Exact libc/POSIX features guaranteed on each target.
- Safest portable path canonicalization strategy.
- Portable local IPC choice across all targets.
- Practical timeout/subprocess implementation strategy on each target.
- `chkconfig`/init-script details per target release.

### Testing

- Best way to automate IRIS boot/test cycles.
- Whether any emulator path exists for 6.2.
- Whether public CI can meaningfully run beyond host-side unit tests without proprietary IRIX assets.

### Packaging

- Exact tardist build toolchain and packaging conventions.
- Whether packages should default service enabled or disabled.
- Final install paths.
- Man page sections and packaging layout.

### Policy

- Default write-extension allowlist for a coding-oriented product.
- Default deny patterns for IRIX-sensitive files.
- Whether profiles remain only `readonly` and `full`, or whether a future `coding` profile is useful.

---

## 18. Recommended Immediate Next Steps

### Step 1: Create the GitHub Project

- Sign up for GitHub if needed.
- Create a public repository with a provisional name such as `irix-mcpserver`.
- Clone it to the Windows workstation.
- Open it in VS Code.

### Step 2: Seed the Repository with Planning Docs

Commit this document as:

```text
docs/PROJECT_PLAN.md
```

Then create the initial doc set listed above, even if several start as thin placeholders.

### Step 3: Add an `AGENTS.md` for Codex

Before coding starts, give Codex a stable rulebook for the repository.

### Step 4: Begin with Specification, Not Code

The first implementation task should not be "write daemon." It should be:

- finalize tool contract v1,
- finalize config/policy schema v1,
- define result object shapes,
- define protocol scope.

That gives Codex a clean target.

### Step 5: Then Build the Portable Core

Only after the contract is clear should implementation start with parser, policy, and protocol foundations.

---

## 19. Final Strategic Position

This project should be built as though it matters beyond one machine, because it does.

A clean, native, local-first MCP server for IRIX would give modern coding agents a safe way to work directly with vintage SGI systems. Done well, it could become a community tool for:

- preserving and extending IRIX software,
- building new utilities on old systems,
- accelerating porting/debugging,
- making exotic machines more approachable to new hobbyists,
- enabling richer collaboration around obsolete but still fascinating platforms.

The architecture is now clear enough to start the project properly. The next move is to create the repo, commit the plan, and let Codex work from a disciplined written brief instead of a pile of chat history.

---

## 20. Source Context Used to Produce This Plan

This document carries forward planning and lessons from:

- the existing Galaxy MCP server architecture/runbook/tasks/config files,
- the Galaxy `mcpserver` operator wrapper script,
- the approved IRIX daemon + stdio bridge direction,
- the decision to target portable C and eventual 5.3/6.2/6.5 builds,
- current OpenAI Codex docs for Windows, VS Code, and MCP support,
- current MCP specification guidance,
- current IRIS emulator project documentation.
