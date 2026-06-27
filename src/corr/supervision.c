/*------------------------------------------------------------------------------
 * supervision.c : correction-mode observability for CORS
 *
 * author  : cors-engine
 * version : $Revision:$ $Date:$
 * history : 2026/06/27 1.0  new
 *-----------------------------------------------------------------------------*/
#include "supervision.h"
#include "log.h"
#include <stdio.h>
#include <string.h>

#define CORR_LOG_TAG "[CORR]"

typedef struct cors_corr_supervision {
    cors_corr_sv_session_t *sessions;
    uv_mutex_t lock;
    uint64_t next_session_id;
    cors_corr_mode_stats_t stats;
    int initialized;
} cors_corr_supervision_t;

static cors_corr_supervision_t corr_sv = {0};

static const char *corr_event_names[CORR_EVT_COUNT] = {
    "session_start",
    "mode_selected",
    "gga_update",
    "fallback",
    "policy_denied",
    "vrs_created",
    "vrs_destroyed"
};

static int corr_log_level(cors_corr_event_t event)
{
    switch (event) {
        case CORR_EVT_POLICY_DENIED: return 1;
        case CORR_EVT_FALLBACK:      return 2;
        default:                     return 3;
    }
}

static void corr_format_pos(const double *pos, char *buf, int n)
{
    if (!pos) {
        snprintf(buf, n, "-,-,-");
        return;
    }
    snprintf(buf, n, "%.6f,%.6f,%.1f", pos[0], pos[1], pos[2]);
}

static cors_corr_sv_session_t *corr_find_session(uint64_t session_id)
{
    cors_corr_sv_session_t *s = NULL;
    HASH_FIND(hh, corr_sv.sessions, &session_id, sizeof(session_id), s);
    return s;
}

static cors_corr_sv_session_t *corr_find_session_key(const char *conn_key)
{
    cors_corr_sv_session_t *s, *tmp;

    if (!conn_key || !*conn_key) return NULL;
    HASH_ITER(hh, corr_sv.sessions, s, tmp) {
        if (!strcmp(s->conn_key, conn_key)) return s;
    }
    return NULL;
}

static void corr_update_stats_event(cors_corr_event_t event)
{
    if (event >= 0 && event < CORR_EVT_COUNT) {
        corr_sv.stats.total_events[event]++;
    }
    if (event == CORR_EVT_FALLBACK) {
        corr_sv.stats.fallback_auto++;
    }
    if (event == CORR_EVT_POLICY_DENIED) {
        corr_sv.stats.policy_denied++;
    }
}

static void corr_recount_active_sessions(void)
{
    cors_corr_sv_session_t *s, *tmp;
    int i;

    for (i = 0; i < CORS_CORR_MODE_MAX; i++) {
        corr_sv.stats.active[i] = 0;
    }
    HASH_ITER(hh, corr_sv.sessions, s, tmp) {
        if (s->effective_mode >= 0 && s->effective_mode < CORS_CORR_MODE_MAX) {
            corr_sv.stats.active[s->effective_mode]++;
        }
    }
}

extern const char *cors_corr_event_str(cors_corr_event_t event)
{
    if (event < 0 || event >= CORR_EVT_COUNT) return "unknown";
    return corr_event_names[event];
}

extern void cors_corr_supervision_init(void)
{
    if (corr_sv.initialized) return;

    memset(&corr_sv.stats, 0, sizeof(corr_sv.stats));
    corr_sv.next_session_id = 1;
    uv_mutex_init(&corr_sv.lock);
    corr_sv.initialized = 1;
    log_trace(3, "%s supervision init\n", CORR_LOG_TAG);
}

extern void cors_corr_supervision_fini(void)
{
    cors_corr_sv_session_t *s, *tmp;

    if (!corr_sv.initialized) return;

    uv_mutex_lock(&corr_sv.lock);
    HASH_ITER(hh, corr_sv.sessions, s, tmp) {
        HASH_DEL(corr_sv.sessions, s);
        free(s);
    }
    corr_sv.sessions = NULL;
    uv_mutex_unlock(&corr_sv.lock);

    uv_mutex_destroy(&corr_sv.lock);
    corr_sv.initialized = 0;
}

