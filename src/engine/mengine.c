/*------------------------------------------------------------------------------
 * mengine.c: multi-process engine for CORS (cors-mengine supervisor / worker)
 *
 * author  : cors contributors
 * version : 1.0
 * history : 2026/06/13 1.0  new
 *-----------------------------------------------------------------------------*/
#include "mcors.h"
#include "vt.h"
#include "options.h"

#include <string.h>
#include <signal.h>

#ifndef WIN32
#include <unistd.h>
#endif

#define CMDPROMPT "cors-mengine> "
#define MAXCMD    256
#define MAXSTR    1024
#define TRACEFILE "cors_mengine_%Y%m%d%h%M.trace"

typedef struct {
    int state;
    vt_t *vt;
    uv_thread_t thread;
    mcors_t *mcors;
} mcon_t;

static mcors_t g_mcors;
static cors_opt_t g_cors_opt;
static int intflg = 0;

static void sigshut(int sig)
{
    (void)sig;
    intflg = 1;
}

static void mcon_stop(mcors_t *mcors, vt_t *vt)
{
    if (!mcors->state) {
        vt_printf(vt, "cors-mengine not running\n");
        return;
    }
    mcors_close(mcors);
    vt_printf(vt, "cors-mengine stopped\n");
}

static void cmd_shutdown(char **args, int narg, vt_t *vt, mcors_t *mcors)
{
    (void)args;
    (void)narg;
    vt_printf(vt, "cors-mengine shutdown ...\n");
    mcon_stop(mcors, vt);
    intflg = 1;
}

static void cmd_workers(char **args, int narg, vt_t *vt, mcors_t *mcors)
{
    int i;
    (void)args;
    (void)narg;
    vt_printf(vt, "workers: %d\n", mcors->worker_count);
    for (i = 0; i < mcors->worker_count; i++) {
        vt_printf(vt, "  [%d] state=%d partition=%s\n",
                  i, mcors->workers[i].state, mcors->workers[i].partition_file);
    }
}

static void mcon_thread(void *arg)
{
    mcon_t *con = (mcon_t *)arg;
    char buff[MAXCMD], *args[MAXCMD], *p;
    int i, narg;

    while (con->state && !intflg) {
        vt_printf(con->vt, CMDPROMPT);
        if (!vt_gets(con->vt, buff, sizeof(buff))) break;

        narg = 0;
        for (p = buff; *p && narg < MAXCMD; p++) {
            if (*p == ' ' || *p == '\t') *p = '\0';
            else if (p == buff || *(p - 1) == '\0') args[narg++] = p;
        }
        if (narg == 0) continue;

        if (!strcmp(args[0], "shutdown")) cmd_shutdown(args, narg, con->vt, con->mcors);
        else if (!strcmp(args[0], "workers")) cmd_workers(args, narg, con->vt, con->mcors);
        else vt_printf(con->vt, "unknown command: %s (try: workers, shutdown)\n", args[0]);
    }
    intflg = 1;
}

static mcon_t *mcon_open(const char *dev, mcors_t *mcors)
{
    mcon_t *con = calloc(1, sizeof(*con));
    if (!con) return NULL;
    con->vt = vt_open(dev);
    if (!con->vt) {
        free(con);
        return NULL;
    }
    con->mcors = mcors;
    con->state = 1;
    if (uv_thread_create(&con->thread, mcon_thread, con)) {
        vt_close(con->vt);
        free(con);
        return NULL;
    }
    return con;
}

static void mcon_close(mcon_t *con)
{
    if (!con) return;
    con->state = 0;
    uv_thread_join(&con->thread);
    vt_close(con->vt);
    free(con);
}

static int resolve_exec_path(char *buf, size_t len, const char *argv0)
{
#ifndef WIN32
    if (realpath(argv0, buf)) return 1;
    if (readlink("/proc/self/exe", buf, len - 1) > 0) {
        buf[len - 1] = '\0';
        return 1;
    }
#endif
    snprintf(buf, len, "%s", argv0);
    return 1;
}

