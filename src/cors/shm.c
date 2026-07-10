/*------------------------------------------------------------------------------
 * shm.c : POSIX shared memory for multi-process CORS (cors-mengine)
 *
 * author  : cors contributors
 * version : 1.0
 * history : 2026/06/13 1.0  new
 *-----------------------------------------------------------------------------*/
#include "mcors.h"

#include <string.h>
#include <errno.h>

#ifdef WIN32
#include <windows.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static size_t cors_shm_segment_size(void)
{
    return sizeof(cors_shm_header_t);
}

static void cors_shm_init_mutex(pthread_mutex_t *mutex)
{
#ifndef WIN32
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(mutex, &attr);
    pthread_mutexattr_destroy(&attr);
#else
    (void)mutex;
#endif
}

static void cors_shm_init_header(cors_shm_header_t *hdr, int workers)
{
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic = MCORS_SHM_MAGIC;
    hdr->version = MCORS_SHM_VERSION;
    hdr->worker_count = (uint32_t)workers;
#ifndef WIN32
    cors_shm_init_mutex(&hdr->nav_lock);
    cors_shm_init_mutex(&hdr->obs_lock);
    cors_shm_init_mutex(&hdr->blsol_lock);
    cors_shm_init_mutex(&hdr->rtcm_lock);
#endif
}

static void cors_shm_init_nav_arrays(cors_shm_header_t *hdr)
{
    eph_t  eph0 = {0, -1, -1};
    geph_t geph0 = {0, -1};
    seph_t seph0 = {0};
    int i;

    for (i = 0; i < MAXSAT * 4; i++) hdr->nav_eph[i] = eph0;
    for (i = 0; i < NSATGLO * 2; i++) hdr->nav_geph[i] = geph0;
    for (i = 0; i < NSATSBS * 2; i++) hdr->nav_seph[i] = seph0;
    memset(hdr->nav_glo_fcn, 0, sizeof(hdr->nav_glo_fcn));
}

static nav_t cors_shm_nav_view(cors_shm_header_t *hdr)
{
    nav_t nav = {0};
    nav.eph = hdr->nav_eph;
    nav.geph = hdr->nav_geph;
    nav.seph = hdr->nav_seph;
    nav.n = MAXSAT * 2;
    nav.ng = NSATGLO * 2;
    nav.ns = NSATSBS * 2;
    memcpy(nav.glo_fcn, hdr->nav_glo_fcn, sizeof(nav.glo_fcn));
    return nav;
}

#ifndef WIN32

static int cors_shm_map(cors_shm_t *shm, const char *name, int create, int workers)
{
    char shm_path[128];
    int fd;
    size_t size = cors_shm_segment_size();

    snprintf(shm_path, sizeof(shm_path), "/%s", name);
    strncpy(shm->name, name, sizeof(shm->name) - 1);

    if (create) {
        shm_unlink(shm_path);
        fd = shm_open(shm_path, O_CREAT | O_RDWR, 0600);
        shm->creator = 1;
    } else {
        fd = shm_open(shm_path, O_RDWR, 0600);
        shm->creator = 0;
    }
    if (fd < 0) {
        log_trace(1, "cors_shm_map: shm_open %s error: %s\n", shm_path, strerror(errno));
        return 0;
    }
    if (create && ftruncate(fd, (off_t)size) < 0) {
        log_trace(1, "cors_shm_map: ftruncate error: %s\n", strerror(errno));
        close(fd);
        shm_unlink(shm_path);
        return 0;
    }
    shm->base = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (shm->base == MAP_FAILED) {
        log_trace(1, "cors_shm_map: mmap error: %s\n", strerror(errno));
        if (create) shm_unlink(shm_path);
        return 0;
    }
    shm->size = size;
    shm->hdr = (cors_shm_header_t *)shm->base;
    if (create) {
        cors_shm_init_header(shm->hdr, workers);
        cors_shm_init_nav_arrays(shm->hdr);
    } else if (shm->hdr->magic != MCORS_SHM_MAGIC || shm->hdr->version != MCORS_SHM_VERSION) {
        log_trace(1, "cors_shm_map: invalid shm header\n");
        munmap(shm->base, shm->size);
        shm->base = NULL;
        shm->hdr = NULL;
        return 0;
    }
    return 1;
}

