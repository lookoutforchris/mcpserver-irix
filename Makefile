# ================================================================
# IRIX MCP Server - Makefile
#
# Targets:
#   make / make irix65    N32 MIPS-IV  (IRIX 6.5, R10000+, Octane2 default)
#   make irix62           N32 MIPS-III (IRIX 6.2 and MIPS-III 6.5 hardware)
#   make irix53           O32 MIPS-II  (IRIX 5.3, statically linked)
#   make verify-isa       Check ISA of built binaries with file(1)
#   make install          Copy binaries to /usr/sbin and /usr/bin
#   make clean            Remove binaries and build/ directory
#
# Run on the target IRIX system.
# Requires MIPSpro 7.4 for 6.x targets; IDO ucode cc for 5.3.
# POSIX make and GNU make (gmake) are both supported.
#
# MIPSpro compiler path on the Octane2 development system:
#   /opt/MIPSpro/bin/cc
# Override on the command line if installed elsewhere:
#   make CC=/usr/bin/cc irix65
#
# ISA note (see docs/PORTABILITY_MATRIX.md §9):
#   On this Octane2 (R10000), /usr/lib32/crt1.o -> mips4/crt1.o.
#   The irix65 target uses -mips4 (correct for R10000, no workaround needed).
#   The irix62 target uses -mips3 with explicit mips3/ startup objects to
#   guarantee true N32 MIPS-III output regardless of which ISA the build
#   host symlinks point to.
# ================================================================

CC       = /opt/MIPSpro/bin/cc
BUILDDIR = build
INCLUDES = -Isrc

# SGUG-RSE exports LIBRARY_PATH=/usr/sgug/lib32 (with a known typo as
# /usr/sgus/lib32 in their config). MIPSpro converts LIBRARY_PATH into -L
# flags and warns when the directory does not exist. unexport prevents make
# from passing this variable to compiler subprocesses at all.
unexport LIBRARY_PATH

# ---------------------------------------------------------------
# Source lists
# ---------------------------------------------------------------
COMPAT_SRCS = \
	src/compat/realpath.c \
	src/compat/fnmatch.c

CORE_SRCS = \
	src/core/json.c \
	src/core/policy.c \
	src/core/protocol.c \
	src/core/tools_fs.c \
	src/core/tools_text.c \
	src/core/tools_write.c \
	src/core/tools_exec.c

DAEMON_SRCS = \
	src/daemon/ipc.c \
	src/daemon/mcpserverd.c

# Note: DAEMON_SRCS kept for reference but DAEMON_ALL and CLI_ALL use
# IPC_SRCS and DAEMON_MAIN separately so ipc.c is shared with the CLI.

CLI_SRCS = \
	src/cli/stdio_bridge.c \
	src/cli/mcpserver.c

# ipc.c is shared: the stdio bridge (CLI) connects to the daemon socket
# using ipc_read_line / ipc_write_all, so ipc.c must be in both builds.
IPC_SRCS    = src/daemon/ipc.c
DAEMON_MAIN = src/daemon/mcpserverd.c

DAEMON_ALL = $(COMPAT_SRCS) $(CORE_SRCS) $(IPC_SRCS) $(DAEMON_MAIN)
CLI_ALL    = $(COMPAT_SRCS) $(CORE_SRCS) $(IPC_SRCS) $(CLI_SRCS)

# ---------------------------------------------------------------
# IRIX 6.5 (default): N32 MIPS-IV
#
# Correct for the Octane2 (R10000). /usr/lib32/crt1.o on this
# machine is already a MIPS-IV object, so no startup override needed.
#
# -D_SGI_SOURCE: enables POSIX and SGI extensions in ANSI mode.
# Required for: snprintf, sigaction, sigemptyset, WNOHANG, and other
# POSIX symbols that -ansi alone hides in MIPSpro's strict mode.
# ---------------------------------------------------------------
# -woff 1429: suppress "long long is nonstandard" from IRIX system headers.
# /usr/include/internal/stdlib_core.h uses long long internally; with -ansi
# -fullwarn MIPSpro warns about it even though it is not our code.
CFLAGS  = -n32 -mips4 -O2 -ansi -fullwarn -D_SGI_SOURCE -woff 1429
LDFLAGS = -n32

all: mcpserverd mcpserver

mcpserverd: $(DAEMON_ALL)
	$(CC) $(CFLAGS) $(INCLUDES) -o mcpserverd $(DAEMON_ALL) $(LDFLAGS)

mcpserver: $(CLI_ALL)
	$(CC) $(CFLAGS) $(INCLUDES) -o mcpserver $(CLI_ALL) $(LDFLAGS)

irix65: all

