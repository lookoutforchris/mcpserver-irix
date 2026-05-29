---
name: iris-transfer
description: Transfer files between Windows and the IRIS emulator (IRIX 5.3) via the scratch disk. Use whenever a file needs to move to or from IRIS — source code, packaging files, tardists. Specify direction ("to-iris" or "from-iris") and the files. TFTP and NFS exist but are unreliable in practice; scratch disk is the only consistent method.
---

# IRIS Scratch Disk Transfer

The IRIX 5.3 emulator (IRIS) has no reliable host↔guest network file transfer. NFS is broken (IRIS bug, partial workaround via nfs-proxy.py), TFTP works intermittently, and small MCP `replace_text_file` writes are limited to ~15 KB. The **scratch disk** is the only consistent method for files of any size.

Key facts:
- Scratch image: `C:\dev\tools\iris\images\5.3\scratch.raw`
- Payload starts at byte 4096 = sector 8
- IRIS does not hold an exclusive lock — Windows can write while IRIS is running
- From IRIX: `/dev/rdsk/dks0d2vol` = whole volume (including SGI Volume Header); `skip=8` skips the VH
- Write direction is asymmetric — see below

## Direction 1: Windows → IRIS

```
# Windows (any shell):
cd c:/dev/projects/mcpserver-irix
tar cf /tmp/transfer.tar <file1> <file2> ...
dd if=/tmp/transfer.tar of=C:/dev/tools/iris/images/5.3/scratch.raw bs=512 seek=8 conv=notrunc

# Note the record count output by dd. Add ~10 for safety margin.
```

Then tell the user to run on IRIS (PuTTY serial console, port 8881):
```
cd /usr/people/shared/projects/mcpserver-irix   # or wherever extracting
dd if=/dev/rdsk/dks0d2vol bs=512 skip=8 count=<N> | tar xf -
```

Where `<N>` = the records dd reported on Windows + 10 padding. Over-counting is harmless (tar stops at EOF); under-counting truncates.

Pack tar paths relative to the destination project root so they extract in place.

## Direction 2: IRIS → Windows

Tell the user to run on IRIS:
```
cd /usr/people/shared/projects/mcpserver-irix
tar cf - <file1> <file2> ... | dd of=/dev/rdsk/dks0d2s0 bs=512
```

**Note the different device path on the write side:** `/dev/rdsk/dks0d2s0` is partition 0 (skips the SGI Volume Header automatically). Writing to `/dev/rdsk/dks0d2vol` would clobber the VH.

Have them report the record count from dd. Then on Windows:
```
cd c:/dev/projects/mcpserver-irix   # or destination
dd if=C:/dev/tools/iris/images/5.3/scratch.raw bs=512 skip=8 count=<records+5> | tar xf -
```

## When to use what

| Method | Size limit | Reliability | When to use |
|---|---|---|---|
| MCP `replace_text_file` / `create_text_file` | ~15 KB | Reliable | Single small file edits (compat.h, spec files) |
| Scratch disk | None | Reliable | Anything larger, bulk transfers, packaging files |
| TFTP | None | Unreliable in practice | Avoid |
| NFS | None | Partial (first ls works) | Avoid for transfers |

## Common pitfalls

| Problem | Fix |
|---|---|
| `tar: short read` on IRIX extract | `count=N` too small; recalculate as ceil(bytes/512) + 10 |
| Files extracted at wrong path | tar was packed with absolute or wrong-relative paths; use `tar cf - -C <root> <files>` or cd first |
| Scratch disk appears corrupted | The SGI VH must not be overwritten; always use `seek=8` writing from Windows, `skip=8` reading |
| IRIS doesn't see the new data | Probably wrote to the wrong offset; verify `seek=8` |
| Read count wrong on Windows | dd output uses 512-byte records — use that number, not file size |
