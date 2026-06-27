/*------------------------------------------------------------------------------
 * service_imax.c : Trimble iMAX stub (phase 2 — not supported)
 *-----------------------------------------------------------------------------*/
#include "corr_i.h"
#include "log.h"

static int imax_attach(cors_corr_session_t *sess)
{
    (void)sess;
    log_trace(1,"corr imax: not supported (phase 2)\n");
    return 0;
}

static void imax_detach(cors_corr_session_t *sess)
{
    (void)sess;
}

static int imax_on_gga(cors_corr_session_t *sess, const double pos[3])
{
    (void)sess; (void)pos;
    return 0;
}

static int imax_produce(cors_corr_session_t *sess, uint8_t *buf, int max_len, const nav_t *nav)
{
    (void)sess; (void)buf; (void)max_len; (void)nav;
    return 0;
}

static int imax_sourcetable_str(const cors_corr_service_t *svc,
                                const cors_mountpoint_def_t *mntdef,
                                char *buf, int max_len)
{
    (void)svc; (void)mntdef; (void)buf; (void)max_len;
    return 0;
}

const cors_corr_service_t cors_corr_service_imax={
    CORS_CORR_IMAX, "imax",
    imax_attach, imax_on_gga, imax_produce, imax_detach, imax_sourcetable_str
};