#endif /* WIN32 */

#ifdef WIN32

static HANDLE cors_shm_win_mutex(cors_shm_t *shm, const char *suffix)
{
    char name[128];
    snprintf(name, sizeof(name), "Local\\cors_shm_%s_%s", shm->name, suffix);
    return CreateMutexA(NULL, FALSE, name);
}

static void cors_shm_lock_nav(cors_shm_t *shm)
{
    if (shm && shm->mtx_nav) WaitForSingleObject((HANDLE)shm->mtx_nav, INFINITE);
}
static void cors_shm_unlock_nav(cors_shm_t *shm)
{
    if (shm && shm->mtx_nav) ReleaseMutex((HANDLE)shm->mtx_nav);
}
static void cors_shm_lock_obs(cors_shm_t *shm)
{
    if (shm && shm->mtx_obs) WaitForSingleObject((HANDLE)shm->mtx_obs, INFINITE);
}
static void cors_shm_unlock_obs(cors_shm_t *shm)
{
    if (shm && shm->mtx_obs) ReleaseMutex((HANDLE)shm->mtx_obs);
}
static void cors_shm_lock_blsol(cors_shm_t *shm)
{
    if (shm && shm->mtx_blsol) WaitForSingleObject((HANDLE)shm->mtx_blsol, INFINITE);
}
static void cors_shm_unlock_blsol(cors_shm_t *shm)
{
    if (shm && shm->mtx_blsol) ReleaseMutex((HANDLE)shm->mtx_blsol);
}
static void cors_shm_lock_rtcm(cors_shm_t *shm)
{
    if (shm && shm->mtx_rtcm) WaitForSingleObject((HANDLE)shm->mtx_rtcm, INFINITE);
}
static void cors_shm_unlock_rtcm(cors_shm_t *shm)
{
    if (shm && shm->mtx_rtcm) ReleaseMutex((HANDLE)shm->mtx_rtcm);
}

static int cors_shm_map_win(cors_shm_t *shm, const char *name, int create, int workers)
{
    char map_name[128];
    HANDLE hmap;
    size_t size = cors_shm_segment_size();
    void *base;
    DWORD err;

    snprintf(map_name, sizeof(map_name), "Local\\cors_shm_%s", name);
    strncpy(shm->name, name, sizeof(shm->name) - 1);

    hmap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                              (DWORD)size, map_name);
    if (!hmap) {
        log_trace(1, "cors_shm_map_win: CreateFileMapping %s error: %lu\n", map_name,
                  (unsigned long)GetLastError());
        return 0;
    }
    err = GetLastError();
    if (!create && err != ERROR_ALREADY_EXISTS) {
        CloseHandle(hmap);
        log_trace(1, "cors_shm_map_win: segment %s not found\n", map_name);
        return 0;
    }
    shm->creator = create ? 1 : 0;
    base = MapViewOfFile(hmap, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!base) {
        log_trace(1, "cors_shm_map_win: MapViewOfFile error: %lu\n",
                  (unsigned long)GetLastError());
        CloseHandle(hmap);
        return 0;
    }
    shm->map_handle = hmap;
    shm->base = base;
    shm->size = size;
    shm->hdr = (cors_shm_header_t *)base;
    shm->mtx_nav = cors_shm_win_mutex(shm, "nav");
    shm->mtx_obs = cors_shm_win_mutex(shm, "obs");
    shm->mtx_blsol = cors_shm_win_mutex(shm, "blsol");
    shm->mtx_rtcm = cors_shm_win_mutex(shm, "rtcm");
    if (!shm->mtx_nav || !shm->mtx_obs || !shm->mtx_blsol || !shm->mtx_rtcm) {
        log_trace(1, "cors_shm_map_win: mutex create failed\n");
        cors_shm_close(shm);
        return 0;
    }
    if (create) {
        cors_shm_init_header(shm->hdr, workers);
        cors_shm_init_nav_arrays(shm->hdr);
    }
    else if (shm->hdr->magic != MCORS_SHM_MAGIC || shm->hdr->version != MCORS_SHM_VERSION) {
        log_trace(1, "cors_shm_map_win: invalid shm header\n");
        cors_shm_close(shm);
        return 0;
    }
    return 1;
}

