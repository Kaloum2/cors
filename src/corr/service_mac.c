/*------------------------------------------------------------------------------
 * service_mac.c : Master-Auxiliary corrections
 *-----------------------------------------------------------------------------*/
#include "corr_i.h"
#include "net_common.h"
#include "log.h"

static int mac_attach(cors_corr_session_t *sess)
{
    strncpy(corr_sess_priv(sess)->out_mntpnt,sess->conn->mntpnt,
            sizeof(corr_sess_priv(sess)->out_mntpnt)-1);
    log_trace(1,"corr mac attach: %s allow_float=%d\n",
              sess->conn->mntpnt,corr_sess_allow_float(sess));
    return 1;
}

static void mac_detach(cors_corr_session_t *sess)
{
    (void)sess;
}

static int mac_on_gga(cors_corr_session_t *sess, const double pos[3])
{
    corr_net_ctx_t net;
    cors_corr_sess_priv_t *p=corr_sess_priv(sess);
    int allow_float=corr_sess_allow_float(sess);

    if (!corr_eligible_mac(corr_global_ctx(),pos,allow_float)) {
        log_trace(1,"corr mac: rejected (network not ready) %s\n",sess->conn->mntpnt);
        return 0;
    }
    if (!corr_net_ctx_at_pos(corr_global_ctx(),pos,&net)) return 0;
    strncpy(p->out_mntpnt,sess->conn->mntpnt,sizeof(p->out_mntpnt)-1);
    log_trace(1,"corr mac on_gga: master=%s aux=%s,%s\n",net.master_name,
              net.aux_name[0],net.aux_name[1]);
    return 1;
}

static int mac_produce(cors_corr_session_t *sess, uint8_t *buf, int max_len, const nav_t *nav)
{
    int allow_float=corr_sess_allow_float(sess);

    if (norm(sess->pos,3)<=0.0) return 0;
    if (!corr_eligible_mac(corr_global_ctx(),sess->pos,allow_float)) return 0;
    return corr_pack_mac(corr_global_ctx(),sess->pos,nav,(char*)buf,max_len,allow_float);
}

static int mac_sourcetable_str(const cors_corr_service_t *svc,
                               const cors_mountpoint_def_t *mntdef,
                               char *buf, int max_len)
{
    (void)svc;
    return snprintf(buf,max_len,"STR;%s;%s;RTCM3;1005(1),1074(1),1034(1);2;GPS+GLO;;NONE;0;FRA;MAC network\n",
                    mntdef->name,mntdef->name);
}

const cors_corr_service_t cors_corr_service_mac={
    CORS_CORR_MAC, "mac",
    mac_attach, mac_on_gga, mac_produce, mac_detach, mac_sourcetable_str
};
