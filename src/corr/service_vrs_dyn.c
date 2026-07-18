/*------------------------------------------------------------------------------
 * service_vrs_dyn.c : dynamic VRS at rover GGA position
 *-----------------------------------------------------------------------------*/
#include "corr_i.h"
#include "net_common.h"
#include "supervision.h"
#include "log.h"

#define SQR(x) ((x)*(x))

extern int vrs_add_vsta(cors_vrs_t *vrs, const char *name, const double *pos);
extern int vrs_del_vsta(cors_vrs_t *vrs, const char *name);

static int vrs_dyn_attach(cors_corr_session_t *sess)
{
    cors_corr_sess_priv_t *p=corr_sess_priv(sess);

    snprintf(p->vsta_name,sizeof(p->vsta_name),"VRS_%p",(void*)sess->conn->conn);
    p->out_mntpnt[0]='\0';
    log_trace(1,"corr vrs_dynamic attach: session=%s\n",p->vsta_name);
    return 1;
}

static void vrs_dyn_detach(cors_corr_session_t *sess)
{
    cors_corr_sess_priv_t *p=corr_sess_priv(sess);
    cors_vrs_t *vrs;

    if (!p->vsta_name[0]) return;
    vrs=&corr_global_ctx()->cors->vrs;
    log_trace(1,"corr vrs_dynamic detach: %s\n",p->vsta_name);
    vrs_del_vsta(vrs,p->vsta_name);
    if (sess->conn&&sess->conn->sv_session_id) {
        cors_corr_log_event(sess->conn->sv_session_id,CORR_EVT_VRS_DESTROYED,
                            CORS_CORR_VRS_DYNAMIC,NULL,p->vsta_name);
        cors_corr_session_set_vrs(sess->conn->sv_session_id,NULL);
    }
    p->vsta_name[0]='\0';
    p->out_mntpnt[0]='\0';
}

static int vrs_dyn_ensure(cors_corr_session_t *sess, const double *pos)
{
    cors_corr_sess_priv_t *p=corr_sess_priv(sess);
    cors_corr_ctx_t *ctx=corr_global_ctx();
    cors_vrs_t *vrs;
    cors_vrs_sta_t *sta;
    const cors_corr_cfg_t *cfg=corr_cfg_get();
    double thresh=cfg?cfg->vrs_dynamic_move_thresh_m:1000.0;

    if (!ctx->cors||norm((double*)pos,3)<=0.0) return 0;
    if (!corr_pos_in_dtrig(ctx,pos)) {
        log_trace(1,"corr vrs_dynamic: rejected (outside dtrig) %s\n",p->vsta_name);
        if (sess->conn&&sess->conn->sv_session_id) {
            cors_corr_session_set_detail(sess->conn->sv_session_id,"outside_dtrig");
        }
        return 0;
    }
    vrs=&ctx->cors->vrs;
    HASH_FIND_STR(vrs->stas.data,p->vsta_name,sta);
    if (sta) {
        double d=sqrt(SQR(sta->pos[0]-pos[0])+SQR(sta->pos[1]-pos[1])+SQR(sta->pos[2]-pos[2]));
        if (d<thresh) return 1;
        vrs_del_vsta(vrs,p->vsta_name);
    }
    if (!vrs_add_vsta(vrs,p->vsta_name,pos)) {
        log_trace(1,"corr vrs_dynamic: vrs_add_vsta failed %s\n",p->vsta_name);
        return 0;
    }
    strncpy(p->out_mntpnt,p->vsta_name,sizeof(p->out_mntpnt)-1);
    log_trace(1,"corr vrs_dynamic: created/updated %s in_dtrig=1 move=%.0fm\n",
              p->vsta_name,thresh);
    if (sess->conn&&sess->conn->sv_session_id) {
        cors_corr_log_event(sess->conn->sv_session_id,CORR_EVT_VRS_CREATED,
                            CORS_CORR_VRS_DYNAMIC,pos,p->vsta_name);
        cors_corr_session_set_detail(sess->conn->sv_session_id,"in_dtrig=1");
    }
    return 1;
}

static int vrs_dyn_on_gga(cors_corr_session_t *sess, const double pos[3])
{
    char key[32];
    int chg;

    chg=vrs_dyn_ensure(sess,pos)?1:0;
    snprintf(key,sizeof(key),"%p",(void*)sess->conn->conn);
    cors_corr_supervision_log(key,sess->conn->mntpnt,"vrs_dynamic",
                              chg?"gga_update":"gga_rejected");
    return chg;
}

static int vrs_dyn_produce(cors_corr_session_t *sess, uint8_t *buf, int max_len, const nav_t *nav)
{
    cors_corr_sess_priv_t *p=corr_sess_priv(sess);
    cors_vrs_t *vrs;
    cors_vrs_sta_t *sta;

    if (!p->vsta_name[0]||!corr_global_ctx()->cors) return 0;
    vrs=&corr_global_ctx()->cors->vrs;
    HASH_FIND_STR(vrs->stas.data,p->vsta_name,sta);
    if (!sta||!sta->in_dtrig||sta->obs.n<=0) return 0;
    return cors_vrs_encode_obs_rtcm(sta,nav,(char*)buf,max_len);
}

static int vrs_dyn_sourcetable_str(const cors_corr_service_t *svc,
                                   const cors_mountpoint_def_t *mntdef,
                                   char *buf, int max_len)
{
    (void)svc;
    return snprintf(buf,max_len,"STR;%s;%s;RTCM3;2;GPS+GLO+GAL;;NONE;0;FRA;VRS dynamic at GGA\n",
                    mntdef->name,mntdef->name);
}

const cors_corr_service_t cors_corr_service_vrs_dyn={
    CORS_CORR_VRS_DYNAMIC, "vrs_dynamic",
    vrs_dyn_attach, vrs_dyn_on_gga, vrs_dyn_produce, vrs_dyn_detach, vrs_dyn_sourcetable_str
};
