/*------------------------------------------------------------------------------
 * monitor_rtcm.c: monitor RTCM data functions for CORS
 *
 * author  : sujinglan
 * version : $Revision: 1.1 $ $Date: 2008/07/17 21:48:06 $
 * history : 2022/11/17 1.0  new
 *-----------------------------------------------------------------------------*/
#include "cors.h"

static cors_monitor_rtcm_msg_t* new_monitor_rtcm_msg(cors_monitor_rtcm_msg_t **tbl, int srcid)
{
    cors_monitor_rtcm_msg_t *s=calloc(1,sizeof(cors_monitor_rtcm_msg_t));
    s->srcid=srcid;
    HASH_ADD_INT(*tbl,srcid,s);
    return s;
}

static void upd_rtcm_msg(cors_monitor_rtcm_msgs_t *msgs, const rtcm_t* rtcm, int srcid)
{
    if (!rtcm->moni_msg) return;
    cors_monitor_rtcm_msg_t *data;

    HASH_FIND_INT(msgs->msg,&srcid,data);
    if (!data&&!(data=new_monitor_rtcm_msg(&msgs->msg,srcid))) {
        return;
    }
    strcpy(data->msg[data->n++%MAX_RTCM_MSG],rtcm->msgtype);
}

static cors_monitor_rtcm_sta_t* new_monitor_rtcm_sta(cors_monitor_rtcm_sta_t **tbl, int srcid)
{
    cors_monitor_rtcm_sta_t *s=calloc(1,sizeof(cors_monitor_rtcm_sta_t));
    s->srcid=srcid;
    HASH_ADD_INT(*tbl,srcid,s);
    return s;
}

static void upd_rtcm_sta(cors_monitor_rtcm_stas_t *stas, const rtcm_t* rtcm, int srcid)
{
    if (rtcm->moni_msg<1005||rtcm->moni_msg>1008) return;

    cors_monitor_rtcm_sta_t *data;
    HASH_FIND_INT(stas->sta,&srcid,data);
    if (!data&&!(data=new_monitor_rtcm_sta(&stas->sta,srcid))) {
        return;
    }
    data->sta=rtcm->sta;
}

static cors_monitor_rtcm_fkp_t* new_monitor_rtcm_fkp(cors_monitor_rtcm_fkp_t **tbl, int srcid)
{
    cors_monitor_rtcm_fkp_t *s=calloc(1,sizeof(cors_monitor_rtcm_fkp_t));
    s->srcid=srcid;
    HASH_ADD_INT(*tbl,srcid,s);
    return s;
}

static void fkp_build_summary(cors_monitor_rtcm_fkp_t *data)
{
    char *p=data->summary;
    int rem=(int)sizeof(data->summary), n, i, max_show;
    const char *sys;

    sys=data->fkpsys==SYS_GLO?"GLO":"GPS";
    n=snprintf(p,rem,"FKP %s nsat=%d",sys,data->nfkp);
    if (n<=0||n>=rem) return;
    p+=n; rem-=n;

    max_show=data->nfkp>3?3:data->nfkp;
    for (i=0;i<max_show&&rem>32;i++) {
        n=snprintf(p,rem," prn=%d gn0=%.2f ge0=%.2f gn1=%.2f ge1=%.2f",
                   data->fkp[i].prn,data->fkp[i].gn0,data->fkp[i].ge0,
                   data->fkp[i].gn1,data->fkp[i].ge1);
        if (n<=0||n>=rem) break;
        p+=n; rem-=n;
    }
    if (data->nfkp>max_show&&rem>8) {
        snprintf(p,rem," +%d",data->nfkp-max_show);
    }
}

static void upd_rtcm_fkp(cors_monitor_rtcm_fkps_t *fkps, const rtcm_t* rtcm, int srcid)
{
    cors_monitor_rtcm_fkp_t *data;

    if (rtcm->moni_msg!=1034&&rtcm->moni_msg!=1035) return;
    if (rtcm->nfkp<=0) return;

    HASH_FIND_INT(fkps->fkp,&srcid,data);
    if (!data&&!(data=new_monitor_rtcm_fkp(&fkps->fkp,srcid))) {
        return;
    }
    data->fkpsys=rtcm->fkpsys;
    data->nfkp=rtcm->nfkp;
    memcpy(data->fkp,rtcm->fkp,sizeof(rtcm_fkp_sat_t)*rtcm->nfkp);
    fkp_build_summary(data);
}

extern void cors_monitor_rtcm(cors_monitor_rtcm_t *moni_rtcm, const rtcm_t* rtcm, int srcid)
{
    upd_rtcm_msg(&moni_rtcm->msgs,rtcm,srcid);
    upd_rtcm_sta(&moni_rtcm->stas,rtcm,srcid);
    upd_rtcm_fkp(&moni_rtcm->fkps,rtcm,srcid);
}

extern void cors_monitor_initrtcm(cors_monitor_rtcm_t *moni_rtcm)
{
    moni_rtcm->stas.sta=NULL;
    moni_rtcm->msgs.msg=NULL;
    moni_rtcm->fkps.fkp=NULL;
}

extern void cors_monitor_freertcm(cors_monitor_rtcm_t *moni_rtcm)
{
    cors_monitor_rtcm_msg_t *d,*tmp_msg;

    HASH_ITER(hh,moni_rtcm->msgs.msg,d,tmp_msg) {
        HASH_DEL(moni_rtcm->msgs.msg,d);
        free(d);
    }

    cors_monitor_rtcm_sta_t *s,*tmp_sta;
    HASH_ITER(hh,moni_rtcm->stas.sta,s,tmp_sta) {
        HASH_DEL(moni_rtcm->stas.sta,s);
        free(s);
    }

    cors_monitor_rtcm_fkp_t *f,*tmp_fkp;
    HASH_ITER(hh,moni_rtcm->fkps.fkp,f,tmp_fkp) {
        HASH_DEL(moni_rtcm->fkps.fkp,f);
        free(f);
    }
}

extern int cors_monitor_rtcm_msg(cors_monitor_rtcm_t *moni_rtcm, int srcid, char **msg_data)
{
    cors_monitor_rtcm_msg_t *d,tmp;

    HASH_FIND_INT(moni_rtcm->msgs.msg,&srcid,d);
    if (!d) return 0;
    tmp=*d;

    int i;
    for (i=0;i<MAX_RTCM_MSG;i++) {
        strcpy(msg_data[i],tmp.msg[i]);
    }
    return 1;
}

extern int cors_monitor_rtcm_sta(cors_monitor_rtcm_t *moni_rtcm, int srcid, sta_t *sta)
{
    cors_monitor_rtcm_sta_t *d;

    HASH_FIND_INT(moni_rtcm->stas.sta,&srcid,d);
    if (!d) return 0;
    *sta=d->sta; return 1;
}

extern int cors_monitor_rtcm_fkp_str(cors_monitor_rtcm_t *moni_rtcm, int srcid,
                                     char *buf, int buflen)
{
    cors_monitor_rtcm_fkp_t *d;

    if (!buf||buflen<=0) return 0;
    buf[0]='\0';
    HASH_FIND_INT(moni_rtcm->fkps.fkp,&srcid,d);
    if (!d||!d->summary[0]) return 0;
    strncpy(buf,d->summary,buflen-1);
    buf[buflen-1]='\0';
    return (int)strlen(buf);
}
