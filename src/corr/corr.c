/*------------------------------------------------------------------------------
 * corr.c : correction layer init, resolve, session lifecycle
 *-----------------------------------------------------------------------------*/
#include "corr.h"
#include "corr_i.h"
#include "net_common.h"
#include "supervision.h"
#include "policy.h"
#include "log.h"
#include "source_rinex.h"
#include "nav_rinex.h"
#include <stdio.h>
#include <string.h>

static cors_corr_ctx_t g_corr_ctx={0};
static cors_corr_registry_t g_corr_reg={0};

typedef struct cors_corr_conn {
    cors_ntrip_conn_t *conn;
    cors_corr_session_t sess;
    UT_hash_handle hh;
} cors_corr_conn_t;

static cors_corr_conn_t *g_conn_sess=NULL;

cors_corr_ctx_t *corr_global_ctx(void)
{
    return &g_corr_ctx;
}

cors_corr_sess_priv_t *corr_sess_priv(cors_corr_session_t *sess)
{
    if (!sess) return NULL;
    if (!sess->priv) {
        sess->priv=calloc(1,sizeof(cors_corr_sess_priv_t));
        ((cors_corr_sess_priv_t*)sess->priv)->ctx=&g_corr_ctx;
    }
    return (cors_corr_sess_priv_t*)sess->priv;
}

static const char *mode_names[]={
    "relay","near","vrs_fixed","vrs_dynamic","fkp","mac","imax","auto"
};

extern const char *cors_corr_mode_str(cors_corr_mode_t mode)
{
    if (mode<0||mode>=CORS_CORR_MODE_MAX) return "unknown";
    return mode_names[mode];
}

extern cors_corr_mode_t cors_corr_mode_parse(const char *str)
{
    if (!str||!*str) return CORS_CORR_RELAY;
    if (!strcmp(str,"relay")) return CORS_CORR_RELAY;
    if (!strcmp(str,"nearest")||!strcmp(str,"near")) return CORS_CORR_NEAR;
    if (!strcmp(str,"vrs_fixed")) return CORS_CORR_VRS_FIXED;
    if (!strcmp(str,"vrs_dynamic")||!strcmp(str,"vrs")) return CORS_CORR_VRS_DYNAMIC;
    if (!strcmp(str,"fkp")) return CORS_CORR_FKP;
    if (!strcmp(str,"mac")) return CORS_CORR_MAC;
    if (!strcmp(str,"imax")) return CORS_CORR_IMAX;
    if (!strcmp(str,"auto")) return CORS_CORR_AUTO;
    return CORS_CORR_RELAY;
}

extern cors_corr_mode_t cors_corr_mode_from_legacy(int conn_type, const char *mntpnt,
                                                   int is_vrs_fixed)
{
    (void)mntpnt;
    if (conn_type==CORS_CORR_LEGACY_TYPE_NEAR) return CORS_CORR_NEAR;
    if (is_vrs_fixed) return CORS_CORR_VRS_FIXED;
    if (conn_type==CORS_CORR_LEGACY_TYPE_RELAY) return CORS_CORR_RELAY;
    return CORS_CORR_RELAY;
}

extern int cors_corr_legacy_type_for_mode(cors_corr_mode_t mode)
{
    switch (mode) {
        case CORS_CORR_NEAR: return CORS_CORR_LEGACY_TYPE_NEAR;
        case CORS_CORR_RELAY:
        case CORS_CORR_VRS_FIXED: return CORS_CORR_LEGACY_TYPE_RELAY;
        default: return 3;
    }
}

extern const cors_corr_service_t *cors_corr_service_for_mode(cors_corr_mode_t mode)
{
    switch (mode) {
        case CORS_CORR_RELAY:       return &cors_corr_service_relay;
        case CORS_CORR_NEAR:        return &cors_corr_service_near;
        case CORS_CORR_VRS_DYNAMIC: return &cors_corr_service_vrs_dyn;
        case CORS_CORR_FKP:         return &cors_corr_service_fkp;
        case CORS_CORR_MAC:         return &cors_corr_service_mac;
        case CORS_CORR_IMAX:        return &cors_corr_service_imax;
        case CORS_CORR_AUTO:        return &cors_corr_service_auto;
        default: return NULL;
    }
}

