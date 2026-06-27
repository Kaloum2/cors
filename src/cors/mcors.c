/*------------------------------------------------------------------------------
 * mcors.c : multi-process coordinator for cors-mengine
 *
 * author  : cors contributors
 * version : 1.0
 * history : 2026/06/13 1.0  new
 *-----------------------------------------------------------------------------*/
#include "mcors.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#ifndef WIN32
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#endif

#define MCORS_PARTITION_PREFIX "cors_worker_"
#define MCORS_MAX_SRCS_PER_WORKER 1024

typedef struct cors_src_line {
    char line[256];
    char name[64];
} cors_src_line_t;

static void mcors_default_work_dir(char *buf, size_t len)
{
    const char *tmp = getenv("TMPDIR");
    if (!tmp || !*tmp) tmp = "/tmp";
    snprintf(buf, len, "%s/cors-mengine", tmp);
}

static int mcors_read_source_lines(const char *file, cors_src_line_t **lines, int *count)
{
    FILE *fp;
    char buff[256], raw[256], *p, *q, *val[16];
    int n, cap = 0, cnt = 0;
    cors_src_line_t *arr = NULL;

    if (!(fp = fopen(file, "r"))) {
        log_trace(1, "mcors: cannot open sources %s\n", file);
        return 0;
    }
    while (fgets(buff, sizeof(buff), fp)) {
        snprintf(raw, sizeof(raw), "%s", buff);
        for (n = 0, p = buff; *p && n < 16; p = q + 1) {
            if ((q = strchr(p, ',')) || (q = strchr(p, '#'))) {
                val[n++] = p;
                *q = '\0';
            } else break;
        }
        if (n < 6) continue;
        if (cnt >= cap) {
            cap = cap ? cap * 2 : 64;
            arr = realloc(arr, cap * sizeof(*arr));
            if (!arr) break;
        }
        snprintf(arr[cnt].name, sizeof(arr[cnt].name), "%s", val[0]);
        snprintf(arr[cnt].line, sizeof(arr[cnt].line), "%s", raw);
        cnt++;
    }
    fclose(fp);
    *lines = arr;
    *count = cnt;
    return cnt > 0;
}

static int mcors_mkdir_p(const char *path)
{
#ifndef WIN32
    char tmp[512];
    char *p;
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0700);
            *p = '/';
        }
    }
    return mkdir(tmp, 0700) == 0 || access(tmp, F_OK) == 0;
#else
    (void)path;
    return 0;
#endif
}

extern void mcors_init_opt(mcors_opt_t *opt)
{
    if (!opt) return;
    memset(opt, 0, sizeof(*opt));
    snprintf(opt->shm_name, sizeof(opt->shm_name), "%s", MCORS_SHM_DEFAULT_NAME);
    snprintf(opt->ci_pipe_path, sizeof(opt->ci_pipe_path), "%s", MCORS_CI_DEFAULT_PATH);
    mcors_default_work_dir(opt->work_dir, sizeof(opt->work_dir));
}

extern int mcors_partition_sources(const char *sources_file, const char *work_dir,
                                   int worker_count, cors_shm_t *shm)
{
    cors_src_line_t *lines = NULL;
    int i, nsrc = 0, wid, per_worker;
    FILE *fp;

    if (!sources_file || !work_dir || !shm || !shm->hdr) return 0;

    if (!mcors_read_source_lines(sources_file, &lines, &nsrc)) return 0;

    if (worker_count <= 0) {
        worker_count = (int)ceil((double)nsrc / MCORS_MAX_SRCS_PER_WORKER);
    }
    if (worker_count > MCORS_MAX_WORKERS) worker_count = MCORS_MAX_WORKERS;
    if (worker_count < 1) worker_count = 1;

    per_worker = (nsrc + worker_count - 1) / worker_count;

    mcors_mkdir_p(work_dir);

    shm->hdr->src_count = 0;
    for (i = 0; i < nsrc; i++) {
        cors_shm_src_entry_t *e;
        if (shm->hdr->src_count >= MCORS_MAX_SOURCES) break;
        e = &shm->hdr->src_table[shm->hdr->src_count];
        snprintf(e->name, sizeof(e->name), "%s", lines[i].name);
        e->srcid = i + 1;
        e->worker_id = i / per_worker;
        if (e->worker_id >= worker_count) e->worker_id = worker_count - 1;
        shm->hdr->src_count++;
    }

    for (wid = 0; wid < worker_count; wid++) {
        char partfile[MAXSTRPATH];
        snprintf(partfile, sizeof(partfile), "%s/%s%d.sources", work_dir,
                 MCORS_PARTITION_PREFIX, wid);
        fp = fopen(partfile, "w");
        if (!fp) continue;
        for (i = 0; i < nsrc; i++) {
            if (shm->hdr->src_table[i].worker_id == wid) {
                fprintf(fp, "%s", lines[i].line);
                if (lines[i].line[strlen(lines[i].line) - 1] != '\n') fprintf(fp, "\n");
            }
        }
        fclose(fp);
    }

    shm->hdr->worker_count = (uint32_t)worker_count;
    free(lines);
    return worker_count;
}

