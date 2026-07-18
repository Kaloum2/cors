/*------------------------------------------------------------------------------
 * policy.h : NTRIP user policy — mode ACL, geofencing, session quotas
 *
 * See doc/dev/security-modes.md
 *-----------------------------------------------------------------------------*/
#ifndef CORS_POLICY_H
#define CORS_POLICY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rtklib.h"

typedef struct cors_ntrip_user cors_ntrip_user_t;

/* correction mode ACL bitmask (distinct from corr.h service enum) */
typedef uint32_t cors_policy_mode_t;

#define CORR_MODE_RELAY        (1u << 0)
#define CORR_MODE_VRS_DYNAMIC  (1u << 1)
#define CORR_MODE_FKP          (1u << 2)
#define CORR_MODE_MAC          (1u << 3)
#define CORR_MODE_IMAX         (1u << 4)
#define CORR_MODE_AUTO         (1u << 5)
#define CORR_MODE_ALL          (CORR_MODE_RELAY|CORR_MODE_VRS_DYNAMIC|CORR_MODE_FKP|\
                                CORR_MODE_MAC|CORR_MODE_IMAX|CORR_MODE_AUTO)

/* geographic region ----------------------------------------------------------*/
#define CORS_REGION_NONE    0
#define CORS_REGION_BBOX    1
#define CORS_REGION_POLYGON 2

#define CORS_POLICY_MAX_POLY_VERTS  256
#define CORS_POLICY_REGION_RECHECK_SEC  3600.0  /* hourly geofence re-check on active session */

typedef struct cors_user_region_bbox {
    double lat_min, lon_min;
    double lat_max, lon_max;
} cors_user_region_bbox_t;

typedef struct cors_user_region {
    int type;
    union {
        cors_user_region_bbox_t bbox;
        struct {
            int n;
            double lat[CORS_POLICY_MAX_POLY_VERTS];
            double lon[CORS_POLICY_MAX_POLY_VERTS];
        } poly;
    } u;
} cors_user_region_t;

typedef struct cors_user_policy {
    cors_policy_mode_t allowed_modes;
    cors_user_region_t region;
    int max_sessions; /* 0 = unlimited */
    /* 1 = FKP/VRS/MAC may use FLOAT baselines (stream without triangle FIX) */
    int allow_float;
} cors_user_policy_t;

/* NTRIP HTTP responses (for ntripagent integration) --------------------------*/
#define CORS_NTRIP_RSP_UNAUTHORIZED \
    "HTTP/1.0 401 Unauthorized\r\n"
#define CORS_NTRIP_RSP_FORBIDDEN \
    "HTTP/1.0 403 Forbidden\r\n"
#define CORS_NTRIP_RSP_FORBIDDEN_MODE \
    "HTTP/1.0 403 Forbidden\r\nReason: mode not allowed\r\n"
#define CORS_NTRIP_RSP_FORBIDDEN_REGION \
    "HTTP/1.0 403 Forbidden\r\nReason: outside authorized region\r\n"
#define CORS_NTRIP_RSP_FORBIDDEN_SESSION \
    "HTTP/1.0 403 Forbidden\r\nReason: session quota exceeded\r\n"

EXPORT void cors_user_policy_init(cors_user_policy_t *policy);
EXPORT void cors_user_policy_free(cors_user_policy_t *policy);
EXPORT int  cors_user_policy_parse(cors_user_policy_t *policy,
                                   const char *modes_str,
                                   const char *region_str,
                                   const char *max_sess_str);

EXPORT cors_policy_mode_t cors_corr_mode_from_name(const char *name);
EXPORT cors_policy_mode_t cors_corr_mode_from_mountpoint(const char *mntpnt);

EXPORT int cors_user_region_contains(const cors_user_region_t *region,
                                     const double pos[3]);

EXPORT int cors_policy_check(const cors_ntrip_user_t *user, cors_policy_mode_t mode,
                             const double pos[3]);
EXPORT int cors_policy_check_sessions(const cors_ntrip_user_t *user, int active_count);

EXPORT int  cors_policy_read_users(const char *path, cors_ntrip_user_t **user_tbl);
EXPORT void cors_policy_free_users(cors_ntrip_user_t **user_tbl);

#ifdef __cplusplus
}
#endif
#endif
