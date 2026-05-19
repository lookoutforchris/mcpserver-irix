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
#include "../core/json.h"
#include "../core/policy.h"
#include "stdio_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#define BOUNDARIES_PATH  "/etc/mcpserver/boundaries.json"
#define PROJECTS_PATH    "/etc/mcpserver/projects.json"
#define PID_PATH         "/var/run/mcpserverd.pid"
#define INIT_SCRIPT      "/etc/init.d/mcpserverd"
#define CHKCONFIG_FLAG   "mcpserver"
#define CHKCONFIG_BIN    "/sbin/chkconfig"
#define BACKUP_DIR       "/etc/mcpserver/backup"

#define PROJECTS_MAX     16
#define PROJECT_NAME_MAX 64
#define PROJECT_DENY_MAX 16
#define PROJECT_DENY_LEN 256
#define BOUNDARIES_BUFSZ 32768

struct project_entry {
    char name[PROJECT_NAME_MAX];
    char root[MCPSERVER_PATH_MAX];
    int  rw;
    char deny[PROJECT_DENY_MAX][PROJECT_DENY_LEN];
    int  deny_count;
};

struct project_list {
    struct project_entry entries[PROJECTS_MAX];
    int                  count;
};

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
/* Project registry helpers                                             */
/* ------------------------------------------------------------------ */

static void
bcat(char *buf, int *pos, int bufsz, const char *str)
{
    int len = (int)strlen(str);
    if (len == 0) return;
    if (*pos + len < bufsz) {
        memcpy(buf + *pos, str, (size_t)len);
        *pos += len;
        buf[*pos] = '\0';
    }
}

static int
file_copy(const char *src, const char *dst)
{
    FILE  *fsrc;
    FILE  *fdst;
    char   buf[4096];
    size_t nr;
    int    ok;

    fsrc = fopen(src, "r");
    if (!fsrc) return -1;
    fdst = fopen(dst, "w");
    if (!fdst) { fclose(fsrc); return -1; }

    ok = 1;
    while ((nr = fread(buf, 1, sizeof(buf), fsrc)) > 0) {
        if (fwrite(buf, 1, nr, fdst) != nr) { ok = 0; break; }
    }
    fclose(fsrc);
    fclose(fdst);
    return ok ? 0 : -1;
}

static int
projects_load(struct project_list *pl)
{
    FILE *f;
    char  buf[65536];
    char  arr[65536];
    char  item[4096];
    char  access[16];
    int   n;
    int   i;
    struct project_entry *e;

    memset(pl, 0, sizeof(*pl));

    f = fopen(PROJECTS_PATH, "r");
    if (!f) {
        fprintf(stderr, "mcpserver: cannot open %s\n", PROJECTS_PATH);
        return -1;
    }
    n = (int)fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n <= 0) return -1;
    buf[n] = '\0';

    if (json_get_object(buf, "projects", arr, sizeof(arr)) != 0) {
        fprintf(stderr, "mcpserver: no 'projects' array in %s\n", PROJECTS_PATH);
        return -1;
    }

    for (i = 0; i < PROJECTS_MAX; i++) {
        if (json_array_get_item(arr, i, item, sizeof(item)) != 0) break;

        e = &pl->entries[pl->count];
        json_get_string(item, "name", e->name, PROJECT_NAME_MAX);
        json_get_string(item, "root", e->root, MCPSERVER_PATH_MAX);

        access[0] = '\0';
        json_get_string(item, "mcp_access", access, sizeof(access));
        e->rw = (strcmp(access, "read_write") == 0);

        e->deny_count = json_get_string_array(item, "deny_overrides",
                            (char *)e->deny, PROJECT_DENY_LEN, PROJECT_DENY_MAX);
        if (e->deny_count < 0) e->deny_count = 0;

        pl->count++;
    }
    return 0;
}

static int
projects_write(const struct project_list *pl)
{
    FILE                       *f;
    const struct project_entry *e;
    char                        esc[MCPSERVER_PATH_MAX * 2 + 4];
    int                         i;
    int                         j;

    f = fopen(PROJECTS_PATH, "w");
    if (!f) {
        fprintf(stderr, "mcpserver: cannot write %s: ", PROJECTS_PATH);
        perror(NULL);
        return -1;
    }

    fprintf(f, "{\"version\":1,\"projects\":[");
    for (i = 0; i < pl->count; i++) {
        e = &pl->entries[i];
        if (i > 0) fprintf(f, ",");
        json_escape(e->name, esc, sizeof(esc));
        fprintf(f, "{\"name\":\"%s\"", esc);
        json_escape(e->root, esc, sizeof(esc));
        fprintf(f, ",\"root\":\"%s\"", esc);
        fprintf(f, ",\"mcp_access\":\"%s\"",
                e->rw ? "read_write" : "read_only");
        fprintf(f, ",\"deny_overrides\":[");
        for (j = 0; j < e->deny_count; j++) {
            if (j > 0) fprintf(f, ",");
            json_escape(e->deny[j], esc, sizeof(esc));
            fprintf(f, "\"%s\"", esc);
        }
        fprintf(f, "]}");
    }
    fprintf(f, "]}\n");
    fclose(f);
    return 0;
}

