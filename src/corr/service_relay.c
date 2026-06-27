/*------------------------------------------------------------------------------
 * service_relay.c : relay physical base mountpoint (stub/minimal wrapper)
 *-----------------------------------------------------------------------------*/
#include "corr_i.h"
#include "log.h"

static int relay_attach(cors_corr_session_t *sess)
{
    cors_corr_sess_priv_t *p=corr_sess_priv(sess);
    log_trace(1,"corr relay attach: %s\n",sess->conn->mntpnt);
    if (sess->mntdef&&sess->mntdef->source[0]) {
        strncpy(p->out_mntpnt,sess->mntdef->source,sizeof(p->out_mntpnt)-1);
    }
    else strncpy(p->out_mntpnt,sess->conn->mntpnt,sizeof(p->out_mntpnt)-1);
    return 1;
}

static void relay_detach(cors_corr_session_t *sess)
{
    (void)sess;
}

static int relay_on_gga(cors_corr_session_t *sess, const double pos[3])
{
    (void)sess; (void)pos;
    return 0;
}

static int relay_produce(cors_corr_session_t *sess, uint8_t *buf, int max_len, const nav_t *nav)
{
    (void)sess; (void)buf; (void)max_len; (void)nav;
    return 0;
}

static int relay_sourcetable_str(const cors_corr_service_t *svc,
                                 const cors_mountpoint_def_t *mntdef,
                                 char *buf, int max_len)
{
    (void)svc;
    return snprintf(buf,max_len,"STR;%s;%s;%s;2;GPS;;NONE;0;FRA\n",
                    mntdef->name,mntdef->name,mntdef->format);
}

const cors_corr_service_t cors_corr_service_relay={
    CORS_CORR_RELAY, "relay",
    relay_attach, relay_on_gga, relay_produce, relay_detach, relay_sourcetable_str
};
