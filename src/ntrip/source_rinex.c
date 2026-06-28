/*------------------------------------------------------------------------------
 * source_rinex.c : RINEX OBS from ingested station streams (addsource)
 *
 * Records decoded observations from physical NTRIP sources (IPGP, OUIL, …),
 * not the RTCM stream sent to rover clients. One OBS file per source per UTC
 * day; closed on delsource or engine shutdown (partial day if before midnight).
 *-----------------------------------------------------------------------------*/
#include "source_rinex.h"
#include "corr.h"
#include "log.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef WIN32
#include <unistd.h>
#endif

typedef struct cors_source_rinex {
    int srcid;
    char name[64];
    rnxopt_t rnx_opt;
    cors_nav_t nav;
    FILE *fp_obs;
    int obs_hdr;
    gtime_t file_day;
    double appos[3];
    int has_appos;
    UT_hash_handle hh;
} cors_source_rinex_t;

static cors_source_rinex_t *g_src_rinex_tbl=NULL;
static uv_mutex_t g_src_rinex_lock;
static int g_src_rinex_lock_init=0;

static void src_rinex_lock_init(void)
{
    if (!g_src_rinex_lock_init) {
        uv_mutex_init(&g_src_rinex_lock);
        g_src_rinex_lock_init=1;
    }
}

static void src_rinex_lock(void)
{
    src_rinex_lock_init();
    uv_mutex_lock(&g_src_rinex_lock);
}

static void src_rinex_unlock(void)
{
    uv_mutex_unlock(&g_src_rinex_lock);
}

static gtime_t utc_day_start(gtime_t t)
{
    double ep[6]={0};

    if (t.time<=0) t=timeget();
    time2epoch(t,ep);
    ep[3]=ep[4]=ep[5]=0.0;
    return epoch2time(ep);
}

static void src_rinex_ensure_dir(const char *path)
{
    struct stat st;

    if (!path||!*path) return;
    if (stat(path,&st)==0&&S_ISDIR(st.st_mode)) return;
#ifdef WIN32
    _mkdir(path);
#else
    mkdir(path,0755);
#endif
}

static int src_rinex_mkpath(char *path, int npath, const char *dir,
                            const char *name, gtime_t day)
{
    double ep[6];

    time2epoch(day,ep);
    snprintf(path,(size_t)npath,"%.200s/%.60s_%04.0f%02.0f%02.0f.obs",
             dir,name,ep[0],ep[1],ep[2]);
    return 1;
}

static void src_rinex_close_obs(cors_source_rinex_t *rec)
{
    if (!rec||!rec->fp_obs) return;
    fclose(rec->fp_obs);
    rec->fp_obs=NULL;
    rec->obs_hdr=0;
    rec->file_day.time=0;
    log_trace(1,"source rinex: closed obs %s srcid=%d\n",rec->name,rec->srcid);
}

static int src_rinex_open_obs(cors_source_rinex_t *rec, gtime_t day)
{
    char path[512];
    struct stat st;
    int append=0;
    const cors_corr_cfg_t *cfg=cors_corr_cfg_get();

    if (!rec||!cfg) return 0;
    src_rinex_close_obs(rec);
    src_rinex_mkpath(path,sizeof(path),cfg->source_rinex_dir,rec->name,day);
    if (stat(path,&st)==0&&st.st_size>0) append=1;
    if (!(rec->fp_obs=fopen(path,append?"a":"w"))) {
        log_trace(1,"source rinex: open fail %s\n",path);
        return 0;
    }
    rec->file_day=day;
    rec->obs_hdr=append;
    log_trace(1,"source rinex: open obs %s (%s)\n",path,append?"append":"new");
    return 1;
}

static cors_source_rinex_t *src_rinex_get(int srcid, const char *name, int create)
{
    cors_source_rinex_t *rec=NULL;

    HASH_FIND_INT(g_src_rinex_tbl,&srcid,rec);
    if (rec||!create||!name||!*name) return rec;

    rec=calloc(1,sizeof(*rec));
    if (!rec) return NULL;
    rec->srcid=srcid;
    strncpy(rec->name,name,sizeof(rec->name)-1);
    HASH_ADD_INT(g_src_rinex_tbl,srcid,rec);
    log_trace(1,"source rinex: track srcid=%d name=%s\n",srcid,name);
    return rec;
}

static void src_rinex_set_appos(cors_source_rinex_t *rec, const double pos[3])
{
    if (!rec||!pos||norm((double*)pos,3)<=0.0) return;
    matcpy(rec->appos,pos,1,3);
    rec->has_appos=1;
}