static int
boundaries_generate(const struct project_list *pl, char *out, int outsz)
{
    char       tmp[64];
    char       esc[MCPSERVER_PATH_MAX * 2 + 4];
    time_t     now;
    struct tm *tm_p;
    int        n;
    int        i;
    int        j;
    int        first;

    n = 0;
    time(&now);
    tm_p = localtime(&now);
    strftime(tmp, sizeof(tmp), "%Y-%m-%dT%H:%M:%S", tm_p);

    bcat(out, &n, outsz, "{\n  \"version\": 1,\n  \"generated_from\": \"");
    bcat(out, &n, outsz, PROJECTS_PATH);
    bcat(out, &n, outsz, "\",\n  \"generated_at\": \"");
    bcat(out, &n, outsz, tmp);
    bcat(out, &n, outsz, "\",\n");

    bcat(out, &n, outsz, "  \"read_write_roots\": [");
    first = 1;
    for (i = 0; i < pl->count; i++) {
        if (!pl->entries[i].rw) continue;
        if (!first) bcat(out, &n, outsz, ", ");
        json_escape(pl->entries[i].root, esc, sizeof(esc));
        bcat(out, &n, outsz, "\""); bcat(out, &n, outsz, esc);
        bcat(out, &n, outsz, "\"");
        first = 0;
    }
    bcat(out, &n, outsz, "],\n");

    bcat(out, &n, outsz, "  \"read_only_roots\": [");
    first = 1;
    for (i = 0; i < pl->count; i++) {
        if (pl->entries[i].rw) continue;
        if (!first) bcat(out, &n, outsz, ", ");
        json_escape(pl->entries[i].root, esc, sizeof(esc));
        bcat(out, &n, outsz, "\""); bcat(out, &n, outsz, esc);
        bcat(out, &n, outsz, "\"");
        first = 0;
    }
    bcat(out, &n, outsz, "],\n");

    bcat(out, &n, outsz, "  \"deny_overrides\": [");
    first = 1;
    for (i = 0; i < pl->count; i++) {
        for (j = 0; j < pl->entries[i].deny_count; j++) {
            if (!first) bcat(out, &n, outsz, ", ");
            json_escape(pl->entries[i].deny[j], esc, sizeof(esc));
            bcat(out, &n, outsz, "\""); bcat(out, &n, outsz, esc);
            bcat(out, &n, outsz, "\"");
            first = 0;
        }
    }
    bcat(out, &n, outsz, "],\n");

    bcat(out, &n, outsz,
        "  \"write_rules\": {\n"
        "    \"allow_create_extensions\": "
        "[\".md\",\".txt\",\".json\",\".c\",\".h\",\".cc\",\".cpp\","
        "\".s\",\".S\",\".sh\",\".mk\"],\n"
        "    \"allow_create_names\": "
        "[\"Makefile\",\"Imakefile\",\"GNUmakefile\"],\n"
        "    \"allow_replace_extensions\": "
        "[\".md\",\".txt\",\".json\",\".c\",\".h\",\".cc\",\".cpp\","
        "\".s\",\".S\",\".sh\",\".mk\"],\n"
        "    \"allow_replace_names\": "
        "[\"Makefile\",\"Imakefile\",\"GNUmakefile\"],\n"
        "    \"deny_write_globs\": []\n"
        "  },\n");

    bcat(out, &n, outsz,
        "  \"read_rules\": {\n"
        "    \"deny_read_globs\": []\n"
        "  },\n");

    bcat(out, &n, outsz,
        "  \"shell_rules\": {\n"
        "    \"allowed_commands\": "
        "[\"pwd\",\"ls\",\"find\",\"cat\",\"grep\","
        "\"head\",\"tail\",\"sed\",\"wc\",\"stat\"],\n"
        "    \"allow_shell\": false,\n"
        "    \"allow_pipes\": false,\n"
        "    \"allow_redirects\": false,\n"
        "    \"allow_glob_expansion\": false\n"
        "  }\n"
        "}\n");

    return (n > 0 && n < outsz - 1) ? 0 : -1;
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
    struct project_list  pl;
    struct project_entry *e;
    const char           *name;
    const char           *root;
    int                   rw;
    int                   i;

    if (argc < 3) {
        fprintf(stderr,
            "Usage: mcpserver add <name> <root> --rw|--ro [--deny <pat>...]\n");
        return 1;
    }

    name = argv[0];
    root = argv[1];

    if (strcmp(argv[2], "--rw") == 0) {
        rw = 1;
    } else if (strcmp(argv[2], "--ro") == 0) {
        rw = 0;
    } else {
        fprintf(stderr, "mcpserver add: expected --rw or --ro, got %s\n",
                argv[2]);
        return 1;
    }

    if (projects_load(&pl) != 0)
        memset(&pl, 0, sizeof(pl));

    for (i = 0; i < pl.count; i++) {
        if (strcmp(pl.entries[i].name, name) == 0) {
            fprintf(stderr, "mcpserver add: project '%s' already exists\n",
                    name);
            return 1;
        }
    }

    if (pl.count >= PROJECTS_MAX) {
        fprintf(stderr, "mcpserver add: project list full (%d max)\n",
                PROJECTS_MAX);
        return 1;
    }

    e = &pl.entries[pl.count];
    strncpy(e->name, name, PROJECT_NAME_MAX - 1);
    e->name[PROJECT_NAME_MAX - 1] = '\0';
    strncpy(e->root, root, MCPSERVER_PATH_MAX - 1);
    e->root[MCPSERVER_PATH_MAX - 1] = '\0';
    e->rw = rw;
    e->deny_count = 0;

    for (i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--deny") == 0 && i + 1 < argc) {
            if (e->deny_count < PROJECT_DENY_MAX) {
                strncpy(e->deny[e->deny_count], argv[i + 1],
                        PROJECT_DENY_LEN - 1);
                e->deny[e->deny_count][PROJECT_DENY_LEN - 1] = '\0';
                e->deny_count++;
            }
            i++;
        }
    }

    pl.count++;

    if (projects_write(&pl) != 0) return 1;

    printf("Added project '%s' (%s) at %s\n",
           name, rw ? "read-write" : "read-only", root);
    return 0;
}

