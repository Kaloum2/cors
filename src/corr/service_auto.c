/*------------------------------------------------------------------------------
 * service_auto.c : AUTO/NEAR multi-mode priority chain
 *
 * Priority: VRS_DYNAMIC -> MAC -> FKP -> NEAR
 *-----------------------------------------------------------------------------*/
#include "corr_i.h"
#include "net_common.h"
#include "supervision.h"
#include "log.h"

static int mode_eligible(cors_corr_session_t *sess, cors_corr_mode_t mode)
{
    cors_corr_ctx_t *ctx=corr_global_ctx();
    int allow_float=corr_sess_allow_float(sess);

    if (norm(sess->pos,3)<=0.0&&mode!=CORS_CORR_NEAR) return 0;
    switch (mode) {
        case CORS_CORR_VRS_DYNAMIC: return corr_eligible_vrs_dynamic(ctx,sess->pos,allow_float);
        case CORS_CORR_MAC:         return corr_eligible_mac(ctx,sess->pos,allow_float);
        case CORS_CORR_FKP:         return corr_eligible_fkp(ctx,sess->pos,allow_float);
        case CORS_CORR_NEAR:        return norm(sess->pos,3)>0.0;
        default: return 0;
    }
}

static int auto_try(cors_corr_session_t *sess, cors_corr_mode_t mode)
{
    const cors_corr_service_t *svc=cors_corr_service_for_mode(mode);
    cors_corr_sess_priv_t *p=corr_sess_priv(sess);
    char detail[128]="";
    char key[32];

    if (!svc||!mode_eligible(sess,mode)) return 0;
    if (!svc->attach(sess)) return 0;

    p->auto_svc=svc;
    p->auto_mode=mode;
    sess->mode=mode;
    /* Keep sess->svc as AUTO so on_gga/produce stay on this wrapper. */

    snprintf(key,sizeof(key),"%p",(void*)sess->conn->conn);
    switch (mode) {
        case CORS_CORR_VRS_DYNAMIC: snprintf(detail,sizeof(detail),"in_dtrig=1"); break;
        case CORS_CORR_MAC:         snprintf(detail,sizeof(detail),"master_aux=1"); break;
        case CORS_CORR_FKP:         snprintf(detail,sizeof(detail),"fkp_ready=1"); break;
        case CORS_CORR_NEAR:        snprintf(detail,sizeof(detail),"fallback nearest"); break;
        default: break;
    }
    cors_corr_supervision_log(key,sess->conn->mntpnt,svc->name,detail);
    if (norm(sess->pos,3)>0.0&&svc->on_gga) svc->on_gga(sess,sess->pos);
    return 1;
}

static int auto_select(cors_corr_session_t *sess)
{
    static const cors_corr_mode_t chain[]={
        CORS_CORR_VRS_DYNAMIC, CORS_CORR_MAC, CORS_CORR_FKP, CORS_CORR_NEAR
    };
    int i;

    for (i=0;i<(int)(sizeof(chain)/sizeof(chain[0]));i++) {
        if (auto_try(sess,chain[i])) return 1;
    }
    return 0;
}

static int auto_attach(cors_corr_session_t *sess)
{
    log_trace(1,"corr auto attach: %s\n",sess->conn->mntpnt);
    return 1;
}

static void auto_detach(cors_corr_session_t *sess)
{
    cors_corr_sess_priv_t *p=corr_sess_priv(sess);

    if (p->auto_svc&&p->auto_svc->detach) p->auto_svc->detach(sess);
    p->auto_svc=NULL;
}

static int auto_on_gga(cors_corr_session_t *sess, const double pos[3])
{
    cors_corr_sess_priv_t *p=corr_sess_priv(sess);
    cors_corr_mode_t prev=p->auto_mode;
    int chg=0;

    if (p->auto_svc&&p->auto_svc->detach) p->auto_svc->detach(sess);
    p->auto_svc=NULL;
    if (!auto_select(sess)) return 0;
    if (p->auto_mode!=prev) chg=1;
    if (p->auto_svc&&p->auto_svc->on_gga) {
        if (p->auto_svc->on_gga(sess,pos)) chg=1;
    }
    return chg;
}

static int auto_produce(cors_corr_session_t *sess, uint8_t *buf, int max_len, const nav_t *nav)
{
    static const cors_corr_mode_t chain[]={
        CORS_CORR_VRS_DYNAMIC, CORS_CORR_MAC, CORS_CORR_FKP, CORS_CORR_NEAR
    };
    cors_corr_sess_priv_t *p=corr_sess_priv(sess);
    int i,n,passed=0;

    if (!p->auto_svc&&!auto_select(sess)) return 0;
    /* NEAR delivers via mountpoint remap (agent_upd_conn), not produce(). */
    if (p->auto_mode==CORS_CORR_NEAR) return 0;
    if (p->auto_svc&&p->auto_svc->produce) {
        n=p->auto_svc->produce(sess,buf,max_len,nav);
        if (n>0) return n;
    }
    /* Higher modes attached but idle — fall through (FKP payload or NEAR remap). */
    for (i=0;i<(int)(sizeof(chain)/sizeof(chain[0]));i++) {
        if (p->auto_mode==chain[i]) {passed=1; continue;}
        if (!passed) continue;
        if (p->auto_svc&&p->auto_svc->detach) p->auto_svc->detach(sess);
        p->auto_svc=NULL;
        if (!auto_try(sess,chain[i])) continue;
        if (p->auto_mode==CORS_CORR_NEAR) return 0;
        if (p->auto_svc&&p->auto_svc->produce) {
            n=p->auto_svc->produce(sess,buf,max_len,nav);
            if (n>0) return n;
        }
    }
    return 0;
}

static int auto_sourcetable_str(const cors_corr_service_t *svc,
                                const cors_mountpoint_def_t *mntdef,
                                char *buf, int max_len)
{
    (void)svc;
    return snprintf(buf,max_len,"STR;%s;%s;RTCM3;2;GPS;;NONE;0;FRA;Auto-select best mode\n",
                    mntdef->name,mntdef->name);
}

const cors_corr_service_t cors_corr_service_auto={
    CORS_CORR_AUTO, "auto",
    auto_attach, auto_on_gga, auto_produce, auto_detach, auto_sourcetable_str
};
