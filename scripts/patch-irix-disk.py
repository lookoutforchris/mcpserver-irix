"""
Patch /etc/init.d/mcpserverd in an IRIX 5.3 EFS raw disk image.

Replaces:  /usr/sbin/mcpserverd ;;
With:      /usr/sbin/mcpserverd&;;

Same byte length -- no inode or filesystem metadata changes needed.
The & backgrounds mcpserverd so the rc script does not block boot.

Usage:
    python patch-irix-disk.py [path-to-disk-image]

Default disk path: C:/dev/tools/iris/images/5.3/scsi1.raw
IRIS must be stopped before running this script.
"""

import sys
import os

DISK_DEFAULT = r"C:\dev\tools\iris\images\5.3\scsi1.raw"

FIND = b"/usr/sbin/mcpserverd ;;"
REPL = b"/usr/sbin/mcpserverd&;;"

assert len(FIND) == len(REPL), "patch lengths must match"

def main():
    disk_path = sys.argv[1] if len(sys.argv) > 1 else DISK_DEFAULT

    if not os.path.exists(disk_path):
        print(f"ERROR: disk image not found: {disk_path}")
        sys.exit(1)

    print(f"Disk: {disk_path}")
    print(f"Find: {FIND!r}")
    print(f"Repl: {REPL!r}")
    print()

    with open(disk_path, "r+b") as f:
        data = f.read()

    count = data.count(FIND)

    if count == 0:
        print("ERROR: pattern not found in disk image.")
        print("The init script may not have been written, or the content differs.")
        sys.exit(1)

    if count > 1:
        print(f"WARNING: pattern found {count} times. Showing all locations:")
        offset = 0
        for i in range(count):
            idx = data.index(FIND, offset)
            ctx_start = max(0, idx - 40)
            ctx_end = min(len(data), idx + len(FIND) + 40)
            print(f"  [{i}] offset {idx:#010x}  context: {data[ctx_start:ctx_end]!r}")
            offset = idx + 1
        print()
        choice = input(f"Patch all {count} occurrences? [y/N] ").strip().lower()
        if choice != 'y':
            print("Aborted.")
            sys.exit(0)
    else:
        idx = data.index(FIND)
        ctx_start = max(0, idx - 40)
        ctx_end = min(len(data), idx + len(FIND) + 40)
        print(f"Found at offset {idx:#010x}")
        print(f"Context: {data[ctx_start:ctx_end]!r}")
        print()
        choice = input("Patch this location? [y/N] ").strip().lower()
        if choice != 'y':
            print("Aborted.")
            sys.exit(0)

    with open(disk_path, "r+b") as f:
        offset = 0
        patches = 0
        while True:
            idx = data.find(FIND, offset)
            if idx == -1:
                break
            f.seek(idx)
            f.write(REPL)
            print(f"Patched offset {idx:#010x}")
            patches += 1
            offset = idx + 1

    print(f"\nDone. {patches} location(s) patched.")
    print("Start IRIS and IRIX should boot without hanging.")

if __name__ == "__main__":
    main()
