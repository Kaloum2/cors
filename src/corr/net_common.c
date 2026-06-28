/*------------------------------------------------------------------------------
 * net_common.c : shared network helpers for correction modes
 *-----------------------------------------------------------------------------*/
#include "net_common.h"
#include "log.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define SQR(x) ((x)*(x))

static cors_corr_cfg_t g_corr_cfg={
    1000.0,  /* vrs_dynamic_move_thresh_m */
    2,       /* auto_vrs_min_fixed_bls */
    3,       /* auto_mac_min_fixed_bls */
    3,       /* auto_fkp_min_stations */
    1,       /* source_rinex_enable */
    "build/source_rinex"
};

const cors_corr_cfg_t *corr_cfg_get(void)
{
    return &g_corr_cfg;
}

const cors_corr_cfg_t *cors_corr_cfg_get(void)
{
    return &g_corr_cfg;
}

extern int corr_cfg_load(const char *file)
{
    char buff[256],key[64],val[128],*p;
    FILE *fp;

    if (!file||!*file) return 0;
    if (!(fp=fopen(file,"r"))) {
        log_trace(1,"corr: config not found %s (defaults)\n",file);
        return 0;
    }
    while (fgets(buff,sizeof(buff),fp)) {
        if ((p=strchr(buff,'#'))) *p='\0';
        if (sscanf(buff," %63[^=] = %127s ",key,val)<2) continue;
        if (!strcmp(key,"vrs-dynamic-move-thresh-m")) {
            g_corr_cfg.vrs_dynamic_move_thresh_m=atof(val);
        }
        else if (!strcmp(key,"auto-vrs-min-fixed-bls")) {
            g_corr_cfg.auto_vrs_min_fixed_bls=atoi(val);
        }
        else if (!strcmp(key,"auto-mac-min-fixed-bls")) {
            g_corr_cfg.auto_mac_min_fixed_bls=atoi(val);
        }
        else if (!strcmp(key,"auto-fkp-min-stations")) {
            g_corr_cfg.auto_fkp_min_stations=atoi(val);
        }
        else if (!strcmp(key,"source-rinex-enable")) {
            g_corr_cfg.source_rinex_enable=atoi(val);
        }
        else if (!strcmp(key,"source-rinex-dir")) {
            strncpy(g_corr_cfg.source_rinex_dir,val,sizeof(g_corr_cfg.source_rinex_dir)-1);
            g_corr_cfg.source_rinex_dir[sizeof(g_corr_cfg.source_rinex_dir)-1]='\0';
        }
        else if (!strcmp(key,"session-rinex-enable")) {
            g_corr_cfg.source_rinex_enable=atoi(val);
        }
        else if (!strcmp(key,"session-rinex-dir")) {
            strncpy(g_corr_cfg.source_rinex_dir,val,sizeof(g_corr_cfg.source_rinex_dir)-1);
            g_corr_cfg.source_rinex_dir[sizeof(g_corr_cfg.source_rinex_dir)-1]='\0';
        }
    }
    fclose(fp);
    log_trace(1,"corr cfg: vrs_move=%.0fm vrs_bl=%d mac_bl=%d fkp_sta=%d src_rinex=%d dir=%s\n",
              g_corr_cfg.vrs_dynamic_move_thresh_m,
              g_corr_cfg.auto_vrs_min_fixed_bls,
              g_corr_cfg.auto_mac_min_fixed_bls,
              g_corr_cfg.auto_fkp_min_stations,
              g_corr_cfg.source_rinex_enable,
              g_corr_cfg.source_rinex_dir);
    return 1;
}

static cors_ntrip_source_info_t *srcinfo_by_id(cors_corr_ctx_t *ctx, int srcid)
{
    cors_ntrip_source_info_t *s;

    if (!ctx||!ctx->cors) return NULL;
    HASH_FIND(ii,ctx->cors->ntrip.info_tbl[1],&srcid,sizeof(int),s);
    return s;
}

int corr_srcinfo_name(cors_corr_ctx_t *ctx, int srcid, char *name, int n)
{
    cors_ntrip_source_info_t *s=srcinfo_by_id(ctx,srcid);

    if (!s||!name||n<=0) return 0;
    strncpy(name,s->name,n-1);
    name[n-1]='\0';
    return 1;
}

int corr_srcinfo_pos(cors_corr_ctx_t *ctx, int srcid, double pos[3])
{
    cors_ntrip_source_info_t *s=srcinfo_by_id(ctx,srcid);

    if (!s||!pos) return 0;
    matcpy(pos,s->pos,1,3);
    return 1;
}

