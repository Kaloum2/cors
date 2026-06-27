/*------------------------------------------------------------------------------
 * policy.c : user policy parsing and authorization checks
 *
 * See doc/dev/security-modes.md and conf/agentusers.example.
 *-----------------------------------------------------------------------------*/
#include "cors.h"
#include "policy.h"
#include "log.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *s)
{
    char *e;

    while (*s&&isspace((unsigned char)*s)) s++;
    if (!*s) return s;
    e=s+strlen(s)-1;
    while (e>s&&isspace((unsigned char)*e)) *e--='\0';
    return s;
}

static int streq_wild(const char *s)
{
    return !s||!*s||!strcmp(s,"*");
}

static int parse_modes(const char *modes_str, cors_policy_mode_t *out)
{
    char buf[256],*p,*q,*tok;
    cors_policy_mode_t mask=0;

    if (streq_wild(modes_str)) {
        *out=CORR_MODE_ALL;
        return 1;
    }
    strncpy(buf,modes_str,sizeof(buf)-1);
    buf[sizeof(buf)-1]='\0';

    for (p=buf;;p=NULL) {
        tok=strtok_r(p,"|",&q);
        if (!tok) break;
        tok=trim(tok);
        if (!*tok) continue;
        mask|=cors_corr_mode_from_name(tok);
    }
    if (!mask) return 0;
    *out=mask;
    return 1;
}

static int parse_bbox(const char *spec, cors_user_region_t *region)
{
    double v[4];
    int n=0;
    char buf[256],*p,*q,*tok;

    strncpy(buf,spec,sizeof(buf)-1);
    buf[sizeof(buf)-1]='\0';
    for (p=buf;n<4;p=NULL) {
        tok=strtok_r(p,",",&q);
        if (!tok) break;
        v[n++]=atof(trim(tok));
    }
    if (n!=4) return 0;

    region->type=CORS_REGION_BBOX;
    region->u.bbox.lat_min=v[0];
    region->u.bbox.lon_min=v[1];
    region->u.bbox.lat_max=v[2];
    region->u.bbox.lon_max=v[3];
    return 1;
}

static int parse_polygon(const char *spec, cors_user_region_t *region)
{
    char buf[4096],*p,*q,*pair,*lat_tok,*lon_tok;
    int n=0;

    strncpy(buf,spec,sizeof(buf)-1);
    buf[sizeof(buf)-1]='\0';

    for (p=buf;;p=NULL) {
        pair=strtok_r(p,";",&q);
        if (!pair) break;
        pair=trim(pair);
        if (!*pair) continue;
        lat_tok=pair;
        lon_tok=strchr(pair,',');
        if (!lon_tok) return 0;
        *lon_tok++='\0';
        if (n>=CORS_POLICY_MAX_POLY_VERTS) return 0;
        region->u.poly.lat[n]=atof(trim(lat_tok));
        region->u.poly.lon[n]=atof(trim(lon_tok));
        n++;
    }
    if (n<3) return 0;

    region->type=CORS_REGION_POLYGON;
    region->u.poly.n=n;
    return 1;
}

static int parse_region(const char *region_str, cors_user_region_t *region)
{
    const char *p;

    if (streq_wild(region_str)) {
        region->type=CORS_REGION_NONE;
        return 1;
    }
    if (!strncmp(region_str,"bbox:",5)) {
        return parse_bbox(region_str+5,region);
    }
    if (!strncmp(region_str,"poly:",5)) {
        return parse_polygon(region_str+5,region);
    }
    /* bare "lat_min,lon_min,lat_max,lon_max" without prefix */
    p=strchr(region_str,',');
    if (p&&strchr(p+1,',')) {
        return parse_bbox(region_str,region);
    }
    return 0;
}

static int parse_max_sessions(const char *max_sess_str, int *out)
{
    if (streq_wild(max_sess_str)) {
        *out=0;
        return 1;
    }
    *out=atoi(max_sess_str);
    if (*out<0) return 0;
    return 1;
}

static int point_in_bbox(const cors_user_region_bbox_t *b, double lat, double lon)
{
    return lat>=b->lat_min&&lat<=b->lat_max&&
           lon>=b->lon_min&&lon<=b->lon_max;
}

static int point_in_polygon(const cors_user_region_t *region, double lat, double lon)
{
    int i,j,n=region->u.poly.n,cross=0;

    for (i=0,j=n-1;i<n;j=i++) {
        double yi=region->u.poly.lat[i], yj=region->u.poly.lat[j];
        double xi=region->u.poly.lon[i], xj=region->u.poly.lon[j];

        if (((yi>lat)!=(yj>lat))&&
            (lon<(xj-xi)*(lat-yi)/(yj-yi+1e-15)+xi)) {
            cross=!cross;
        }
    }
    return cross;
}

extern void cors_user_policy_init(cors_user_policy_t *policy)
{
    memset(policy,0,sizeof(*policy));
    policy->allowed_modes=CORR_MODE_ALL;
    policy->region.type=CORS_REGION_NONE;
    policy->max_sessions=0;
}

extern void cors_user_policy_free(cors_user_policy_t *policy)
{
    (void)policy;
}

