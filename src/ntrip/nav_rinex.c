/*------------------------------------------------------------------------------
 * nav_rinex.c : combined mixed RINEX NAV — periodic flush + UTC midnight
 *
 * Accumulates broadcast ephemerides, then appends to CORS_YYYYMMDD.nav.
 * Periodic flush (configurable, max 24 h) clears RAM; midnight/shutdown
 * writes any remainder for the current UTC day.
 *-----------------------------------------------------------------------------*/
#include "nav_rinex.h"
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

#define NAV_RINEX_MAX_EPH   8192
#define NAV_RINEX_MAX_GEPH  2048

typedef struct {
    eph_t  eph[NAV_RINEX_MAX_EPH];
    int    n_eph;
    geph_t geph[NAV_RINEX_MAX_GEPH];
    int    n_geph;
    nav_t  meta;
    gtime_t day;
    gtime_t last_flush;
    int    active;
} cors_nav_rinex_acc_t;

static cors_nav_rinex_acc_t g_nav_rinex = {0};
static uv_mutex_t g_nav_rinex_lock;
static int g_nav_rinex_lock_init=0;

static void nav_rinex_lock_init(void)
{
    if (!g_nav_rinex_lock_init) {
        uv_mutex_init(&g_nav_rinex_lock);
        g_nav_rinex_lock_init=1;
    }
}

static void nav_rinex_lock(void)
{
    nav_rinex_lock_init();
    uv_mutex_lock(&g_nav_rinex_lock);
}

static void nav_rinex_unlock(void)
{
    uv_mutex_unlock(&g_nav_rinex_lock);
}

static gtime_t utc_day_start(gtime_t t)
{
    double ep[6]={0};

    if (t.time<=0) t=timeget();
    time2epoch(t,ep);
    ep[3]=ep[4]=ep[5]=0.0;
    return epoch2time(ep);
}

static void nav_rinex_ensure_dir(const char *path)
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

static double nav_rinex_flush_interval(void)
{
    const cors_corr_cfg_t *cfg=cors_corr_cfg_get();
    double sec;

    if (!cfg) return 0.0;
    sec=cfg->source_rinex_flush_sec;
    if (sec<=0.0) return 0.0;
    if (sec>CORS_RINEX_FLUSH_SEC_MAX) return CORS_RINEX_FLUSH_SEC_MAX;
    return sec;
}

static int eph_valid(const eph_t *e)
{
    return e&&e->sat>0&&(e->toc.time||e->toe.time||e->ttr.time);
}

static int geph_valid(const geph_t *g)
{
    return g&&g->sat>0&&(g->toe.time||g->tof.time);
}

static int eph_dup(const eph_t *a, const eph_t *b)
{
    if (a->sat!=b->sat) return 0;
    if (a->iode!=b->iode) return 0;
    return timediff(a->toe,b->toe)==0.0;
}

static int geph_dup(const geph_t *a, const geph_t *b)
{
    if (a->sat!=b->sat) return 0;
    if (a->iode!=b->iode) return 0;
    return timediff(a->toe,b->toe)==0.0;
}

static void nav_rinex_merge_meta(const nav_t *nav)
{
    int i;

    if (!nav) return;
    memcpy(g_nav_rinex.meta.ion_gps,nav->ion_gps,sizeof(nav->ion_gps));
    memcpy(g_nav_rinex.meta.ion_gal,nav->ion_gal,sizeof(nav->ion_gal));
    memcpy(g_nav_rinex.meta.ion_qzs,nav->ion_qzs,sizeof(nav->ion_qzs));
    memcpy(g_nav_rinex.meta.ion_cmp,nav->ion_cmp,sizeof(nav->ion_cmp));
    memcpy(g_nav_rinex.meta.ion_irn,nav->ion_irn,sizeof(nav->ion_irn));
    memcpy(g_nav_rinex.meta.utc_gps,nav->utc_gps,sizeof(nav->utc_gps));
    memcpy(g_nav_rinex.meta.utc_glo,nav->utc_glo,sizeof(nav->utc_glo));
    memcpy(g_nav_rinex.meta.utc_gal,nav->utc_gal,sizeof(nav->utc_gal));
    memcpy(g_nav_rinex.meta.utc_qzs,nav->utc_qzs,sizeof(nav->utc_qzs));
    memcpy(g_nav_rinex.meta.utc_cmp,nav->utc_cmp,sizeof(nav->utc_cmp));
    memcpy(g_nav_rinex.meta.utc_irn,nav->utc_irn,sizeof(nav->utc_irn));
    for (i=0;i<32;i++) {
        if (nav->glo_fcn[i]) g_nav_rinex.meta.glo_fcn[i]=nav->glo_fcn[i];
    }
}