static int
cmd_remove(int argc, char **argv)
{
    struct project_list out;
    struct project_list pl;
    const char         *name;
    int                 found;
    int                 i;

    if (argc < 1) {
        fprintf(stderr, "Usage: mcpserver remove <name>\n");
        return 1;
    }

    name = argv[0];

    if (projects_load(&pl) != 0) return 1;

    memset(&out, 0, sizeof(out));
    found = 0;
    for (i = 0; i < pl.count; i++) {
        if (strcmp(pl.entries[i].name, name) == 0) {
            found = 1;
            continue;
        }
        out.entries[out.count++] = pl.entries[i];
    }

    if (!found) {
        fprintf(stderr, "mcpserver remove: project '%s' not found\n", name);
        return 1;
    }

    if (projects_write(&out) != 0) return 1;

    printf("Removed project '%s'\n", name);
    return 0;
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
    struct project_list         pl;
    const struct project_entry *e;
    int                         i;
    int                         j;

    if (projects_load(&pl) != 0) return 1;

    if (pl.count == 0) {
        printf("No projects configured.\n");
        return 0;
    }

    for (i = 0; i < pl.count; i++) {
        e = &pl.entries[i];
        printf("%-20s  %-10s  %s\n",
               e->name,
               e->rw ? "read-write" : "read-only",
               e->root);
        for (j = 0; j < e->deny_count; j++)
            printf("  deny: %s\n", e->deny[j]);
    }
    return 0;
}

static int
cmd_preview(void)
{
    struct project_list pl;
    static char         buf[BOUNDARIES_BUFSZ];

    if (projects_load(&pl) != 0) return 1;

    if (boundaries_generate(&pl, buf, sizeof(buf)) != 0) {
        fprintf(stderr, "mcpserver preview: generation failed\n");
        return 1;
    }
    printf("%s", buf);
    return 0;
}

static int
cmd_apply(void)
{
    struct project_list pl;
    static char         newbuf[BOUNDARIES_BUFSZ];
    char                bakpath[MCPSERVER_PATH_MAX];
    time_t              now;
    struct tm          *tm_p;
    long                pid;
    FILE               *f;

    if (projects_load(&pl) != 0) return 1;

    if (boundaries_generate(&pl, newbuf, sizeof(newbuf)) != 0) {
        fprintf(stderr, "mcpserver apply: generation failed\n");
        return 1;
    }

    mkdir(BACKUP_DIR, 0755);

    time(&now);
    tm_p = localtime(&now);
    snprintf(bakpath, sizeof(bakpath),
             "%s/boundaries.json.%04d%02d%02d-%02d%02d%02d",
             BACKUP_DIR,
             tm_p->tm_year + 1900, tm_p->tm_mon + 1, tm_p->tm_mday,
             tm_p->tm_hour, tm_p->tm_min, tm_p->tm_sec);
    file_copy(BOUNDARIES_PATH, bakpath);

    f = fopen(BOUNDARIES_PATH, "w");
    if (!f) {
        fprintf(stderr, "mcpserver apply: cannot write %s: ", BOUNDARIES_PATH);
        perror(NULL);
        return 1;
    }
    fputs(newbuf, f);
    fclose(f);

    printf("Wrote %s\n", BOUNDARIES_PATH);
    printf("Backed up previous to %s\n", bakpath);

    pid = read_pid();
    if (pid > 0) {
        if (kill((pid_t)pid, SIGHUP) == 0)
            printf("Sent SIGHUP to daemon (pid %ld)\n", pid);
        else
            fprintf(stderr, "mcpserver apply: warning: could not signal daemon\n");
    } else {
        printf("Daemon not running; policy will load at next start.\n");
    }

    return 0;
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
