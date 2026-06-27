/*------------------------------------------------------------------------------
 * service_near.c : nearest physical base (RTCM32 / NEAR)
 *-----------------------------------------------------------------------------*/
#include "corr_i.h"
#include "log.h"
#include "kdtree.h"

static int near_find_base(cors_corr_ctx_t *ctx, const double *pos, char *out, int n)
{
    cors_ntrip_t *ntrip;
    struct kdres *res;
    cors_ntrip_source_info_t *info;

    if (!ctx||!ctx->cors||norm((double*)pos,3)<=0.0) return 0;
    ntrip=&ctx->cors->ntrip;
    if (!ntrip->src_kdtree) return 0;
    if (!(res=kd_nearest(ntrip->src_kdtree,pos))) return 0;
    info=kd_res_item_data(res);
    if (!info) return 0;
    strncpy(out,info->name,n-1);
    out[n-1]='\0';
    return 1;
}

static int near_attach(cors_corr_session_t *sess)
{
    log_trace(1,"corr nearest attach: %s\n",sess->conn->mntpnt);
    return 1;
}

static void near_detach(cors_corr_session_t *sess)
{
    (void)sess;
}

static int near_on_gga(cors_corr_session_t *sess, const double pos[3])
{
    char new_mntpnt[32]="";
    cors_corr_sess_priv_t *p=corr_sess_priv(sess);
    cors_corr_ctx_t *ctx=corr_global_ctx();
    int chg=0;

    if (!near_find_base(ctx,pos,new_mntpnt,sizeof(new_mntpnt))) return 0;
    if (strcmp(p->out_mntpnt,new_mntpnt)) {
        log_trace(1,"corr nearest: %s -> %s\n",sess->conn->mntpnt,new_mntpnt);
        strncpy(p->out_mntpnt,new_mntpnt,sizeof(p->out_mntpnt)-1);
        chg=1;
    }
    return chg;
}

static int near_produce(cors_corr_session_t *sess, uint8_t *buf, int max_len, const nav_t *nav)
{
    (void)sess; (void)buf; (void)max_len; (void)nav;
    return 0;
}

static int near_sourcetable_str(const cors_corr_service_t *svc,
                                 const cors_mountpoint_def_t *mntdef,
                                 char *buf, int max_len)
{
    (void)svc;
    return snprintf(buf,max_len,"STR;%s;%s;RTCM3;2;GPS;;NONE;0;FRA;Nearest base at GGA\n",
                    mntdef->name,mntdef->name);
}

const cors_corr_service_t cors_corr_service_near={
    CORS_CORR_NEAR, "near",
    near_attach, near_on_gga, near_produce, near_detach, near_sourcetable_str
};