extern int cors_user_policy_parse(cors_user_policy_t *policy,
                                  const char *modes_str,
                                  const char *region_str,
                                  const char *max_sess_str)
{
    cors_user_policy_init(policy);

    if (modes_str&&!streq_wild(modes_str)) {
        if (!parse_modes(modes_str,&policy->allowed_modes)) return 0;
    }
    if (region_str&&!streq_wild(region_str)) {
        if (!parse_region(region_str,&policy->region)) return 0;
    }
    if (max_sess_str&&!streq_wild(max_sess_str)) {
        if (!parse_max_sessions(max_sess_str,&policy->max_sessions)) return 0;
    }
    return 1;
}

extern cors_policy_mode_t cors_corr_mode_from_name(const char *name)
{
    if (!name||!*name) return 0;
    if (!strcmp(name,"*")||!strcasecmp(name,"ALL")) return CORR_MODE_ALL;

    if (!strcasecmp(name,"RELAY")) return CORR_MODE_RELAY;
    if (!strcasecmp(name,"VRS")||!strcasecmp(name,"VRS_DYNAMIC")||
        !strcasecmp(name,"VRS32")) return CORR_MODE_VRS_DYNAMIC;
    if (!strcasecmp(name,"FKP")) return CORR_MODE_FKP;
    if (!strcasecmp(name,"MAC")) return CORR_MODE_MAC;
    if (!strcasecmp(name,"IMAX")) return CORR_MODE_IMAX;
    if (!strcasecmp(name,"AUTO")||!strcasecmp(name,"NEAR")) return CORR_MODE_AUTO;

    return 0;
}

extern cors_policy_mode_t cors_corr_mode_from_mountpoint(const char *mntpnt)
{
    cors_policy_mode_t mode;

    if (!mntpnt||!*mntpnt) return 0;

    mode=cors_corr_mode_from_name(mntpnt);
    if (mode) return mode;

    /* RTCM32 = nearest physical base — treated as relay-class for ACL */
    if (!strcasecmp(mntpnt,"RTCM32")) return CORR_MODE_RELAY;

    /* named sources (IPGP, VRS_PAR, …) default to relay mountpoint class */
    return CORR_MODE_RELAY;
}

int cors_user_region_contains(const cors_user_region_t *region, const double pos[3])
{
    double llh[3];

    if (!region||region->type==CORS_REGION_NONE) return 1;
    if (!pos||norm(pos,3)<=0.0) return 1; /* defer until GGA available */

    ecef2pos(pos,llh);

    if (region->type==CORS_REGION_BBOX) {
        return point_in_bbox(&region->u.bbox,llh[0]*R2D,llh[1]*R2D);
    }
    if (region->type==CORS_REGION_POLYGON) {
        return point_in_polygon(region,llh[0]*R2D,llh[1]*R2D);
    }
    return 1;
}

extern int cors_policy_check(const cors_ntrip_user_t *user, cors_policy_mode_t mode,
                             const double pos[3])
{
    if (!user) return 0;
    if (!mode) return 0;
    if (!(user->policy.allowed_modes&mode)) return 0;
    if (!cors_user_region_contains(&user->policy.region,pos)) return 0;
    return 1;
}

extern int cors_policy_check_sessions(const cors_ntrip_user_t *user, int active_count)
{
    if (!user) return 0;
    if (user->policy.max_sessions<=0) return 1;
    return active_count<user->policy.max_sessions;
}

static int split_csv_line(char *buff, char *val[], int max_val)
{
    char *p,*q;
    int n;

    for (n=0,p=buff;*p&&n<max_val;p=q+1) {
        if ((q=strchr(p,','))||(q=strchr(p,'#'))) {
            val[n++]=p;
            *q='\0';
        }
        else break;
    }
    if (n<max_val&&*p) val[n++]=p;
    return n;
}

extern int cors_policy_read_users(const char *path, cors_ntrip_user_t **user_tbl)
{
    char buff[4096],*val[8];
    cors_ntrip_user_t *user;
    int n;
    FILE *fp;

    if (!path||!user_tbl) return 0;
    if (!(fp=fopen(path,"r"))) return 0;

    while (fgets(buff,sizeof(buff),fp)) {
        char *line=trim(buff);

        if (!*line||*line=='#') continue;

        n=split_csv_line(line,val,8);
        if (n<2) continue;

        val[0]=trim(val[0]);
        val[1]=trim(val[1]);
        if (!*val[0]||!*val[1]) continue;

        HASH_FIND_STR(*user_tbl,val[0],user);
        if (user) continue;

        user=calloc(1,sizeof(*user));
        strncpy(user->user,val[0],sizeof(user->user)-1);
        strncpy(user->passwd,val[1],sizeof(user->passwd)-1);

        if (n>=3) {
            const char *modes=trim(val[2]);
            const char *region=n>=4?trim(val[3]):NULL;
            const char *max_sess=n>=5?trim(val[4]):NULL;

            if (!cors_user_policy_parse(&user->policy,modes,region,max_sess)) {
                log_trace(1,"policy: skip user %s (invalid policy fields)\n",val[0]);
                cors_user_policy_free(&user->policy);
                free(user);
                continue;
            }
        }
        else {
            cors_user_policy_init(&user->policy);
        }

        HASH_ADD_STR(*user_tbl,user,user);
    }
    fclose(fp);
    return 1;
}

extern void cors_policy_free_users(cors_ntrip_user_t **user_tbl)
{
    cors_ntrip_user_t *u,*tmp;

    if (!user_tbl) return;
    HASH_ITER(hh,*user_tbl,u,tmp) {
        HASH_DEL(*user_tbl,u);
        cors_user_policy_free(&u->policy);
        free(u);
    }
    *user_tbl=NULL;
}
