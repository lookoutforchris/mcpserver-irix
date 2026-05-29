# Bug Report: IRIS Monitor — Inconsistent Line Endings Between Banner and Command Output

**Project:** IRIS (Inaccurate Rust Iris System Monitor)
**Component:** Monitor TCP interface (port 8888)
**Severity:** Minor — usability issue; workaround exists

---

## Summary

The monitor's welcome banner uses correct CRLF (`\r\n`) line endings throughout, but all command output uses bare LF (`\n`) only. This inconsistency makes the monitor unusable in strict VT100 terminals (PuTTY Raw, Windows Terminal, etc.) without enabling a non-default client-side workaround.

---

## Observed Behavior

Connecting via raw TCP to port 8888 and issuing any command produces misaligned output. Prompts appear at the wrong column — for example, after an error response of 38 characters followed by bare `\n`, the next `> ` prompt appears at column 38 rather than column 0:

```
> stuff
Unknown command. Type 'help' for list.
                                      > help
```

The 38 leading spaces before `>` are not sent by the server — they are empty terminal cells left because the cursor was never returned to column 0.

---

## Root Cause

In VT100, bare `\n` (LF, `0x0A`) advances the cursor **down one row** but does **not** return it to column 0. A carriage return (`\r`, `0x0D`) is required to reset the column. The banner was written with correct CRLF; command output was not.

Confirmed by packet capture:

| Section | CR | Bare LF | CRLF |
|---|---|---|---|
| Banner (1578 bytes) | 0 | 0 | 31 |
| `help` response (5303 bytes) | 0 | 87 | 0 |

No ANSI escape sequences are present in any output.

---

## Reproduction Steps

1. Connect to the IRIS monitor via raw TCP (not telnet protocol) — e.g. PuTTY with connection type set to Raw, host `localhost`, port `8888`
2. Observe the banner renders correctly
3. Type any command and press Enter
4. Observe the subsequent `> ` prompt is indented by the length of the response text

---

## Expected Behavior

All monitor output uses `\r\n` line endings consistently. This is the correct convention for any TCP server targeting terminal clients (RFC 854), and is what the banner already does.

---

## Workaround

Enable **"Implicit CR in every LF"** in the terminal client (PuTTY: Terminal settings). This is a non-default setting that most users would not know to enable.

---

## Suggested Fix

Standardize all monitor output on `\r\n`. The banner already does this correctly and serves as the reference. The command dispatch and response writer code paths should be updated to match.