#else /* !WIN32 */

static void cors_shm_lock_nav(cors_shm_t *shm)
{
    pthread_mutex_lock(&shm->hdr->nav_lock);
}
static void cors_shm_unlock_nav(cors_shm_t *shm)
{
    pthread_mutex_unlock(&shm->hdr->nav_lock);
}
static void cors_shm_lock_obs(cors_shm_t *shm)
{
    pthread_mutex_lock(&shm->hdr->obs_lock);
}
static void cors_shm_unlock_obs(cors_shm_t *shm)
{
    pthread_mutex_unlock(&shm->hdr->obs_lock);
}
static void cors_shm_lock_blsol(cors_shm_t *shm)
{
    pthread_mutex_lock(&shm->hdr->blsol_lock);
}
static void cors_shm_unlock_blsol(cors_shm_t *shm)
{
    pthread_mutex_unlock(&shm->hdr->blsol_lock);
}
static void cors_shm_lock_rtcm(cors_shm_t *shm)
{
    pthread_mutex_lock(&shm->hdr->rtcm_lock);
}
static void cors_shm_unlock_rtcm(cors_shm_t *shm)
{
    pthread_mutex_unlock(&shm->hdr->rtcm_lock);
}

#endif /* WIN32 */

extern int cors_shm_create(cors_shm_t *shm, const char *name, int workers)
{
    if (!shm || !name || !*name) return 0;
    memset(shm, 0, sizeof(*shm));
#ifdef WIN32
    if (workers <= 0 || workers > MCORS_MAX_WORKERS) workers = 1;
    return cors_shm_map_win(shm, name, 1, workers);
#else
    if (workers <= 0 || workers > MCORS_MAX_WORKERS) workers = 1;
    return cors_shm_map(shm, name, 1, workers);
#endif
}

extern int cors_shm_open(cors_shm_t *shm, const char *name)
{
    if (!shm || !name || !*name) return 0;
    memset(shm, 0, sizeof(*shm));
#ifdef WIN32
    return cors_shm_map_win(shm, name, 0, 0);
#else
    return cors_shm_map(shm, name, 0, 0);
#endif
}

extern void cors_shm_close(cors_shm_t *shm)
{
    if (!shm || !shm->base) return;
#ifdef WIN32
    if (shm->mtx_nav) CloseHandle((HANDLE)shm->mtx_nav);
    if (shm->mtx_obs) CloseHandle((HANDLE)shm->mtx_obs);
    if (shm->mtx_blsol) CloseHandle((HANDLE)shm->mtx_blsol);
    if (shm->mtx_rtcm) CloseHandle((HANDLE)shm->mtx_rtcm);
    shm->mtx_nav = shm->mtx_obs = shm->mtx_blsol = shm->mtx_rtcm = NULL;
    UnmapViewOfFile(shm->base);
    if (shm->map_handle) CloseHandle((HANDLE)shm->map_handle);
    shm->map_handle = NULL;
    shm->base = NULL;
    shm->hdr = NULL;
#else
    munmap(shm->base, shm->size);
    if (shm->creator) {
        char shm_path[128];
        snprintf(shm_path, sizeof(shm_path), "/%s", shm->name);
        shm_unlink(shm_path);
    }
    shm->base = NULL;
    shm->hdr = NULL;
#endif
}