static int nav_rinex_add_eph(const eph_t *eph)
{
    int i;

    if (!eph_valid(eph)) return 0;
    for (i=0;i<g_nav_rinex.n_eph;i++) {
        if (eph_dup(&g_nav_rinex.eph[i],eph)) return 0;
    }
    if (g_nav_rinex.n_eph>=NAV_RINEX_MAX_EPH) {
        log_trace(1,"nav rinex: eph buffer full\n");
        return 0;
    }
    g_nav_rinex.eph[g_nav_rinex.n_eph++]=*eph;
    return 1;
}

static int nav_rinex_add_geph(const geph_t *geph)
{
    int i;

    if (!geph_valid(geph)) return 0;
    for (i=0;i<g_nav_rinex.n_geph;i++) {
        if (geph_dup(&g_nav_rinex.geph[i],geph)) return 0;
    }
    if (g_nav_rinex.n_geph>=NAV_RINEX_MAX_GEPH) {
        log_trace(1,"nav rinex: geph buffer full\n");
        return 0;
    }
    g_nav_rinex.geph[g_nav_rinex.n_geph++]=*geph;
    return 1;
}

static void nav_rinex_clear_buffer(void)
{
    g_nav_rinex.n_eph=0;
    g_nav_rinex.n_geph=0;
}

static void nav_rinex_reset_acc(void)
{
    memset(&g_nav_rinex.meta,0,sizeof(g_nav_rinex.meta));
    nav_rinex_clear_buffer();
}

static void nav_rinex_init_rnxopt(rnxopt_t *opt)
{
    memset(opt,0,sizeof(*opt));
    opt->rnxver=304;
    opt->navsys=SYS_ALL;
    opt->sep_nav=0;
    opt->outiono=1;
    opt->outtime=1;
    opt->outleaps=1;
    strncpy(opt->prog,"cors-engine",sizeof(opt->prog)-1);
    strncpy(opt->runby,"CORS",sizeof(opt->runby)-1);
    strncpy(opt->staid,"CORS",sizeof(opt->staid)-1);
}

static void nav_rinex_mkpath(gtime_t day, char *path, int npath)
{
    const cors_corr_cfg_t *cfg=cors_corr_cfg_get();
    double ep[6];

    time2epoch(day,ep);
    snprintf(path,(size_t)npath,"%.200s/CORS_%04.0f%02.0f%02.0f.nav",
             cfg->source_rinex_dir,ep[0],ep[1],ep[2]);
}

static int nav_rinex_file_nonempty(const char *path)
{
    struct stat st;

    return stat(path,&st)==0&&st.st_size>0;
}

