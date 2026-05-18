/*
 * mcpserver.c - operator CLI main
 *
 * Entry point for the mcpserver command. Dispatches to subcommands:
 *
 *   help, version, status, start, stop, restart,
 *   enable, disable, logs, validate, show, preview,
 *   apply, add, remove, stdio
 *
 * "mcpserver stdio" is the stdio bridge mode (see stdio_bridge.c).
 * All other subcommands manage the daemon and configuration.
 */

#include "../compat/compat.h"
#include "../core/policy.h"
#include "stdio_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>

#define BOUNDARIES_PATH  "/etc/mcpserver/boundaries.json"
#define PROJECTS_PATH    "/etc/mcpserver/projects.json"
#define PID_PATH         "/var/run/mcpserverd.pid"
#define INIT_SCRIPT      "/etc/init.d/mcpserverd"
#define CHKCONFIG_FLAG   "mcpserver"
#define CHKCONFIG_BIN    "/sbin/chkconfig"

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static void
usage(void)
{
    printf(
        "Usage: mcpserver <command> [args]\n"
        "\n"
        "Commands:\n"
        "  help              Show this message\n"
        "  version           Print version\n"
        "  status            Show daemon and chkconfig status\n"
        "  start             Start mcpserverd\n"
        "  stop              Stop mcpserverd\n"
        "  restart           Restart mcpserverd\n"
        "  enable            Enable at boot and start daemon\n"
        "  disable           Disable at boot and stop daemon\n"
        "  logs [N]          Show last N syslog lines (default 80)\n"
        "  add <name> <root> --rw|--ro [--deny <pat>...]\n"
        "                    Add a project to the registry\n"
        "  remove <name>     Remove a project from the registry\n"
        "  validate          Validate projects.json and generated policy\n"
        "  show              Print current project registry\n"
        "  preview           Print what boundaries.json would look like\n"
        "  apply             Write new boundaries.json from projects.json\n"
        "  stdio             Run MCP stdio bridge (for AI client connection)\n"
    );
}

static long
read_pid(void)
{
    FILE *f;
    long  pid = -1;

    f = fopen(PID_PATH, "r");
    if (!f) return -1;
    fscanf(f, "%ld", &pid);
    fclose(f);
    return pid;
}

/* ------------------------------------------------------------------ */
/* Subcommand stubs                                                     */
/* ------------------------------------------------------------------ */

static int
cmd_version(void)
{
    printf("mcpserver %s\n", MCPSERVER_VERSION);
    return 0;
}

static int
cmd_status(void)
{
    long pid = read_pid();
    int  enabled;

    /* check chkconfig state */
    enabled = (system(CHKCONFIG_BIN " " CHKCONFIG_FLAG " 2>/dev/null") == 0);

    printf("Boot:   %s\n", enabled ? "enabled" : "disabled");
    if (pid > 0)
        printf("Daemon: running (pid %ld)\n", pid);
    else
        printf("Daemon: stopped\n");
    return 0;
}

static int
cmd_start(void)
{
    /* TODO: check not already running, then exec mcpserverd */
    printf("Starting mcpserverd...\n");
    return system("/usr/sbin/mcpserverd");
}

static int
cmd_stop(void)
{
    long pid = read_pid();
    if (pid <= 0) {
        fprintf(stderr, "mcpserver: daemon is not running\n");
        return 1;
    }
    printf("Stopping mcpserverd (pid %ld)...\n", pid);
    if (kill((pid_t)pid, SIGTERM) < 0) {
        perror("mcpserver: kill");
        return 1;
    }
    return 0;
}

static int
cmd_restart(void)
{
    cmd_stop();
    /* brief pause to let daemon clean up socket */
    sleep(1);
    return cmd_start();
}

static int
cmd_enable(void)
{
    system(CHKCONFIG_BIN " -f " CHKCONFIG_FLAG " on");
    return cmd_start();
}

static int
cmd_disable(void)
{
    cmd_stop();
    system(CHKCONFIG_BIN " -f " CHKCONFIG_FLAG " off");
    return 0;
}

static int
cmd_add(int argc, char **argv)
{
    /* TODO: implement project registry add */
    (void)argc; (void)argv;
    fprintf(stderr, "mcpserver add: not yet implemented\n");
    return 1;
}

static int
cmd_remove(int argc, char **argv)
{
    /* TODO: implement project registry remove */
    (void)argc; (void)argv;
    fprintf(stderr, "mcpserver remove: not yet implemented\n");
    return 1;
}

static int
cmd_validate(void)
{
    struct policy p;
    printf("Validating %s ...\n", PROJECTS_PATH);
    /* TODO: validate projects.json structure, then generate and check policy */
    printf("Validating boundaries...\n");
    if (policy_load(BOUNDARIES_PATH, &p) != 0) {
        fprintf(stderr, "mcpserver validate: boundaries.json is invalid\n");
        return 1;
    }
    printf("OK\n");
    return 0;
}

static int
cmd_show(void)
{
    /* TODO: parse projects.json and print human-readable summary */
    fprintf(stderr, "mcpserver show: not yet implemented\n");
    return 1;
}

static int
cmd_preview(void)
{
    /* TODO: generate boundaries.json from projects.json and print to stdout */
    fprintf(stderr, "mcpserver preview: not yet implemented\n");
    return 1;
}

static int
cmd_apply(void)
{
    /* TODO: generate boundaries.json, backup old, write new, report */
    fprintf(stderr, "mcpserver apply: not yet implemented\n");
    return 1;
}

static int
cmd_logs(int n_lines)
{
    char cmd[128];
    /* On IRIX, /var/adm/SYSLOG is the typical syslog destination */
    snprintf(cmd, sizeof(cmd),
             "grep mcpserverd /var/adm/SYSLOG 2>/dev/null | tail -%d",
             n_lines);
    return system(cmd);
}

/* ------------------------------------------------------------------ */
/* Main                                                                 */
/* ------------------------------------------------------------------ */

int
main(int argc, char *argv[])
{
    const char *cmd;

    if (argc < 2) {
        usage();
        return 1;
    }

    cmd = argv[1];

    if (strcmp(cmd, "help")    == 0) { usage(); return 0; }
    if (strcmp(cmd, "version") == 0) return cmd_version();
    if (strcmp(cmd, "status")  == 0) return cmd_status();
    if (strcmp(cmd, "start")   == 0) return cmd_start();
    if (strcmp(cmd, "stop")    == 0) return cmd_stop();
    if (strcmp(cmd, "restart") == 0) return cmd_restart();
    if (strcmp(cmd, "enable")  == 0) return cmd_enable();
    if (strcmp(cmd, "disable") == 0) return cmd_disable();
    if (strcmp(cmd, "validate")== 0) return cmd_validate();
    if (strcmp(cmd, "show")    == 0) return cmd_show();
    if (strcmp(cmd, "preview") == 0) return cmd_preview();
    if (strcmp(cmd, "apply")   == 0) return cmd_apply();
    if (strcmp(cmd, "stdio")   == 0) return bridge_run();

    if (strcmp(cmd, "add")    == 0) return cmd_add(argc - 2, argv + 2);
    if (strcmp(cmd, "remove") == 0) return cmd_remove(argc - 2, argv + 2);

    if (strcmp(cmd, "logs") == 0) {
        int n = (argc >= 3) ? atoi(argv[2]) : 80;
        if (n <= 0) n = 80;
        return cmd_logs(n);
    }

    fprintf(stderr, "mcpserver: unknown command '%s'\n", cmd);
    fprintf(stderr, "Run 'mcpserver help' for usage.\n");
    return 1;
}