extern const cors_mountpoint_def_t *
cors_corr_resolve_mountpoint(const cors_corr_ctx_t *ctx, const char *mntpnt,
                             int is_vrs_fixed, cors_mountpoint_def_t *scratch)
{
    const cors_mountpoint_def_t *m;

    if (!ctx||!mntpnt||!*mntpnt) return NULL;
    m=cors_corr_registry_find(ctx->registry,mntpnt);
    if (m) return m;
    if (is_vrs_fixed&&scratch) {
        memset(scratch,0,sizeof(*scratch));
        strncpy(scratch->name,mntpnt,sizeof(scratch->name)-1);
        scratch->mode=CORS_CORR_VRS_FIXED;
        scratch->legacy_type=CORS_CORR_LEGACY_TYPE_RELAY;
        return scratch;
    }
    return NULL;
}

extern int cors_corr_session_attach(cors_corr_session_t *sess,
                                    const cors_corr_ctx_t *ctx,
                                    cors_ntrip_conn_t *conn,
                                    const cors_mountpoint_def_t *mntdef)
{
    const cors_corr_service_t *svc;

    if (!sess||!ctx||!conn||!mntdef) return 0;
    svc=cors_corr_service_for_mode(mntdef->mode);
    if (!svc||!svc->attach) return 0;

    sess->conn=conn;
    sess->mntdef=mntdef;
    sess->req_mode=mntdef->mode;
    sess->mode=mntdef->mode;
    sess->svc=svc;
    if (!svc->attach(sess)) return 0;
    sess->attached=1;
    return 1;
}

extern int cors_corr_session_on_gga(cors_corr_session_t *sess, const double pos[3])
{
    if (!sess||!sess->svc||!sess->svc->on_gga) return 0;
    matcpy(sess->pos,pos,1,3);
    sess->time_gga=timeget();
    return sess->svc->on_gga(sess,pos);
}

extern int cors_corr_session_produce(cors_corr_session_t *sess, uint8_t *buf, int max_len,
                                     const nav_t *nav)
{
    if (!sess||!sess->svc||!sess->svc->produce) return 0;
    return sess->svc->produce(sess,buf,max_len,nav);
}

extern void cors_corr_session_detach(cors_corr_session_t *sess)
{
    if (!sess) return;
    if (sess->svc&&sess->svc->detach) sess->svc->detach(sess);
    if (sess->priv) {free(sess->priv); sess->priv=NULL;}
    sess->attached=0;
    sess->svc=NULL;
}

static int st_append(char *buf, int max_len, int *n, const char *line)
{
    int nb;

    if (!buf||!n||!line||*n>=max_len) return 0;
    nb=snprintf(buf+*n,max_len-*n,"%s",line);
    if (nb<=0||*n+nb>=max_len) return 0;
    *n+=nb;
    return nb;
}

static int user_allows_mode(const cors_ntrip_user_t *user, cors_corr_mode_t mode)
{
    cors_policy_mode_t pm;

    if (!user) return 1;
    switch (mode) {
        case CORS_CORR_RELAY:
        case CORS_CORR_NEAR:
            pm=CORR_MODE_RELAY;
            break;
        case CORS_CORR_VRS_FIXED:
            return (user->policy.allowed_modes&
                    (CORR_MODE_RELAY|CORR_MODE_VRS_DYNAMIC))!=0;
        case CORS_CORR_VRS_DYNAMIC:
            pm=CORR_MODE_VRS_DYNAMIC;
            break;
        case CORS_CORR_FKP:
            pm=CORR_MODE_FKP;
            break;
        case CORS_CORR_MAC:
            pm=CORR_MODE_MAC;
            break;
        case CORS_CORR_AUTO:
            pm=CORR_MODE_AUTO;
            break;
        case CORS_CORR_IMAX:
            pm=CORR_MODE_IMAX;
            break;
        default:
            return 0;
    }
    return (user->policy.allowed_modes&pm)!=0;
}

