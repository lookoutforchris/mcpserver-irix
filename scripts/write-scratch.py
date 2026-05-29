import sys, os

SCRATCH = r"C:\dev\tools\iris\images\5.3\scratch.raw"
OFFSET  = 4096   # skip SGI Volume Header (8 x 512-byte sectors)

src = sys.argv[1] if len(sys.argv) > 1 else None
if not src:
    print("Usage: write-scratch.py <file>")
    sys.exit(1)

src_size = os.path.getsize(src)
print("Source: %s (%d bytes, %.1f MB)" % (src, src_size, src_size / (1024*1024)))

if src_size + OFFSET > os.path.getsize(SCRATCH):
    print("ERROR: source too large for scratch disk")
    sys.exit(1)

try:
    with open(SCRATCH, "r+b") as dst:
        dst.seek(OFFSET)
        with open(src, "rb") as f:
            written = 0
            while True:
                chunk = f.read(1024 * 1024)
                if not chunk:
                    break
                dst.write(chunk)
                written += len(chunk)
                print("  wrote %d / %d bytes (%.0f%%)" % (written, src_size, 100*written/src_size))
        dst.flush()
    print("Done. Written %d bytes at offset %d." % (written, OFFSET))
    # dks0d2s0 = partition slot 0 (PT_RAW, first_block=8) — starts at sector 8, no skip needed
    # dks0d2vol = partition slot 10 (PT_VOLUME, first_block=0) — starts at sector 0, needs skip=8
    print("From IRIX (use one of):")
    print("  dd if=/dev/rdsk/dks0d2s0 bs=64k | tar xf -")
    print("  dd if=/dev/rdsk/dks0d2vol bs=512 skip=8 count=%d | tar xf -" % ((src_size + 511) // 512))
except PermissionError as e:
    print("LOCKED: %s" % e)
    print("IRIS has the file locked — shut down IRIX first.")
    sys.exit(2)