extern int mcors_prepare(mcors_t *mcors, const cors_opt_t *cors_opt, const mcors_opt_t *mopt,
                          const char *config_file)
{
    int workers;

    if (!mcors || !cors_opt || !mopt) return 0;
    memset(mcors, 0, sizeof(*mcors));
    mcors->opt = *mopt;
    mcors->cors_opt = *cors_opt;
    if (config_file) {
        snprintf(mcors->config_file, sizeof(mcors->config_file), "%s", config_file);
    }

    if (!mcors->opt.work_dir[0]) {
        mcors_default_work_dir(mcors->opt.work_dir, sizeof(mcors->opt.work_dir));
    }

    mcors->shm = calloc(1, sizeof(cors_shm_t));
    if (!mcors->shm) return 0;

    workers = mopt->worker_count > 0 ? mopt->worker_count : 1;
    if (!cors_shm_create(mcors->shm, mopt->shm_name, workers)) {
        free(mcors->shm);
        mcors->shm = NULL;
        return 0;
    }

    workers = mcors_partition_sources(cors_opt->ntrip_sources_file, mcors->opt.work_dir,
                                      mopt->worker_count, mcors->shm);
    if (workers <= 0) {
        cors_shm_close(mcors->shm);
        free(mcors->shm);
        mcors->shm = NULL;
        return 0;
    }
    mcors->worker_count = workers;
    mcors->opt.worker_count = workers;
    return workers;
}

static void on_worker_exit(uv_process_t *req, int64_t exit_status, int term_signal)
{
    mcors_worker_t *w = req->data;
    log_trace(1, "mcors worker %d exited status=%lld signal=%d\n",
              w->id, (long long)exit_status, term_signal);
    w->state = 0;
}

static int mcors_spawn_worker(mcors_t *mcors, int wid)
{
    mcors_worker_t *w = &mcors->workers[wid];
    char wid_str[16], shm_str[80], partfile[MAXSTRPATH];
    char *args[16];
    int i = 0;
    uv_loop_t *loop = uv_default_loop();

    snprintf(partfile, sizeof(partfile), "%s/%s%d.sources",
             mcors->opt.work_dir, MCORS_PARTITION_PREFIX, wid);

    w->id = wid;
    snprintf(w->partition_file, sizeof(w->partition_file), "%s", partfile);
    snprintf(w->ci_pipe_path, sizeof(w->ci_pipe_path), "%s", mcors->opt.ci_pipe_path);

    snprintf(wid_str, sizeof(wid_str), "%d", wid);
    snprintf(shm_str, sizeof(shm_str), "%s", mcors->opt.shm_name);

    args[i++] = "cors-mengine";
    args[i++] = "--worker";
    args[i++] = wid_str;
    args[i++] = shm_str;
    args[i++] = w->partition_file;
    args[i++] = "-o";
    args[i++] = mcors->config_file;
    args[i++] = "--ci";
    args[i++] = w->ci_pipe_path;
    args[i++] = NULL;

    memset(&w->options, 0, sizeof(w->options));
    w->options.file = mcors->opt.exec_path[0] ? mcors->opt.exec_path : args[0];
    w->options.args = args;
    w->options.exit_cb = on_worker_exit;

    w->process.data = w;
    if (uv_spawn(loop, &w->process, &w->options) != 0) {
        log_trace(1, "mcors_spawn_worker %d failed\n", wid);
        return 0;
    }
    w->state = 1;
    mcors->shm->hdr->active_workers++;
    return 1;
}

