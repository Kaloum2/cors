/*------------------------------------------------------------------------------
 * supervision.h : correction-mode observability (sessions, events, metrics)
 *
 * author  : cors-engine
 * version : $Revision:$ $Date:$
 * history : 2026/06/27 1.0  new
 *-----------------------------------------------------------------------------*/
#ifndef CORR_SUPERVISION_H
#define CORR_SUPERVISION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "corr.h"
#include "uve.h"
#include "uthash.h"

#define CORR_SV_USER_LEN     32
#define CORR_SV_MNTPNT_LEN   32
#define CORR_SV_KEY_LEN      32
#define CORR_SV_REASON_LEN   128
#define CORR_SV_DETAIL_LEN   128
#define CORR_SV_VRS_LEN      64

typedef enum cors_corr_event {
    CORR_EVT_SESSION_START  = 0,
    CORR_EVT_MODE_SELECTED,
    CORR_EVT_GGA_UPDATE,
    CORR_EVT_FALLBACK,
    CORR_EVT_POLICY_DENIED,
    CORR_EVT_VRS_CREATED,
    CORR_EVT_VRS_DESTROYED,
    CORR_EVT_COUNT
} cors_corr_event_t;

/* Session de supervision (distincte de cors_corr_session_t handler corr.h) */
typedef struct cors_corr_sv_session {
    uint64_t session_id;
    char conn_key[CORR_SV_KEY_LEN];
    char user[CORR_SV_USER_LEN];
    char mountpoint[CORR_SV_MNTPNT_LEN];
    cors_corr_mode_t requested_mode;
    cors_corr_mode_t effective_mode;
    double pos[3];
    gtime_t start_time;
    gtime_t last_gga_time;
    char detail[CORR_SV_DETAIL_LEN];
    char vrs_name[CORR_SV_VRS_LEN];
    uint64_t bytes_out;
    gtime_t last_produce;
    UT_hash_handle hh;
} cors_corr_sv_session_t;

typedef struct cors_corr_mode_stats {
    uint64_t active[CORS_CORR_MODE_MAX];
    uint64_t fallback_auto;
    uint64_t policy_denied;
    uint64_t total_events[CORR_EVT_COUNT];
    uint64_t bytes_out_total;
} cors_corr_mode_stats_t;

EXPORT void cors_corr_supervision_init(void);
EXPORT void cors_corr_supervision_fini(void);

EXPORT int cors_corr_session_register(const char *user, const char *mountpoint,
                                      cors_corr_mode_t requested_mode,
                                      const char *conn_key, uint64_t *session_id);
EXPORT void cors_corr_session_deregister(uint64_t session_id);

EXPORT void cors_corr_log_event(uint64_t session_id, cors_corr_event_t event,
                                cors_corr_mode_t effective_mode,
                                const double *pos, const char *reason);

EXPORT void cors_corr_session_set_detail(uint64_t session_id, const char *detail);
EXPORT void cors_corr_session_set_vrs(uint64_t session_id, const char *vrs_name);
EXPORT void cors_corr_session_add_bytes(uint64_t session_id, int nbytes);

EXPORT int cors_corr_session_count(void);
EXPORT int cors_corr_get_mode_stats(cors_corr_mode_stats_t *stats);

EXPORT const char *cors_corr_event_str(cors_corr_event_t event);

/* Compat chaîne AUTO (service_auto.c) */
EXPORT void cors_corr_supervision_log(const char *session, const char *mountpoint,
                                      const char *selected_mode, const char *detail);

/* monitor console stubs */
EXPORT int cors_corr_monitor_showsessions(char *out, int outlen);
EXPORT int cors_corr_monitor_showmode_stats(char *out, int outlen);
EXPORT int cors_corr_monitor_cmd(const char *cmd, char *out, int outlen);

#ifdef __cplusplus
}
#endif
#endif