extern int cors_corr_session_register(const char *user, const char *mountpoint,
                                      cors_corr_mode_t requested_mode,
                                      const char *conn_key, uint64_t *session_id)
{
    cors_corr_sv_session_t *s;

    if (!corr_sv.initialized) cors_corr_supervision_init();
    if (!user || !mountpoint || !session_id) return 0;

    s = calloc(1, sizeof(*s));
    if (!s) return 0;

    uv_mutex_lock(&corr_sv.lock);
    s->session_id = corr_sv.next_session_id++;
    strncpy(s->user, user, CORR_SV_USER_LEN - 1);
    strncpy(s->mountpoint, mountpoint, CORR_SV_MNTPNT_LEN - 1);
    if (conn_key) strncpy(s->conn_key, conn_key, CORR_SV_KEY_LEN - 1);
    s->requested_mode = requested_mode;
    s->effective_mode = requested_mode;
    s->start_time = timeget();
    HASH_ADD(hh, corr_sv.sessions, session_id, sizeof(s->session_id), s);
    *session_id = s->session_id;
    corr_recount_active_sessions();
    uv_mutex_unlock(&corr_sv.lock);

    cors_corr_log_event(s->session_id, CORR_EVT_SESSION_START, s->effective_mode,
                        NULL, "connected");
    return 1;
}

extern void cors_corr_session_deregister(uint64_t session_id)
{
    cors_corr_sv_session_t *s;

    if (!corr_sv.initialized) return;

    uv_mutex_lock(&corr_sv.lock);
    s = corr_find_session(session_id);
    if (s) {
        HASH_DEL(corr_sv.sessions, s);
        free(s);
        corr_recount_active_sessions();
    }
    uv_mutex_unlock(&corr_sv.lock);
}

extern void cors_corr_log_event(uint64_t session_id, cors_corr_event_t event,
                                cors_corr_mode_t effective_mode,
                                const double *pos, const char *reason)
{
    cors_corr_sv_session_t *s;
    char user[CORR_SV_USER_LEN] = "-";
    char mountpoint[CORR_SV_MNTPNT_LEN] = "-";
    char pos_str[64];
    char reason_buf[CORR_SV_REASON_LEN];
    cors_corr_mode_t mode = effective_mode;
    int level;

    if (!corr_sv.initialized) cors_corr_supervision_init();
    if (event < 0 || event >= CORR_EVT_COUNT) return;

    strncpy(reason_buf, reason ? reason : "", CORR_SV_REASON_LEN - 1);
    reason_buf[CORR_SV_REASON_LEN - 1] = '\0';

    uv_mutex_lock(&corr_sv.lock);

    s = corr_find_session(session_id);
    if (s) {
        strncpy(user, s->user, sizeof(user) - 1);
        strncpy(mountpoint, s->mountpoint, sizeof(mountpoint) - 1);
        if (mode < 0 || mode >= CORS_CORR_MODE_MAX) mode = s->effective_mode;

        if (event == CORR_EVT_MODE_SELECTED || event == CORR_EVT_FALLBACK ||
            event == CORR_EVT_SESSION_START) {
            s->effective_mode = mode;
        }
        if (event == CORR_EVT_GGA_UPDATE && pos) {
            s->pos[0] = pos[0];
            s->pos[1] = pos[1];
            s->pos[2] = pos[2];
            s->last_gga_time = timeget();
        }
        corr_recount_active_sessions();
    }

    corr_update_stats_event(event);
    corr_format_pos(pos ? pos : (s ? s->pos : NULL), pos_str, sizeof(pos_str));
    uv_mutex_unlock(&corr_sv.lock);

    level = corr_log_level(event);
    log_trace(level,
              "%s event=%s session_id=%llu user=%s mountpoint=%s "
              "effective_mode=%s pos=%s reason=%s\n",
              CORR_LOG_TAG,
              cors_corr_event_str(event),
              (unsigned long long)session_id,
              user,
              mountpoint,
              cors_corr_mode_str(mode),
              pos_str,
              reason_buf[0] ? reason_buf : "-");
}

