/*------------------------------------------------------------------------------
 * corr_send.c : pack and push RTCM for FKP/MAC/VRS pull
 *-----------------------------------------------------------------------------*/
#include "corr_i.h"
#include "net_common.h"
#include "log.h"
#include <string.h>

int corr_pack_fkp(cors_corr_ctx_t *ctx, const double pos[3], const nav_t *nav,
                  char *buff, int max);
int corr_pack_mac(cors_corr_ctx_t *ctx, const double pos[3], const nav_t *nav,
                  char *buff, int max);

static int fkp_to_rtcm(const corr_fkp_data_t *fkp, rtcm_fkp_sat_t *out, int max)
{
    int i,n=fkp->nsat>max?max:fkp->nsat;

    for (i=0;i<n;i++) {
        out[i].prn=fkp->sat[i].prn;
        out[i].iod=fkp->sat[i].iod;
        out[i].gn0=fkp->sat[i].grad_n0;
        out[i].ge0=fkp->sat[i].grad_e0;
        out[i].gn1=fkp->sat[i].grad_n1;
        out[i].ge1=fkp->sat[i].grad_e1;
    }
    return n;
}

static int pack_sta1005(cors_corr_ctx_t *ctx, int srcid, char *buff, int max)
{
    sta_t sta={0};
    double pos[3];
    int nb;

    if (max<=0) return 0;
    if (!corr_srcinfo_pos(ctx,srcid,pos)) return 0;
    sta.staid=srcid;
    matcpy(sta.pos,pos,1,3);
    nb=rtcm_encode_sta(1005,&sta,buff);
    return nb>max?0:nb;
}

static int pack_master_obs(cors_corr_ctx_t *ctx, int srcid, const nav_t *nav,
                           char *buff, int max)
{
    cors_obsd_t *od;
    rtcm_t rtcm={0};
    int type[1]={1074},nb;

    if (!ctx||!ctx->cors||!nav||!buff||max<=0) return 0;
    HASH_FIND_INT(ctx->cors->obs.data,&srcid,od);
    if (!od||od->obs.n<=0) return 0;
    nb=rtcm_encode_obs(&rtcm,type,1,nav,od->obs.data,od->obs.n,buff);
    return nb>max?0:nb;
}

static int append_fkp_msgs(cors_corr_ctx_t *ctx, const double pos[3],
                           int staid, char *buff, int max)
{
    corr_fkp_data_t gps,glo;
    rtcm_fkp_sat_t rsat[CORR_FKP_MAXSAT];
    int nb=0,ns;

    if (corr_fkp_compute(ctx,pos,SYS_GPS,&gps)) {
        ns=fkp_to_rtcm(&gps,rsat,CORR_FKP_MAXSAT);
        nb+=rtcm_encode_fkp(1034,staid,rsat,ns,buff+nb);
    }
    if (corr_fkp_compute(ctx,pos,SYS_GLO,&glo)) {
        ns=fkp_to_rtcm(&glo,rsat,CORR_FKP_MAXSAT);
        nb+=rtcm_encode_fkp(1035,staid,rsat,ns,buff+nb);
    }
    return nb>max?0:nb;
}

int corr_pack_fkp(cors_corr_ctx_t *ctx, const double pos[3], const nav_t *nav,
                  char *buff, int max)
{
    corr_net_ctx_t net;
    int nb=0;
    int navtype[5]={1019,1020,1046,1042,1044};

    if (!ctx||!pos||!nav||!buff||max<=0) return 0;
    if (!corr_net_ctx_at_pos(ctx,pos,&net)) return 0;

    nb+=pack_sta1005(ctx,net.master_srcid,buff+nb,max-nb);
    nb+=append_fkp_msgs(ctx,pos,net.master_srcid,buff+nb,max-nb);
    if (max-nb>0) nb+=rtcm_encode_nav(navtype,nav,buff+nb);
    return nb>max?0:nb;
}

int corr_pack_mac(cors_corr_ctx_t *ctx, const double pos[3], const nav_t *nav,
                  char *buff, int max)
{
    corr_net_ctx_t net;
    int nb=0,i;
    int navtype[5]={1019,1020,1046,1042,1044};

    if (!ctx||!pos||!nav||!buff||max<=0) return 0;
    if (!corr_net_ctx_at_pos(ctx,pos,&net)) return 0;

    nb+=pack_sta1005(ctx,net.master_srcid,buff+nb,max-nb);
    nb+=pack_master_obs(ctx,net.master_srcid,nav,buff+nb,max-nb);
    for (i=0;i<2;i++) {
        if (net.aux_srcid[i]<=0) continue;
        nb+=pack_sta1005(ctx,net.aux_srcid[i],buff+nb,max-nb);
    }
    nb+=append_fkp_msgs(ctx,pos,net.master_srcid,buff+nb,max-nb);
    if (max-nb>0) nb+=rtcm_encode_nav(navtype,nav,buff+nb);
    return nb>max?0:nb;
}
