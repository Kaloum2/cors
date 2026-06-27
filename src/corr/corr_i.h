/*------------------------------------------------------------------------------
 * corr_i.h : internal helpers for correction services
 *-----------------------------------------------------------------------------*/
#ifndef CORR_I_H
#define CORR_I_H

#include "corr.h"

typedef struct cors_corr_sess_priv {
    cors_corr_ctx_t *ctx;
    char out_mntpnt[32];
    char vsta_name[32];
    const cors_corr_service_t *auto_svc;
    cors_corr_mode_t auto_mode;
} cors_corr_sess_priv_t;

cors_corr_sess_priv_t *corr_sess_priv(cors_corr_session_t *sess);
cors_corr_ctx_t *corr_global_ctx(void);

extern const cors_corr_service_t cors_corr_service_relay;
extern const cors_corr_service_t cors_corr_service_near;
extern const cors_corr_service_t cors_corr_service_vrs_dyn;
extern const cors_corr_service_t cors_corr_service_fkp;
extern const cors_corr_service_t cors_corr_service_mac;
extern const cors_corr_service_t cors_corr_service_imax;
extern const cors_corr_service_t cors_corr_service_auto;

#endif