static int rtcm_relay_cb(void *userdata, const uint8_t *data, int n, int srcid)
{
    cors_t *cors = (cors_t *)userdata;
    cors_ntrip_source_info_t *s, *tmp, *info = NULL;

    HASH_ITER(ii, cors->ntrip.info_tbl[1], s, tmp) {
        if (s->ID == srcid) {
            info = s;
            break;
        }
    }
    if (info) {
        cors_ntrip_agent_send(&cors->agent, info->mntpnt, (const char *)data, n,
                              &cors->nav.data);
    }
    return n;
}

static void mcors_sync_thread(void *arg)
{
    mcors_t *mcors = (mcors_t *)arg;
    cors_t *cors = &mcors->cors;

    while (mcors->state && !cors_shm_shutdown_requested(mcors->shm)) {
        cors_shm_sync_nav(mcors->shm, &cors->nav);
        cors_shm_sync_obs(mcors->shm, &cors->obs);
        cors_shm_sync_blsols(mcors->shm, &cors->blsols);
        cors_shm_drain_rtcm(mcors->shm, rtcm_relay_cb, cors);
        uv_sleep(50);
    }
}

extern int mcors_start(mcors_t *mcors)
{
    int wid;
    uv_loop_t *loop;

    if (!mcors || !mcors->shm || mcors->worker_count <= 0) return 0;

    loop = uv_default_loop();
    if (!cors_ci_start(&mcors->ci, loop, mcors->opt.ci_pipe_path)) {
        log_trace(1, "mcors_start: ci start failed\n");
        return 0;
    }

    if (!cors_start_supervisor(&mcors->cors, &mcors->cors_opt, mcors->shm)) {
        cors_ci_close(&mcors->ci);
        return 0;
    }

    for (wid = 0; wid < mcors->worker_count; wid++) {
        if (!mcors_spawn_worker(mcors, wid)) {
            log_trace(1, "mcors_start: worker %d spawn failed\n", wid);
        }
    }

    mcors->state = 1;
    if (uv_thread_create(&mcors->sync_thread, mcors_sync_thread, mcors)) {
        log_trace(1, "mcors_start: sync thread failed\n");
    }
    return 1;
}

extern void mcors_close(mcors_t *mcors)
{
    int wid;

    if (!mcors) return;

    mcors->state = 0;
    if (mcors->shm) cors_shm_request_shutdown(mcors->shm);

    uv_thread_join(&mcors->sync_thread);

    for (wid = 0; wid < mcors->worker_count; wid++) {
        mcors_worker_t *w = &mcors->workers[wid];
        if (w->state) {
#ifndef WIN32
            uv_process_kill(&w->process, SIGTERM);
#endif
            uv_close((uv_handle_t *)&w->process, NULL);
        }
    }

    if (mcors->cors.state) cors_close(&mcors->cors);
    cors_ci_close(&mcors->ci);

    if (mcors->shm) {
        cors_shm_close(mcors->shm);
        free(mcors->shm);
        mcors->shm = NULL;
    }
    mcors->state = 0;
}

extern int mcors_worker_run(int worker_id, const char *shm_name,
                            const char *partition_file, const cors_opt_t *cors_opt,
                            const char *ci_pipe_path)
{
    cors_t cors = {0};
    cors_shm_t shm = {0};
    cors_opt_t opt;

    (void)ci_pipe_path;

    if (!shm_name || !partition_file || !cors_opt) return 1;

    opt = *cors_opt;
    snprintf(opt.ntrip_sources_file, sizeof(opt.ntrip_sources_file), "%s", partition_file);

    if (!cors_shm_open(&shm, shm_name)) {
        fprintf(stderr, "worker %d: cannot open shm %s\n", worker_id, shm_name);
        return 1;
    }

    if (!cors_start_worker(&cors, &opt, &shm, worker_id)) {
        fprintf(stderr, "worker %d: cors_start_worker failed\n", worker_id);
        cors_shm_close(&shm);
        return 1;
    }

    while (cors.state && !cors_shm_shutdown_requested(&shm)) {
        uv_sleep(100);
    }

    cors_close(&cors);
    cors_shm_close(&shm);
    return 0;
}
