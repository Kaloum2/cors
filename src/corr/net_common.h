#ifndef NET_COMMON_H
#define NET_COMMON_H

#include "corr.h"

#define CORR_FKP_MAXSAT  32

typedef struct corr_fkp_sat {
    int prn;
    int iod;
    double grad_n0;  /* geometric gradient north (ppm) */
    double grad_e0;  /* geometric gradient east  (ppm) */
    double grad_n1;  /* ionospheric gradient north (ppm) */
    double grad_e1;  /* ionospheric gradient east  (ppm) */
} corr_fkp_sat_t;

typedef struct corr_fkp_data {
    int staid;
    int nsat;
    corr_fkp_sat_t sat[CORR_FKP_MAXSAT];
} corr_fkp_data_t;

typedef struct corr_net_ctx {
    const cors_dtrig_t *dtrig;
    int master_srcid;
    char master_name[32];
    int aux_srcid[2];
    char aux_name[2][32];
    int n_physical;
    int n_fixed_bls;
} corr_net_ctx_t;

const cors_corr_cfg_t *corr_cfg_get(void);
int corr_cfg_load(const char *file);

int corr_pos_in_dtrig(cors_corr_ctx_t *ctx, const double pos[3]);
const cors_dtrig_t *corr_dtrig_at_pos(cors_corr_ctx_t *ctx, const double pos[3]);
int corr_nearest_physical(cors_corr_ctx_t *ctx, const double pos[3], char *name, int n);
int corr_dtrig_fixed_baseline_count(cors_corr_ctx_t *ctx, const cors_dtrig_t *d);
int corr_net_ctx_at_pos(cors_corr_ctx_t *ctx, const double pos[3], corr_net_ctx_t *out);
int corr_fkp_compute(cors_corr_ctx_t *ctx, const double pos[3], int sys,
                     corr_fkp_data_t *fkp);
int corr_eligible_vrs_dynamic(cors_corr_ctx_t *ctx, const double pos[3]);
int corr_eligible_mac(cors_corr_ctx_t *ctx, const double pos[3]);
int corr_eligible_fkp(cors_corr_ctx_t *ctx, const double pos[3]);
int corr_srcinfo_name(cors_corr_ctx_t *ctx, int srcid, char *name, int n);
int corr_srcinfo_pos(cors_corr_ctx_t *ctx, int srcid, double pos[3]);
int corr_pack_fkp(cors_corr_ctx_t *ctx, const double pos[3], const nav_t *nav,
                  char *buff, int max);
int corr_pack_mac(cors_corr_ctx_t *ctx, const double pos[3], const nav_t *nav,
                  char *buff, int max);

#endif