extern cors_corr_mode_t cors_corr_mode_for_mountpoint(const cors_corr_ctx_t *ctx,
                                                      const char *mntpnt,
                                                      int legacy_conn_type)
{
    cors_mountpoint_def_t scratch;
    const cors_mountpoint_def_t *mnt;
    cors_vrs_sta_t *vsta;

    if (legacy_conn_type==CORS_CORR_LEGACY_TYPE_NEAR) return CORS_CORR_NEAR;
    if (!ctx||!mntpnt||!*mntpnt) return CORS_CORR_RELAY;
    mnt=cors_corr_resolve_mountpoint(ctx,mntpnt,0,&scratch);
    if (mnt) return mnt->mode;
    if (ctx->cors&&ctx->cors->vrs.stas.data) {
        HASH_FIND_STR(ctx->cors->vrs.stas.data,mntpnt,vsta);
        if (vsta) return CORS_CORR_VRS_FIXED;
    }
    return CORS_CORR_RELAY;
}

extern const char *cors_corr_policy_result_str(cors_corr_policy_result_t result)
{
    switch (result) {
        case CORS_CORR_POLICY_OK:      return "ok";
        case CORS_CORR_POLICY_UNAUTH:  return "unauthorized";
        case CORS_CORR_POLICY_FORBID:  return "mode_not_allowed";
        case CORS_CORR_POLICY_REGION:  return "outside_region";
        case CORS_CORR_POLICY_QUOTA:   return "session_quota";
        case CORS_CORR_POLICY_UNKNOWN: return "unknown_mountpoint";
        default:                       return "denied";
    }
}

extern cors_corr_policy_result_t cors_corr_policy_check(const cors_corr_ctx_t *ctx,
                                                        const char *user,
                                                        cors_corr_mode_t mode,
                                                        const double pos[3],
                                                        int active_sessions)
{
    cors_ntrip_user_t *u=NULL;

    if (!ctx||!user||!*user) return CORS_CORR_POLICY_UNAUTH;
    if (mode<0||mode>=CORS_CORR_MODE_MAX) return CORS_CORR_POLICY_UNKNOWN;
    if (mode!=CORS_CORR_VRS_FIXED&&!cors_corr_service_for_mode(mode)) {
        return CORS_CORR_POLICY_UNKNOWN;
    }
    if (!ctx->agent) return CORS_CORR_POLICY_UNAUTH;
    HASH_FIND_STR(ctx->agent->user_tbl,user,u);
    if (!u) return CORS_CORR_POLICY_UNAUTH;
    if (!user_allows_mode(u,mode)) return CORS_CORR_POLICY_FORBID;
    if (pos&&norm((double*)pos,3)>0.0&&
        !cors_user_region_contains(&u->policy.region,pos)) {
        return CORS_CORR_POLICY_REGION;
    }
    if (active_sessions>=0&&!cors_policy_check_sessions(u,active_sessions)) {
        return CORS_CORR_POLICY_QUOTA;
    }
    return CORS_CORR_POLICY_OK;
}

static int append_mount_str(const cors_corr_ctx_t *ctx, const cors_mountpoint_def_t *mnt,
                            char *buf, int max_len, int *n)
{
    const cors_corr_service_t *svc;
    char line[512];
    int nb;

    (void)ctx;
    if (!mnt||!mnt->name[0]) return 0;
    if (mnt->mode==CORS_CORR_IMAX) return 0;
    svc=cors_corr_service_for_mode(mnt->mode);
    if (!svc&&mnt->mode==CORS_CORR_VRS_FIXED) svc=&cors_corr_service_relay;
    if (svc&&svc->sourcetable_str) {
        nb=svc->sourcetable_str(svc,mnt,line,sizeof(line));
        if (nb<=0) return 0;
        if (line[nb-1]!='\n') strncat(line,"\n",sizeof(line)-strlen(line)-1);
        return st_append(buf,max_len,n,line);
    }
    nb=snprintf(line,sizeof(line),"STR;%s;%s;%s;2;GPS;;NONE;0;FRA\n",
                mnt->name,mnt->name,mnt->format[0]?mnt->format:"RTCM3");
    return nb>0?st_append(buf,max_len,n,line):0;
}

static int append_mount_if_allowed(const cors_corr_ctx_t *ctx,
                                   const cors_ntrip_user_t *user,
                                   const cors_mountpoint_def_t *mnt,
                                   char *buf, int max_len, int *n)
{
    if (!user_allows_mode(user,mnt->mode)) return 0;
    return append_mount_str(ctx,mnt,buf,max_len,n);
}

