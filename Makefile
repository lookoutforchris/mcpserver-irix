# ================================================================
# IRIX MCP Server - Makefile
#
# Targets:
#   make              Build for IRIX 6.5 (N32, default)
#   make irix53       Build for IRIX 5.3 (O32, static)
#   make irix62       Build for IRIX 6.2 (N32)
#   make clean        Remove built binaries
#
# Run on the target IRIX system. Requires MIPSpro (6.x) or IDO (5.3).
# GNU make and POSIX make are both supported.
# ================================================================

CC      = cc
CFLAGS  = -n32 -mips3 -O2 -ansi -fullwarn
LDFLAGS = -n32
INCLUDES = -Isrc

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

CLI_SRCS = \
	src/cli/stdio_bridge.c \
	src/cli/mcpserver.c

ALL_SRCS = $(COMPAT_SRCS) $(CORE_SRCS)

all: mcpserverd mcpserver

mcpserverd: $(ALL_SRCS) $(DAEMON_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) -o mcpserverd \
		$(ALL_SRCS) $(DAEMON_SRCS) $(LDFLAGS)

mcpserver: $(ALL_SRCS) $(CLI_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) -o mcpserver \
		$(ALL_SRCS) $(CLI_SRCS) $(LDFLAGS)

# ----------------------------------------------------------------
# IRIX 5.3: O32 ABI, MIPS2, static linking for libc portability.
# Requires -ansi (ucode compiler defaults to K&R C).
# Build on a 5.3 system (IDO) or on 6.x with -o32.
# ----------------------------------------------------------------
irix53:
	$(MAKE) CC=cc \
		CFLAGS="-o32 -mips2 -O2 -ansi -fullwarn -static" \
		LDFLAGS="-o32 -static" \
		INCLUDES="-Isrc" \
		all

# ----------------------------------------------------------------
# IRIX 6.2: N32 ABI, MIPS3 (same as 6.5 default).
# ----------------------------------------------------------------
irix62:
	$(MAKE) CC=cc \
		CFLAGS="-n32 -mips3 -O2 -ansi -fullwarn" \
		LDFLAGS="-n32" \
		INCLUDES="-Isrc" \
		all

# ----------------------------------------------------------------
# IRIX 6.5: N32 ABI, MIPS3 (this is the default).
# ----------------------------------------------------------------
irix65:
	$(MAKE) all

clean:
	rm -f mcpserverd mcpserver

install:
	cp mcpserverd /usr/sbin/mcpserverd
	cp mcpserver /usr/bin/mcpserver
	chmod 755 /usr/sbin/mcpserverd
	chmod 755 /usr/bin/mcpserver

.PHONY: all irix53 irix62 irix65 clean install