int corr_pos_in_dtrig(cors_corr_ctx_t *ctx, const double pos[3])
{
    if (!ctx||!ctx->cors) return 0;
    return cors_vrs_pos_in_dtrig(&ctx->cors->vrs,pos);
}

const cors_dtrig_t *corr_dtrig_at_pos(cors_corr_ctx_t *ctx, const double pos[3])
{
    if (!ctx||!ctx->cors) return NULL;
    return cors_vrs_dtrig_at_pos(&ctx->cors->vrs,pos);
}

int corr_nearest_physical(cors_corr_ctx_t *ctx, const double pos[3], char *name, int n)
{
    struct kdres *res;
    cors_ntrip_source_info_t *info;

    if (!ctx||!ctx->cors||!name||n<=0||norm((double*)pos,3)<=0.0) return 0;
    if (!ctx->cors->ntrip.src_kdtree) return 0;
    if (!(res=kd_nearest(ctx->cors->ntrip.src_kdtree,pos))) return 0;
    info=kd_res_item_data(res);
    if (!info) return 0;
    strncpy(name,info->name,n-1);
    name[n-1]='\0';
    return 1;
}

static cors_baseline_t *find_baseline(cors_srtk_t *srtk, int base, int rover)
{
    cors_baseline_t *bl;
    char id[32];

    snprintf(id,sizeof(id),"%d->%d",base,rover);
    HASH_FIND_STR(srtk->bls.data,id,bl);
    if (bl) return bl;
    snprintf(id,sizeof(id),"%d->%d",rover,base);
    HASH_FIND_STR(srtk->bls.data,id,bl);
    return bl;
}

static int baseline_is_fixed(const cors_baseline_t *bl)
{
    if (!bl) return 0;
    return bl->rtk.sol.stat==SOLQ_FIX;
}

int corr_dtrig_fixed_baseline_count(cors_corr_ctx_t *ctx, const cors_dtrig_t *d)
{
    cors_srtk_t *srtk;
    cors_baseline_t *bl;
    int i,j,n=0;

    if (!ctx||!ctx->cors||!d) return 0;
    srtk=&ctx->cors->srtk;
    for (i=0;i<3;i++) {
        for (j=i+1;j<3;j++) {
            if (!d->vt[i]||!d->vt[j]) continue;
            bl=find_baseline(srtk,d->vt[i]->srcid,d->vt[j]->srcid);
            if (baseline_is_fixed(bl)) n++;
        }
    }
    return n;
}

int corr_net_ctx_at_pos(cors_corr_ctx_t *ctx, const double pos[3], corr_net_ctx_t *out)
{
    const cors_dtrig_t *d;
    cors_ntrip_source_info_t *info;
    double dr[3],dmin=-1.0;
    int i,k,m=-1;

    if (!out) return 0;
    memset(out,0,sizeof(*out));
    d=corr_dtrig_at_pos(ctx,pos);
    if (!d) return 0;

    out->dtrig=d;
    out->n_physical=3;
    out->n_fixed_bls=corr_dtrig_fixed_baseline_count(ctx,d);

    for (i=0;i<3;i++) {
        if (!d->vt[i]) continue;
        for (k=0;k<3;k++) dr[k]=pos[k]-d->vt[i]->pos[k];
        if (m<0||norm(dr,3)<dmin) {
            dmin=norm(dr,3);
            m=i;
        }
    }
    if (m<0) return 0;

    out->master_srcid=d->vt[m]->srcid;
    corr_srcinfo_name(ctx,out->master_srcid,out->master_name,sizeof(out->master_name));

    for (i=k=0;i<3&&k<2;i++) {
        if (i==m||!d->vt[i]) continue;
        out->aux_srcid[k]=d->vt[i]->srcid;
        info=srcinfo_by_id(ctx,out->aux_srcid[k]);
        if (info) {
            strncpy(out->aux_name[k],info->name,sizeof(out->aux_name[k])-1);
        }
        k++;
    }
    return 1;
}