static int run_supervisor(int argc, char **argv)
{
    mcors_opt_t mopt;
    mcon_t *con = NULL;
    char optfile[MAXSTR] = "";
    char exec_path[MAXSTRPATH] = "";
    int i, trace = 1, workers = 0, start = 0;

    mcors_init_opt(&mopt);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            snprintf(optfile, sizeof(optfile), "%s", argv[++i]);
        } else if (!strcmp(argv[i], "-t") && i + 1 < argc) {
            trace = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-w") && i + 1 < argc) {
            workers = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-s")) {
            start = 1;
        } else if (!strcmp(argv[i], "--shm") && i + 1 < argc) {
            snprintf(mopt.shm_name, sizeof(mopt.shm_name), "%s", argv[++i]);
        } else if (!strcmp(argv[i], "--ci") && i + 1 < argc) {
            snprintf(mopt.ci_pipe_path, sizeof(mopt.ci_pipe_path), "%s", argv[++i]);
        }
    }

    if (!optfile[0]) {
        fprintf(stderr, "usage: cors-mengine -o <cors.conf> [-w workers] [-t trace] [-s]\n");
        return 1;
    }

    if (!cors_loadopts(&g_cors_opt, optfile)) {
        fprintf(stderr, "cannot load options: %s\n", optfile);
        return 1;
    }

    if (trace > 0) {
        log_trace_open(strcmp(g_cors_opt.trace_file, "") == 0 ? TRACEFILE : g_cors_opt.trace_file);
        log_set_level(trace);
    }

    resolve_exec_path(exec_path, sizeof(exec_path), argv[0]);
    snprintf(mopt.exec_path, sizeof(mopt.exec_path), "%s", exec_path);
    if (workers > 0) mopt.worker_count = workers;

    if (!mcors_prepare(&g_mcors, &g_cors_opt, &mopt, optfile)) {
        fprintf(stderr, "mcors_prepare failed\n");
        log_trace_close();
        return 1;
    }

    signal(SIGINT, sigshut);
    signal(SIGTERM, sigshut);
    signal(SIGHUP, SIG_IGN);

    con = mcon_open("", &g_mcors);
    if (!con) {
        mcors_close(&g_mcors);
        log_trace_close();
        return 1;
    }

    if (start && !mcors_start(&g_mcors)) {
        fprintf(stderr, "mcors_start failed\n");
        mcon_close(con);
        mcors_close(&g_mcors);
        log_trace_close();
        return 1;
    }

    fprintf(stderr, "cors-mengine: %d workers, shm=%s, ci=%s\n",
            g_mcors.worker_count, mopt.shm_name, mopt.ci_pipe_path);
    fprintf(stderr, "commands: workers, shutdown\n");

    while (!intflg) uv_sleep(100);

    if (g_mcors.state) mcors_close(&g_mcors);
    mcon_close(con);
    log_trace_close();
    return 0;
}

static int run_worker(int argc, char **argv)
{
    int worker_id;
    char shm_name[MAXSTRPATH] = "";
    char partition_file[MAXSTRPATH] = "";
    char optfile[MAXSTR] = "";
    char ci_path[256] = MCORS_CI_DEFAULT_PATH;
    cors_opt_t opt;
    int i, trace = 1;

    if (argc < 6) {
        fprintf(stderr, "usage: cors-mengine --worker <id> <shm> <partition> -o <cors.conf>\n");
        return 1;
    }

    worker_id = atoi(argv[2]);
    snprintf(shm_name, sizeof(shm_name), "%s", argv[3]);
    snprintf(partition_file, sizeof(partition_file), "%s", argv[4]);

    for (i = 5; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            snprintf(optfile, sizeof(optfile), "%s", argv[++i]);
        } else if (!strcmp(argv[i], "-t") && i + 1 < argc) {
            trace = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--ci") && i + 1 < argc) {
            snprintf(ci_path, sizeof(ci_path), "%s", argv[++i]);
        }
    }

    if (!optfile[0]) {
        fprintf(stderr, "worker requires -o <cors.conf>\n");
        return 1;
    }

    if (!cors_loadopts(&opt, optfile)) return 1;

    if (trace > 0) {
        log_trace_open(strcmp(opt.trace_file, "") == 0 ? TRACEFILE : opt.trace_file);
        log_set_level(trace);
    }

    return mcors_worker_run(worker_id, shm_name, partition_file, &opt, ci_path);
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "--worker")) {
        return run_worker(argc, argv);
    }
    return run_supervisor(argc, argv);
}