static int registry_has_name(const cors_corr_registry_t *reg, const char *name)
{
    return reg&&name&&*name&&cors_corr_registry_find(reg,name);
}

extern int cors_corr_sourcetable_build(const cors_corr_ctx_t *ctx,
                                       const cors_ntrip_user_t *user,
                                       char *buf, int max_len)
{
    cors_mountpoint_def_t *m,*t;
    cors_ntrip_source_info_t *info,*it;
    cors_mountpoint_def_t scratch;
    int n=0;

    if (!ctx||!buf||max_len<=0) return 0;

    {
        char cas_line[256];
        snprintf(cas_line,sizeof(cas_line),
                 "CAS;%s;%d;CORS;CORS;0;FRA;0.00;0.00;1;0;cors-engine;none;B;N;0;0\n",
                 CORS_NTRIP_AGENT_HOST,CORS_NTRIP_AGENT_PORT);
        st_append(buf,max_len,&n,cas_line);
    }
    st_append(buf,max_len,&n,"NET;CORS;CORS;FRA;2;GPS+GLO+GAL;cors-engine;;;0;0\n");

    if (ctx->registry) {
        HASH_ITER(hh,ctx->registry->mnt_tbl,m,t) {
            append_mount_if_allowed(ctx,user,m,buf,max_len,&n);
            if (n>=max_len-256) break;
        }
    }

    if (ctx->cors&&ctx->cors->ntrip.info_tbl[0]) {
        HASH_ITER(hh,ctx->cors->ntrip.info_tbl[0],info,it) {
            if (!info->name[0]) continue;
            if (ctx->registry&&registry_has_name(ctx->registry,info->name)) continue;
            memset(&scratch,0,sizeof(scratch));
            strncpy(scratch.name,info->name,sizeof(scratch.name)-1);
            scratch.mode=CORS_CORR_RELAY;
            scratch.legacy_type=CORS_CORR_LEGACY_TYPE_RELAY;
            snprintf(scratch.format,sizeof(scratch.format),"%s","RTCM3");
            snprintf(scratch.desc,sizeof(scratch.desc),"Physical base %s",info->name);
            append_mount_if_allowed(ctx,user,&scratch,buf,max_len,&n);
            if (n>=max_len-256) break;
        }
    }

    if (ctx->cors) {
        cors_vrs_sta_t *vsta,*vt;
        HASH_ITER(hh,ctx->cors->vrs.stas.data,vsta,vt) {
            if (!vsta->name[0]) continue;
            if (ctx->registry&&registry_has_name(ctx->registry,vsta->name)) continue;
            if (ctx->cors->ntrip.info_tbl[0]) {
                HASH_FIND_STR(ctx->cors->ntrip.info_tbl[0],vsta->name,info);
                if (info) continue;
            }
            memset(&scratch,0,sizeof(scratch));
            strncpy(scratch.name,vsta->name,sizeof(scratch.name)-1);
            scratch.mode=CORS_CORR_VRS_FIXED;
            scratch.legacy_type=CORS_CORR_LEGACY_TYPE_RELAY;
            snprintf(scratch.format,sizeof(scratch.format),"%s","RTCM3_MSM7");
            snprintf(scratch.desc,sizeof(scratch.desc),"VRS fixed %s",vsta->name);
            append_mount_if_allowed(ctx,user,&scratch,buf,max_len,&n);
            if (n>=max_len-256) break;
        }
    }

    log_trace(1,"corr: sourcetable built %d bytes user=%s\n",n,user?user->user:"*");
    return n;
}

extern int cors_corr_init(cors_t *cors, cors_ntrip_agent_t *agent)
{
    const char *mountpoints_file;
    const char *corr_conf_file;

    if (!cors) return 0;
    mountpoints_file=cors->opt.mountpoints_file[0]?cors->opt.mountpoints_file:"conf/mountpoints";
    corr_conf_file=cors->opt.corr_conf_file[0]?cors->opt.corr_conf_file:"conf/corr.conf";

    cors_corr_registry_init(&g_corr_reg);
    corr_cfg_load(corr_conf_file);
    cors_corr_registry_load(&g_corr_reg,mountpoints_file);
    g_corr_ctx.agent=agent;
    g_corr_ctx.registry=&g_corr_reg;
    g_corr_ctx.cors=cors;
    cors_corr_supervision_init();
    log_trace(1,"corr: init ok\n");
    return 1;
}