extern void cors_corr_supervision_log(const char *session, const char *mountpoint,
                                      const char *selected_mode, const char *detail)
{
    cors_corr_sv_session_t *s;
    cors_corr_event_t event = CORR_EVT_MODE_SELECTED;
    cors_corr_mode_t mode;
    uint64_t session_id = 0;

    if (!corr_sv.initialized) cors_corr_supervision_init();

    uv_mutex_lock(&corr_sv.lock);
    s = corr_find_session_key(session);
    if (s) session_id = s->session_id;
    uv_mutex_unlock(&corr_sv.lock);

    mode = cors_corr_mode_parse(selected_mode);
    if (detail && strstr(detail, "fallback")) {
        event = CORR_EVT_FALLBACK;
    }
    cors_corr_log_event(session_id, event, mode, NULL, detail);
}

extern int cors_corr_session_count(void)
{
    int n = 0;

    if (!corr_sv.initialized) return 0;

    uv_mutex_lock(&corr_sv.lock);
    n = HASH_COUNT(corr_sv.sessions);
    uv_mutex_unlock(&corr_sv.lock);
    return n;
}

extern int cors_corr_get_mode_stats(cors_corr_mode_stats_t *stats)
{
    if (!stats) return 0;
    if (!corr_sv.initialized) cors_corr_supervision_init();

    uv_mutex_lock(&corr_sv.lock);
    *stats = corr_sv.stats;
    uv_mutex_unlock(&corr_sv.lock);
    return 1;
}

extern int cors_corr_monitor_showsessions(char *out, int outlen)
{
    cors_corr_sv_session_t *s, *tmp;
    int n = 0, rem;
    char tstr[32], pos_str[64];

    if (!out || outlen <= 0) return 0;
    if (!corr_sv.initialized) cors_corr_supervision_init();

    rem = outlen;
    n += snprintf(out + n, rem - n, "active_sessions=%d\n", cors_corr_session_count());
    rem = outlen - n;
    if (rem <= 0) return n;

    uv_mutex_lock(&corr_sv.lock);
    HASH_ITER(hh, corr_sv.sessions, s, tmp) {
        time2str(s->start_time, tstr, 0);
        corr_format_pos(s->pos, pos_str, sizeof(pos_str));
        n += snprintf(out + n, rem - n,
                      "session_id=%llu user=%s mountpoint=%s "
                      "requested=%s effective=%s pos=%s start=%s\n",
                      (unsigned long long)s->session_id,
                      s->user,
                      s->mountpoint,
                      cors_corr_mode_str(s->requested_mode),
                      cors_corr_mode_str(s->effective_mode),
                      pos_str,
                      tstr);
        rem = outlen - n;
        if (rem <= 0) break;
    }
    uv_mutex_unlock(&corr_sv.lock);
    return n;
}

extern int cors_corr_monitor_showmode_stats(char *out, int outlen)
{
    cors_corr_mode_stats_t stats;
    int i, n = 0, rem;

    if (!out || outlen <= 0) return 0;
    if (!cors_corr_get_mode_stats(&stats)) return 0;

    rem = outlen;
    for (i = 0; i < CORS_CORR_MODE_MAX; i++) {
        if (!stats.active[i]) continue;
        n += snprintf(out + n, rem - n, "active_mode=%s count=%llu\n",
                      cors_corr_mode_str((cors_corr_mode_t)i),
                      (unsigned long long)stats.active[i]);
        rem = outlen - n;
        if (rem <= 0) return n;
    }
    n += snprintf(out + n, rem - n,
                  "fallback_auto=%llu policy_denied=%llu\n",
                  (unsigned long long)stats.fallback_auto,
                  (unsigned long long)stats.policy_denied);
    return n;
}

extern int cors_corr_monitor_cmd(const char *cmd, char *out, int outlen)
{
    if (!cmd || !out || outlen <= 0) return 0;

    if (!strcmp(cmd, "showsessions")) {
        return cors_corr_monitor_showsessions(out, outlen);
    }
    if (!strcmp(cmd, "showmode_stats")) {
        return cors_corr_monitor_showmode_stats(out, outlen);
    }
    snprintf(out, outlen, "unknown corr monitor cmd: %s\n", cmd);
    return (int)strlen(out);
}
