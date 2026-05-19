#!/sbin/sh
#
# setup-test-config.sh - create /etc/mcpserver config for initial testing
#
# Usage: /sbin/sh scripts/setup-test-config.sh <project-root>
# Example: /sbin/sh scripts/setup-test-config.sh /home/chris/src/mcpserver-irix
#
# Must be run as root. Creates /etc/mcpserver/ with a read-only boundary
# policy pointing at the given project root. Safe for first daemon test.

ROOT="$1"

if [ -z "$ROOT" ]; then
    echo "Usage: $0 <project-root>"
    echo "Example: $0 /home/chris/src/mcpserver-irix"
    exit 1
fi

if [ ! -d "$ROOT" ]; then
    echo "Error: $ROOT does not exist or is not a directory"
    exit 1
fi

mkdir -p /etc/mcpserver

# projects.json
cat > /etc/mcpserver/projects.json << EOF
{"version": 1, "projects": [{"name": "test", "root": "$ROOT", "mcp_access": "read_only", "deny_overrides": []}]}
EOF

# boundaries.json (read-only, safe for initial test)
cat > /etc/mcpserver/boundaries.json << EOF
{
  "version": 1,
  "generated_from": "/etc/mcpserver/projects.json",
  "generated_at": "2026-05-19T00:00:00",
  "read_write_roots": [],
  "read_only_roots": ["$ROOT"],
  "deny_overrides": [],
  "write_rules": {
    "allow_create_extensions": [],
    "allow_create_names": [],
    "allow_replace_extensions": [],
    "allow_replace_names": [],
    "deny_write_globs": []
  },
  "read_rules": {
    "deny_read_globs": ["**/.env", "**/*.secret", "**/*.key"]
  },
  "shell_rules": {
    "allowed_commands": ["pwd", "ls", "find", "cat", "grep", "head", "tail", "wc"],
    "allow_shell": false,
    "allow_pipes": false,
    "allow_redirects": false,
    "allow_glob_expansion": false
  }
}
EOF

echo "Created /etc/mcpserver/projects.json"
echo "Created /etc/mcpserver/boundaries.json"
echo "Root: $ROOT"
echo ""
echo "Next: ./mcpserverd &"