static void src_rinex_ensure_day(cors_source_rinex_t *rec, gtime_t epoch)
{
    gtime_t day=utc_day_start(epoch);

    if (!rec->fp_obs||rec->file_day.time!=day.time) {
        src_rinex_open_obs(rec,day);
    }
}

static void src_rinex_write_obs(cors_source_rinex_t *rec, const obsd_t *obs, int n,
                                const nav_t *nav)
{
    const nav_t *nav_use=nav?nav:&rec->nav.data;

    if (!rec->fp_obs||!obs||n<=0) return;
    if (!rec->obs_hdr) {
        init_rnxopt(&rec->rnx_opt,obs,n);
        if (rec->has_appos) matcpy(rec->rnx_opt.apppos,rec->appos,1,3);
        snprintf(rec->rnx_opt.staid,sizeof(rec->rnx_opt.staid),"%.31s",rec->name);
        outrnxobsh(rec->fp_obs,&rec->rnx_opt,nav_use);
        rec->obs_hdr=1;
    }
    outrnxobsb(rec->fp_obs,&rec->rnx_opt,obs,n,0);
}

static void src_rinex_remove(cors_source_rinex_t *rec)
{
    if (!rec) return;
    src_rinex_close_obs(rec);
    HASH_DEL(g_src_rinex_tbl,rec);
    free(rec);
}

extern void cors_source_rinex_obs(int srcid, const char *name,
                                  const obsd_t *obs, int n,
                                  const nav_t *nav, const double pos[3])
{
    cors_source_rinex_t *rec;
    const cors_corr_cfg_t *cfg=cors_corr_cfg_get();
    gtime_t epoch;

    if (!cfg||!cfg->source_rinex_enable||!obs||n<=0||!name||!*name) return;

    src_rinex_lock();
    if (!g_src_rinex_tbl) src_rinex_ensure_dir(cfg->source_rinex_dir);
    rec=src_rinex_get(srcid,name,1);
    if (!rec) { src_rinex_unlock(); return; }
    if (pos) src_rinex_set_appos(rec,pos);
    epoch=obs[0].time;
    src_rinex_ensure_day(rec,epoch);
    src_rinex_write_obs(rec,obs,n,nav);
    src_rinex_unlock();
}

extern void cors_source_rinex_nav(int srcid, const nav_t *nav, int ephsat, int ephset)
{
    cors_source_rinex_t *rec;

    if (!nav||!cors_corr_cfg_get()->source_rinex_enable) return;
    src_rinex_lock();
    rec=src_rinex_get(srcid,NULL,0);
    if (rec) cors_updnav(&rec->nav,nav,ephsat,ephset);
    src_rinex_unlock();
}

extern void cors_source_rinex_sta(int srcid, const sta_t *sta)
{
    cors_source_rinex_t *rec;

    if (!sta||!cors_corr_cfg_get()->source_rinex_enable) return;
    src_rinex_lock();
    rec=src_rinex_get(srcid,NULL,0);
    if (rec&&norm(sta->pos,3)>0.0) src_rinex_set_appos(rec,sta->pos);
    src_rinex_unlock();
}

extern void cors_source_rinex_end(int srcid)
{
    cors_source_rinex_t *rec;

    src_rinex_lock();
    HASH_FIND_INT(g_src_rinex_tbl,&srcid,rec);
    if (rec) src_rinex_remove(rec);
    src_rinex_unlock();
}

extern void cors_source_rinex_end_by_name(const char *name)
{
    cors_source_rinex_t *rec,*t;

    if (!name||!*name) return;
    src_rinex_lock();
    HASH_ITER(hh,g_src_rinex_tbl,rec,t) {
        if (!strcmp(rec->name,name)) {
            src_rinex_remove(rec);
            break;
        }
    }
    src_rinex_unlock();
}

extern void cors_source_rinex_fini(void)
{
    cors_source_rinex_t *rec,*t;

    src_rinex_lock();
    HASH_ITER(hh,g_src_rinex_tbl,rec,t) src_rinex_remove(rec);
    src_rinex_unlock();
}

extern void cors_source_rinex_sync_disk(void)
{
    cors_source_rinex_t *rec,*t;

    if (!cors_corr_cfg_get()->source_rinex_enable) return;
    src_rinex_lock();
    HASH_ITER(hh,g_src_rinex_tbl,rec,t) {
        if (rec->fp_obs) fflush(rec->fp_obs);
    }
    src_rinex_unlock();
}
