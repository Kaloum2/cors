/*------------------------------------------------------------------------------
 * service_fkp.c : FKP corrections (RTCM 1034/1035)
 *-----------------------------------------------------------------------------*/
#include "corr_i.h"
#include "net_common.h"
#include "log.h"

static int fkp_attach(cors_corr_session_t *sess)
{
    cors_corr_sess_priv_t *p=corr_sess_priv(sess);

    if (!corr_eligible_fkp(corr_global_ctx(),sess->pos)) {
        log_trace(1,"corr fkp attach deferred (await GGA)\n");
    }
    strncpy(p->out_mntpnt,sess->conn->mntpnt,sizeof(p->out_mntpnt)-1);
    log_trace(1,"corr fkp attach: %s\n",sess->conn->mntpnt);
    return 1;
}

static void fkp_detach(cors_corr_session_t *sess)
{
    (void)sess;
}

static int fkp_on_gga(cors_corr_session_t *sess, const double pos[3])
{
    if (!corr_eligible_fkp(corr_global_ctx(),pos)) {
        log_trace(1,"corr fkp: rejected (network not ready) %s\n",sess->conn->mntpnt);
        return 0;
    }
    log_trace(1,"corr fkp on_gga: %s nsat ready\n",sess->conn->mntpnt);
    return 1;
}

static int fkp_produce(cors_corr_session_t *sess, uint8_t *buf, int max_len, const nav_t *nav)
{
    if (norm(sess->pos,3)<=0.0) return 0;
    if (!corr_eligible_fkp(corr_global_ctx(),sess->pos)) return 0;
    return corr_pack_fkp(corr_global_ctx(),sess->pos,nav,(char*)buf,max_len);
}

static int fkp_sourcetable_str(const cors_corr_service_t *svc,
                               const cors_mountpoint_def_t *mntdef,
                               char *buf, int max_len)
{
    (void)svc;
    return snprintf(buf,max_len,"STR;%s;%s;RTCM3;1005(1),1034(1),1035(1);2;GPS+GLO;;NONE;0;FRA;FKP network\n",
                    mntdef->name,mntdef->name);
}

const cors_corr_service_t cors_corr_service_fkp={
    CORS_CORR_FKP, "fkp",
    fkp_attach, fkp_on_gga, fkp_produce, fkp_detach, fkp_sourcetable_str
};