# ---------------------------------------------------------------
# IRIX 6.2 / MIPS-III compat: N32 MIPS-III
#
# On R10000 build hosts, /usr/lib32/crt1.o -> mips4/crt1.o.
# The MIPSpro 7.4 driver does not override startup object selection
# based on -mips3 when the system symlinks resolve to MIPS-IV.
#
# Fix: compile all sources to object files, then link with explicit
# /usr/lib32/mips3/crt1.o and /usr/lib32/mips3/crtn.o via -nostartfiles.
# This produces a verified N32 MIPS-III ELF regardless of build host ISA.
#
# See docs/PORTABILITY_MATRIX.md §9 for full diagnosis and evidence.
# ---------------------------------------------------------------
LIB32        = /usr/lib32
CRT1_MIPS3   = $(LIB32)/mips3/crt1.o
CRTN_MIPS3   = $(LIB32)/mips3/crtn.o
CFLAGS_MIPS3 = -n32 -mips3 -O2 -ansi -fullwarn -D_SGI_SOURCE -woff 1429

irix62: irix62-check
	@mkdir -p $(BUILDDIR)/mips3/d $(BUILDDIR)/mips3/c
	@echo "=== Compiling daemon sources (N32 MIPS-III) ==="
	@for src in $(DAEMON_ALL); do \
		obj="$(BUILDDIR)/mips3/d/`basename $$src .c`.o"; \
		echo "  cc $$src"; \
		$(CC) $(CFLAGS_MIPS3) $(INCLUDES) -c $$src -o $$obj || exit 1; \
	done
	@echo "=== Linking mcpserverd (N32 MIPS-III, explicit startup objects) ==="
	$(CC) -n32 -mips3 -nostartfiles \
		$(CRT1_MIPS3) \
		$(BUILDDIR)/mips3/d/*.o \
		$(CRTN_MIPS3) \
		-lc -o mcpserverd
	@echo "=== Compiling CLI sources (N32 MIPS-III) ==="
	@for src in $(CLI_ALL); do \
		obj="$(BUILDDIR)/mips3/c/`basename $$src .c`.o"; \
		echo "  cc $$src"; \
		$(CC) $(CFLAGS_MIPS3) $(INCLUDES) -c $$src -o $$obj || exit 1; \
	done
	@echo "=== Linking mcpserver (N32 MIPS-III, explicit startup objects) ==="
	$(CC) -n32 -mips3 -nostartfiles \
		$(CRT1_MIPS3) \
		$(BUILDDIR)/mips3/c/*.o \
		$(CRTN_MIPS3) \
		-lc -o mcpserver
	@echo "=== irix62 build complete. Run: make verify-isa ==="

irix62-check:
	@if [ ! -f $(CRT1_MIPS3) ]; then \
		echo "ERROR: $(CRT1_MIPS3) not found."; \
		echo "       MIPS-III startup objects are required for the irix62 target."; \
		echo "       On this system they should be at $(LIB32)/mips3/."; \
		exit 1; \
	fi
	@if [ ! -f $(CRTN_MIPS3) ]; then \
		echo "ERROR: $(CRTN_MIPS3) not found."; \
		exit 1; \
	fi

# ---------------------------------------------------------------
# IRIX 5.3: O32 MIPS-II, statically linked
#
# Build on an IRIX 5.3 system with IDO, or cross-compile on 6.x
# with -o32. -ansi is required (ucode cc defaults to K&R C).
# Static linking avoids libc version dependency across IRIX releases.
# ---------------------------------------------------------------
irix53:
	$(CC) -o32 -mips2 -O2 -ansi -fullwarn -D_SGI_SOURCE -static \
		$(INCLUDES) -o mcpserverd $(DAEMON_ALL)
	$(CC) -o32 -mips2 -O2 -ansi -fullwarn -D_SGI_SOURCE -static \
		$(INCLUDES) -o mcpserver $(CLI_ALL)

# ---------------------------------------------------------------
# ISA verification
#
# Run after any build to confirm the ELF ISA matches the target.
# Expected output:
#   irix65: ELF 32-bit MSB executable, MIPS, N32 MIPS-IV version 1
#   irix62: ELF 32-bit MSB executable, MIPS, N32 MIPS-III version 1
#   irix53: ELF 32-bit MSB executable, MIPS, version 1  (O32 MIPS-II)
# ---------------------------------------------------------------
verify-isa:
	@echo "=== ISA check ==="
	@file mcpserverd mcpserver 2>/dev/null || echo "  (binaries not found - run make first)"
	@echo ""
	@echo "Expected ISA by target:"
	@echo "  irix65: N32 MIPS-IV  (R10000 Octane2 native)"
	@echo "  irix62: N32 MIPS-III (MIPS-III hardware compat)"
	@echo "  irix53: MIPS-II O32  (IRIX 5.3 static)"

# ---------------------------------------------------------------
# Install
# ---------------------------------------------------------------
install: all
	cp mcpserverd /usr/sbin/mcpserverd
	cp mcpserver  /usr/bin/mcpserver
	chmod 755 /usr/sbin/mcpserverd /usr/bin/mcpserver

# ---------------------------------------------------------------
# Clean
# ---------------------------------------------------------------
clean:
	rm -f mcpserverd mcpserver
	rm -f *.o
	rm -rf $(BUILDDIR)

.PHONY: all irix65 irix62 irix62-check irix53 verify-isa install clean