static void cors_shm_updnav_locked(nav_t *shm_nav, const nav_t *nav, int ephsat, int ephset)
{
    geph_t *geph1, *geph2, *geph3;
    eph_t *eph1, *eph2, *eph3;
    int prn;

    if (!shm_nav->eph || !nav->eph) return;

    if (satsys(ephsat, &prn) != SYS_GLO) {
        eph1 = nav->eph + ephsat - 1 + MAXSAT * ephset;
        eph2 = shm_nav->eph + ephsat - 1 + MAXSAT * ephset;
        eph3 = shm_nav->eph + ephsat - 1 + MAXSAT * (2 + ephset);
        if (eph2->ttr.time == 0 ||
            (eph1->iode != eph3->iode && eph1->iode != eph2->iode) ||
            (timediff(eph1->toe, eph3->toe) != 0.0 && timediff(eph1->toe, eph2->toe) != 0.0) ||
            (timediff(eph1->toc, eph3->toc) != 0.0 && timediff(eph1->toc, eph2->toc) != 0.0)) {
            *eph3 = *eph2;
            *eph2 = *eph1;
        }
    } else {
        shm_nav->glo_fcn[prn - 1] = nav->glo_fcn[prn - 1];
        geph1 = nav->geph + prn - 1;
        geph2 = shm_nav->geph + prn - 1;
        geph3 = shm_nav->geph + prn - 1 + MAXPRNGLO;
        if (geph2->tof.time == 0 ||
            (geph1->iode != geph3->iode && geph1->iode != geph2->iode)) {
            *geph3 = *geph2;
            *geph2 = *geph1;
        }
    }
}

extern void cors_shm_publish_nav(cors_shm_t *shm, const nav_t *nav, int ephsat, int ephset)
{
    nav_t shm_nav;
    if (!shm || !shm->hdr || !nav) return;
    cors_shm_lock_nav(shm);
    shm_nav = cors_shm_nav_view(shm->hdr);
    cors_shm_updnav_locked(&shm_nav, nav, ephsat, ephset);
    memcpy(shm->hdr->nav_glo_fcn, shm_nav.glo_fcn, sizeof(shm->hdr->nav_glo_fcn));
    shm->hdr->nav_valid = 1;
    cors_shm_unlock_nav(shm);
}

extern void cors_shm_sync_nav(cors_shm_t *shm, cors_nav_t *cors_nav)
{
    if (!shm || !shm->hdr || !cors_nav) return;
    if (!shm->hdr->nav_valid) return;
    cors_shm_lock_nav(shm);
    if (cors_nav->data.eph) {
        memcpy(cors_nav->data.eph, shm->hdr->nav_eph, sizeof(eph_t) * MAXSAT * 4);
        memcpy(cors_nav->data.geph, shm->hdr->nav_geph, sizeof(geph_t) * NSATGLO * 2);
        memcpy(cors_nav->data.seph, shm->hdr->nav_seph, sizeof(seph_t) * NSATSBS * 2);
        memcpy(cors_nav->data.glo_fcn, shm->hdr->nav_glo_fcn, sizeof(shm->hdr->nav_glo_fcn));
    }
    cors_shm_unlock_nav(shm);
}

extern void cors_shm_publish_obs(cors_shm_t *shm, const obsd_t *obsd, int n, int srcid)
{
    int i;
    if (!shm || !shm->hdr || !obsd || n <= 0 || n > MAXOBS) return;

    cors_shm_lock_obs(shm);
    for (i = 0; i < MCORS_MAX_OBS_SLOTS; i++) {
        cors_shm_obs_slot_t *slot = &shm->hdr->obs[i];
        if (!slot->valid || slot->srcid == srcid) {
            slot->valid = 1;
            slot->srcid = srcid;
            slot->n = n;
            slot->time = obsd[0].time;
            memcpy(slot->obs, obsd, sizeof(obsd_t) * n);
            cors_shm_unlock_obs(shm);
            return;
        }
    }
    cors_shm_unlock_obs(shm);
    log_trace(2, "cors_shm_publish_obs: no slot for srcid %d\n", srcid);
}

extern int cors_shm_sync_obs(cors_shm_t *shm, cors_obs_t *cors_obs)
{
    int i, synced = 0;
    if (!shm || !shm->hdr || !cors_obs) return 0;

    cors_shm_lock_obs(shm);
    for (i = 0; i < MCORS_MAX_OBS_SLOTS; i++) {
        cors_shm_obs_slot_t *slot = &shm->hdr->obs[i];
        if (!slot->valid || slot->n <= 0) continue;
        cors_updobs(cors_obs, slot->obs, slot->n, slot->srcid);
        synced++;
    }
    cors_shm_unlock_obs(shm);
    return synced;
}