static int fit_plane_gradients(const double ref_ecef[3], rtk_t **rtk, int n,
                               int sat, int frq, double *gn, double *ge)
{
    double *H,*v,e[3],dr[3],pos[3],age=30.0;
    int i,k,m,info,c[3];
    gtime_t t0={0};

    *gn=*ge=0.0;
    if (n<2) return 0;
    matcpy(pos,ref_ecef,1,3);
    ecef2pos(pos,pos);
    H=mat(n,3); v=mat(n,1);

    for (m=i=0;i<n;i++) {
        if (!rtk[i]) continue;
        if (!t0.time) t0=rtk[i]->time;
        if (fabs(timediff(t0,rtk[i]->time))>age) continue;
        if (!rtk[i]->ssat[sat-1].vsat[frq]) continue;
        if (rtk[i]->ssat[sat-1].slip[frq]) continue;
        if (rtk[i]->ssat[sat-1].fix[frq]<2) continue;
        if (norm(rtk[i]->bl,3)<=0.0) continue;
        for (k=0;k<3;k++) dr[k]=rtk[i]->bl[k];
        ecef2enu(pos,dr,e);
        H[3*m]=e[0];
        H[3*m+1]=e[1];
        H[3*m+2]=1.0;
        v[m]=rtk[i]->ssat[sat-1].resc[frq];
        m++;
    }
    if (m<2) { free(H); free(v); return 0; }
    info=lsq(H,v,3,m,c,NULL);
    free(H); free(v);
    if (info) return 0;
    *gn=c[0]*1E6/CLIGHT;
    *ge=c[1]*1E6/CLIGHT;
    return 1;
}

static int collect_dtrig_rtk(const cors_dtrig_t *d, cors_srtk_t *srtk, rtk_t **rtk, int *n)
{
    cors_baseline_t *bl;
    int i,i2,cnt=0;

    for (i=0;i<3;i++) {
        i2=(i+1)%3;
        if (!d->vt[i]||!d->vt[i2]) continue;
        bl=find_baseline(srtk,d->vt[i]->srcid,d->vt[i2]->srcid);
        if (!bl||!baseline_is_fixed(bl)) continue;
        rtk[cnt++]=&bl->rtk;
    }
    *n=cnt;
    return cnt>=2;
}

int corr_fkp_compute(cors_corr_ctx_t *ctx, const double pos[3], int sys,
                     corr_fkp_data_t *fkp)
{
    corr_net_ctx_t net;
    cors_srtk_t *srtk;
    rtk_t *rtk[3];
    int nrtk,i,prn,sat,ns=0;
    double gn,ge,refpos[3];

    if (!ctx||!ctx->cors||!pos||!fkp) return 0;
    memset(fkp,0,sizeof(*fkp));
    if (!corr_net_ctx_at_pos(ctx,pos,&net)) return 0;

    srtk=&ctx->cors->srtk;
    fkp->staid=net.master_srcid;
    if (!corr_srcinfo_pos(ctx,net.master_srcid,refpos)) {
        matcpy(refpos,net.dtrig->vt[0]->pos,1,3);
    }
    if (!collect_dtrig_rtk(net.dtrig,srtk,rtk,&nrtk)) return 0;

    for (sat=1;sat<=MAXSAT&&ns<CORR_FKP_MAXSAT;sat++) {
        if (satsys(sat,&prn)!=sys) continue;
        if (!fit_plane_gradients(refpos,rtk,nrtk,sat,0,&gn,&ge)) continue;
        fkp->sat[ns].prn=prn;
        fkp->sat[ns].iod=0;
        fkp->sat[ns].grad_n0=gn*0.1;
        fkp->sat[ns].grad_e0=ge*0.1;
        fkp->sat[ns].grad_n1=gn;
        fkp->sat[ns].grad_e1=ge;
        ns++;
    }
    fkp->nsat=ns;
    return ns>0;
}

int corr_eligible_vrs_dynamic(cors_corr_ctx_t *ctx, const double pos[3])
{
    const cors_dtrig_t *d;

    if (!corr_pos_in_dtrig(ctx,pos)) return 0;
    d=corr_dtrig_at_pos(ctx,pos);
    return corr_dtrig_fixed_baseline_count(ctx,d)>=g_corr_cfg.auto_vrs_min_fixed_bls;
}

int corr_eligible_mac(cors_corr_ctx_t *ctx, const double pos[3])
{
    corr_net_ctx_t net;

    if (!corr_net_ctx_at_pos(ctx,pos,&net)) return 0;
    return net.n_physical>=g_corr_cfg.auto_fkp_min_stations&&
           net.n_fixed_bls>=g_corr_cfg.auto_mac_min_fixed_bls;
}

int corr_eligible_fkp(cors_corr_ctx_t *ctx, const double pos[3])
{
    corr_net_ctx_t net;

    if (!corr_net_ctx_at_pos(ctx,pos,&net)) return 0;
    return net.n_physical>=g_corr_cfg.auto_fkp_min_stations&&
           net.n_fixed_bls>=2;
}