extern void cors_corr_close(void)
{
    cors_corr_conn_t *c,*t;

    HASH_ITER(hh,g_conn_sess,c,t) {
        cors_corr_conn_end(c->conn);
    }
    cors_corr_supervision_fini();
    cors_corr_registry_free(&g_corr_reg);
    cors_source_rinex_fini();
    cors_nav_rinex_fini();
    memset(&g_corr_ctx,0,sizeof(g_corr_ctx));
}

static cors_corr_conn_t *corr_conn_find(cors_ntrip_conn_t *conn)
{
    cors_corr_conn_t *c;
    HASH_FIND_PTR(g_conn_sess,&conn,c);
    return c;
}

extern const cors_corr_ctx_t *cors_corr_ctx_get(void)
{
    return &g_corr_ctx;
}

extern int cors_corr_conn_begin(cors_ntrip_conn_t *conn, const cors_mountpoint_def_t *mntdef)
{
    cors_corr_conn_t *c;
    cors_corr_policy_result_t pr;

    if (!conn||!mntdef) return 0;
    if (corr_conn_find(conn)) return 1;
    if (conn->user[0]) {
        pr=cors_corr_policy_check(&g_corr_ctx,conn->user,mntdef->mode,conn->pos,-1);
        if (pr!=CORS_CORR_POLICY_OK) {
            log_trace(1,"corr: policy denied at attach user=%s mntpnt=%s reason=%s\n",
                      conn->user,conn->mntpnt,cors_corr_policy_result_str(pr));
            return 0;
        }
    }
    c=calloc(1,sizeof(*c));
    c->conn=conn;
    if (!cors_corr_session_attach(&c->sess,&g_corr_ctx,conn,mntdef)) {
        free(c);
        return 0;
    }
    HASH_ADD_PTR(g_conn_sess,conn,c);
    {
        char key[32];
        snprintf(key,sizeof(key),"%p",(void*)conn->conn);
        cors_corr_supervision_log(key,conn->mntpnt,cors_corr_mode_str(mntdef->mode),"attached");
    }
    return 1;
}

extern int cors_corr_conn_gga(cors_ntrip_conn_t *conn, const double *pos)
{
    cors_corr_conn_t *c=corr_conn_find(conn);
    if (!c) return 0;
    return cors_corr_session_on_gga(&c->sess,pos);
}

extern void cors_corr_conn_end(cors_ntrip_conn_t *conn)
{
    cors_corr_conn_t *c=corr_conn_find(conn);
    if (!c) return;
    cors_corr_session_detach(&c->sess);
    HASH_DEL(g_conn_sess,c);
    free(c);
}

extern int cors_corr_conn_output(cors_ntrip_conn_t *conn, char *mntpnt, int n)
{
    cors_corr_conn_t *c=corr_conn_find(conn);
    cors_corr_sess_priv_t *p;

    if (!c||!mntpnt||n<=0) return 0;
    p=corr_sess_priv(&c->sess);
    if (p&&p->out_mntpnt[0]) {
        strncpy(mntpnt,p->out_mntpnt,n-1);
        mntpnt[n-1]='\0';
        return 1;
    }
    strncpy(mntpnt,conn->mntpnt,n-1);
    mntpnt[n-1]='\0';
    return 1;
}

extern int cors_corr_conn_push(cors_ntrip_conn_t *conn)
{
    cors_corr_conn_t *c=corr_conn_find(conn);
    cors_corr_sess_priv_t *p;
    char buff[MAXSAT*512];
    const nav_t *nav;
    const char *mnt;
    int nb;

    if (!c||!g_corr_ctx.cors) return 0;
    nav=&g_corr_ctx.cors->nav.data;
    nb=cors_corr_session_produce(&c->sess,(uint8_t*)buff,sizeof(buff),nav);
    if (nb<=0) return 0;

    p=corr_sess_priv(&c->sess);
    mnt=conn->mntpnt;
    if (p&&p->out_mntpnt[0]) mnt=p->out_mntpnt;

    if (!g_corr_ctx.agent) return 0;
    return cors_ntrip_agent_send(g_corr_ctx.agent,mnt,buff,nb,nav);
}