extern void cors_shm_publish_blsol(cors_shm_t *shm, const rtk_t *rtk,
                                   int base_srcid, int rover_srcid)
{
    int i;
    if (!shm || !shm->hdr || !rtk) return;

    cors_shm_lock_blsol(shm);
    for (i = 0; i < MCORS_MAX_BLSOL_SLOTS; i++) {
        cors_shm_blsol_slot_t *slot = &shm->hdr->blsols[i];
        if (!slot->valid ||
            (slot->base_srcid == base_srcid && slot->rover_srcid == rover_srcid)) {
            slot->valid = 1;
            slot->base_srcid = base_srcid;
            slot->rover_srcid = rover_srcid;
            slot->rtk = *rtk;
            cors_shm_unlock_blsol(shm);
            return;
        }
    }
    cors_shm_unlock_blsol(shm);
}

extern int cors_shm_sync_blsols(cors_shm_t *shm, cors_blsols_t *blsols)
{
    int i, synced = 0;
    cors_baseline_t bl_stub = {0};

    if (!shm || !shm->hdr || !blsols) return 0;

    cors_shm_lock_blsol(shm);
    for (i = 0; i < MCORS_MAX_BLSOL_SLOTS; i++) {
        cors_shm_blsol_slot_t *slot = &shm->hdr->blsols[i];
        if (!slot->valid) continue;
        cors_updblsol(blsols, &bl_stub, &slot->rtk, slot->base_srcid, slot->rover_srcid);
        synced++;
    }
    cors_shm_unlock_blsol(shm);
    return synced;
}

extern int cors_shm_publish_rtcm(cors_shm_t *shm, const uint8_t *data, int n, int srcid)
{
    uint32_t next;
    cors_shm_rtcm_slot_t *slot;

    if (!shm || !shm->hdr || !data || n <= 0 || n > MCORS_RTCM_MAX_PAYLOAD) return 0;

    cors_shm_lock_rtcm(shm);
    next = (shm->hdr->rtcm_write_idx + 1) % MCORS_RTCM_RING_SLOTS;
    if (next == shm->hdr->rtcm_read_idx) {
        cors_shm_unlock_rtcm(shm);
        return 0;
    }
    slot = &shm->hdr->rtcm_ring[shm->hdr->rtcm_write_idx];
    slot->len = (uint32_t)n;
    slot->srcid = srcid;
    memcpy(slot->data, data, n);
    shm->hdr->rtcm_write_idx = next;
    cors_shm_unlock_rtcm(shm);
    return 1;
}

extern int cors_shm_drain_rtcm(cors_shm_t *shm,
                               int (*cb)(void *userdata, const uint8_t *data, int n, int srcid),
                               void *userdata)
{
    int count = 0;
    cors_shm_rtcm_slot_t *slot;

    if (!shm || !shm->hdr || !cb) return 0;

    cors_shm_lock_rtcm(shm);
    while (shm->hdr->rtcm_read_idx != shm->hdr->rtcm_write_idx) {
        slot = &shm->hdr->rtcm_ring[shm->hdr->rtcm_read_idx];
        cb(userdata, slot->data, (int)slot->len, slot->srcid);
        shm->hdr->rtcm_read_idx = (shm->hdr->rtcm_read_idx + 1) % MCORS_RTCM_RING_SLOTS;
        count++;
    }
    cors_shm_unlock_rtcm(shm);
    return count;
}

extern int cors_shm_find_srcid(cors_shm_t *shm, const char *name)
{
    int i;
    if (!shm || !shm->hdr || !name) return 0;
    for (i = 0; i < shm->hdr->src_count && i < MCORS_MAX_SOURCES; i++) {
        if (!strcmp(shm->hdr->src_table[i].name, name)) {
            return shm->hdr->src_table[i].srcid;
        }
    }
    return 0;
}

extern void cors_shm_request_shutdown(cors_shm_t *shm)
{
    if (shm && shm->hdr) shm->hdr->shutdown_flag = 1;
}

extern int cors_shm_shutdown_requested(cors_shm_t *shm)
{
    if (!shm || !shm->hdr) return 0;
    return shm->hdr->shutdown_flag != 0;
}
