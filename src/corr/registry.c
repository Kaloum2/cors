/*------------------------------------------------------------------------------
 * registry.c : mountpoint registry — load config, resolve name → definition
 *-----------------------------------------------------------------------------*/
#include "corr.h"
#include "corr_i.h"
#include "log.h"
#include <stdio.h>
#include <string.h>

static void add_default_mount(cors_corr_registry_t *reg, const char *name,
                              cors_corr_mode_t mode, int legacy_type, const char *format)
{
    cors_mountpoint_def_t *m, def={0};

    if (!reg||!name||!*name) return;
    HASH_FIND_STR(reg->mnt_tbl,name,m);
    if (m) return;

    strncpy(def.name,name,sizeof(def.name)-1);
    def.mode=mode;
    def.legacy_type=legacy_type;
    if (format) strncpy(def.format,format,sizeof(def.format)-1);
    cors_corr_registry_add(reg,&def);
}

static void register_builtin_mounts(cors_corr_registry_t *reg)
{
    add_default_mount(reg,"RTCM32",CORS_CORR_NEAR,CORS_CORR_LEGACY_TYPE_NEAR,"RTCM3");
    add_default_mount(reg,"NEAR",CORS_CORR_NEAR,CORS_CORR_LEGACY_TYPE_NEAR,"RTCM3");
    add_default_mount(reg,"VRS",CORS_CORR_VRS_DYNAMIC,3,"RTCM3_MSM7");
    add_default_mount(reg,"VRS32",CORS_CORR_VRS_DYNAMIC,3,"RTCM3_MSM7");
    add_default_mount(reg,"FKP",CORS_CORR_FKP,3,"RTCM3_1034_1035");
    add_default_mount(reg,"MAC",CORS_CORR_MAC,3,"RTCM3_MAC");
    add_default_mount(reg,"IMAX",CORS_CORR_IMAX,3,"RTCM3_IMAX");
    add_default_mount(reg,"AUTO",CORS_CORR_AUTO,3,"RTCM3");
}

static void parse_mount_line(cors_corr_registry_t *reg, char *line)
{
    char *p,*q,*name=NULL,*type=NULL,*source=NULL,*format=NULL,*priority=NULL;
    cors_mountpoint_def_t def={0};

    if ((p=strchr(line,'#'))) *p='\0';
    for (p=line;*p==' '||*p=='\t';p++);
    if (!*p) return;

    name=p;
    if ((q=strchr(p,','))) {
        *q='\0'; type=q+1;
        for (q=type;(p=strchr(q,','));q=p+1) {
            *p='\0';
            if (!strncmp(q,"type=",5)) type=q+5;
            else if (!strncmp(q,"source=",7)) source=q+7;
            else if (!strncmp(q,"format=",7)) format=q+7;
            else if (!strncmp(q,"priority=",9)) priority=q+9;
        }
        if (!strncmp(q,"type=",5)) type=q+5;
        else if (!strncmp(q,"source=",7)) source=q+7;
        else if (!strncmp(q,"format=",7)) format=q+7;
        else if (!strncmp(q,"priority=",9)) priority=q+9;
    }
    def.mode=cors_corr_mode_parse(type);
    strncpy(def.name,name,sizeof(def.name)-1);
    if (source) strncpy(def.source,source,sizeof(def.source)-1);
    if (format) strncpy(def.format,format,sizeof(def.format)-1);
    else strcpy(def.format,"RTCM3");
    if (priority) strncpy(def.priority,priority,sizeof(def.priority)-1);
    if (def.mode==CORS_CORR_NEAR) def.legacy_type=CORS_CORR_LEGACY_TYPE_NEAR;
    else if (def.mode==CORS_CORR_RELAY) def.legacy_type=CORS_CORR_LEGACY_TYPE_RELAY;
    else def.legacy_type=3;
    cors_corr_registry_add(reg,&def);
}

static void load_mountpoints_file(cors_corr_registry_t *reg, const char *file)
{
    char buff[256];
    FILE *fp;

    if (!file||!*file) return;
    if (!(fp=fopen(file,"r"))) {
        log_trace(1,"corr: mountpoints file not found: %s (using builtins)\n",file);
        return;
    }
    while (fgets(buff,sizeof(buff),fp)) parse_mount_line(reg,buff);
    fclose(fp);
}

extern int cors_corr_registry_init(cors_corr_registry_t *reg)
{
    if (!reg) return 0;
    reg->mnt_tbl=NULL;
    reg->n_mnt=0;
    return 1;
}

extern void cors_corr_registry_free(cors_corr_registry_t *reg)
{
    cors_mountpoint_def_t *m,*t;

    if (!reg) return;
    HASH_ITER(hh,reg->mnt_tbl,m,t) {
        HASH_DEL(reg->mnt_tbl,m);
        free(m);
    }
    reg->mnt_tbl=NULL;
    reg->n_mnt=0;
}

extern int cors_corr_registry_add(cors_corr_registry_t *reg, const cors_mountpoint_def_t *def)
{
    cors_mountpoint_def_t *m;

    if (!reg||!def||!def->name[0]) return 0;
    HASH_FIND_STR(reg->mnt_tbl,def->name,m);
    if (!m) {
        m=calloc(1,sizeof(*m));
        HASH_ADD_STR(reg->mnt_tbl,name,m);
        reg->n_mnt++;
    }
    *m=*def;
    return 1;
}

extern const cors_mountpoint_def_t *cors_corr_registry_find(const cors_corr_registry_t *reg,
                                                            const char *mntpnt)
{
    cors_mountpoint_def_t *m;

    if (!reg||!mntpnt||!*mntpnt) return NULL;
    HASH_FIND_STR(reg->mnt_tbl,mntpnt,m);
    return m;
}

extern int cors_corr_registry_load(cors_corr_registry_t *reg, const char *file)
{
    if (!reg) return 0;
    register_builtin_mounts(reg);
    load_mountpoints_file(reg,file);
    log_trace(1,"corr: registry loaded %d mountpoints\n",reg->n_mnt);
    return 1;
}