/* Append in-memory buffer to day file; clear buffer on success */
static int nav_rinex_flush_buffer(gtime_t day, const char *reason)
{
    const cors_corr_cfg_t *cfg=cors_corr_cfg_get();
    rnxopt_t opt;
    char path[512];
    FILE *fp;
    int i,sys,nw=0,append=0;

    if (!cfg||!cfg->source_rinex_enable) return 0;
    if (g_nav_rinex.n_eph<=0&&g_nav_rinex.n_geph<=0) return 0;

    nav_rinex_mkpath(day,path,sizeof(path));
    nav_rinex_ensure_dir(cfg->source_rinex_dir);
    append=nav_rinex_file_nonempty(path);
    if (!(fp=fopen(path,append?"a":"w"))) {
        log_trace(1,"nav rinex: open fail %s\n",path);
        return 0;
    }

    nav_rinex_init_rnxopt(&opt);
    if (!append) {
        if (!outrnxnavh(fp,&opt,&g_nav_rinex.meta)) {
            fclose(fp);
            return 0;
        }
    }
    for (i=0;i<g_nav_rinex.n_eph;i++) {
        sys=satsys(g_nav_rinex.eph[i].sat,NULL);
        if (sys==SYS_GLO) continue;
        if (outrnxnavb(fp,&opt,&g_nav_rinex.eph[i])) nw++;
    }
    for (i=0;i<g_nav_rinex.n_geph;i++) {
        if (outrnxgnavb(fp,&opt,&g_nav_rinex.geph[i])) nw++;
    }
    fclose(fp);
    log_trace(1,"nav rinex: %s %s records=%d eph=%d geph=%d\n",
              append?"append":"write",path,nw,g_nav_rinex.n_eph,g_nav_rinex.n_geph);
    (void)reason;
    nav_rinex_clear_buffer();
    return nw>0;
}

static void nav_rinex_check_periodic_locked(void)
{
    double interval;
    gtime_t now;

    interval=nav_rinex_flush_interval();
    if (interval<=0.0||!g_nav_rinex.active) return;

    now=timeget();
    if (g_nav_rinex.last_flush.time<=0) {
        g_nav_rinex.last_flush=now;
        return;
    }
    if (timediff(now,g_nav_rinex.last_flush)<interval) return;

    nav_rinex_flush_buffer(g_nav_rinex.day,"periodic");
    cors_source_rinex_sync_disk();
    g_nav_rinex.last_flush=now;
}

static void nav_rinex_check_day_locked(void)
{
    gtime_t today;

    today=utc_day_start(timeget());
    if (g_nav_rinex.active&&g_nav_rinex.day.time!=today.time) {
        nav_rinex_flush_buffer(g_nav_rinex.day,"midnight");
        nav_rinex_reset_acc();
        g_nav_rinex.day=today;
        g_nav_rinex.last_flush=timeget();
    }
    else if (!g_nav_rinex.active) {
        g_nav_rinex.day=today;
        g_nav_rinex.active=1;
        g_nav_rinex.last_flush=timeget();
        nav_rinex_ensure_dir(cors_corr_cfg_get()->source_rinex_dir);
    }
    nav_rinex_check_periodic_locked();
}

extern void cors_nav_rinex_check_day(void)
{
    if (!cors_corr_cfg_get()->source_rinex_enable) return;
    nav_rinex_lock();
    nav_rinex_check_day_locked();
    nav_rinex_unlock();
}

extern void cors_nav_rinex_feed(const nav_t *nav, int ephsat, int ephset)
{
    const eph_t *eph;
    const geph_t *geph;
    int prn;

    if (!nav||!cors_corr_cfg_get()->source_rinex_enable) return;

    nav_rinex_lock();
    nav_rinex_check_day_locked();

    if (satsys(ephsat,&prn)!=SYS_GLO) {
        eph=nav->eph+ephsat-1+MAXSAT*ephset;
        nav_rinex_add_eph(eph);
    }
    else {
        geph=nav->geph+prn-1;
        nav_rinex_add_geph(geph);
    }
    nav_rinex_merge_meta(nav);
    nav_rinex_unlock();
}

extern void cors_nav_rinex_fini(void)
{
    nav_rinex_lock();
    if (g_nav_rinex.active) {
        nav_rinex_flush_buffer(g_nav_rinex.day,"shutdown");
        cors_source_rinex_sync_disk();
    }
    nav_rinex_reset_acc();
    g_nav_rinex.active=0;
    g_nav_rinex.day.time=0;
    g_nav_rinex.last_flush.time=0;
    nav_rinex_unlock();
}
