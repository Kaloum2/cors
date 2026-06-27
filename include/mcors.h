/*------------------------------------------------------------------------------
 * mcors.h : multi-process CORS engine API (cors-mengine)
 *
 * Provides shared memory, control-interface pipes, and worker orchestration
 * for horizontally scaling NTRIP ingestion and RTK baseline processing.
 *
 * author  : cors contributors
 * version : 1.0
 * history : 2026/06/13 1.0  new
 *-----------------------------------------------------------------------------*/
#ifndef MCORS_H
#define MCORS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cors.h"

/* -------------------------------------------------------------------------- */
/* Constants                                                                  */
/* -------------------------------------------------------------------------- */

#define MCORS_SHM_MAGIC       0xC0522026u
#define MCORS_SHM_VERSION     1u
#define MCORS_MAX_WORKERS     64
#define MCORS_MAX_SOURCES     2048
#define MCORS_MAX_OBS_SLOTS   256
#define MCORS_MAX_BLSOL_SLOTS 512
#define MCORS_RTCM_RING_SLOTS 128
#define MCORS_RTCM_MAX_PAYLOAD 4096
#define MCORS_CI_MAX_MSG      4096
#define MCORS_CI_DEFAULT_PATH "/tmp/cors-mengine.ci"
#define MCORS_SHM_DEFAULT_NAME "cors_mengine_shm"

/* cors_role_t is defined in cors.h */

/* -------------------------------------------------------------------------- */
/* Shared-memory layout (mapped segment; do not embed pointers)               */
/* -------------------------------------------------------------------------- */

typedef struct cors_shm_src_entry {
    char name[64];
    int  srcid;
    int  worker_id;
} cors_shm_src_entry_t;

typedef struct cors_shm_obs_slot {
    int     valid;
    int     srcid;
    int     n;
    gtime_t time;
    obsd_t  obs[MAXOBS];
} cors_shm_obs_slot_t;

typedef struct cors_shm_blsol_slot {
    int valid;
    int base_srcid;
    int rover_srcid;
    rtk_t rtk;
} cors_shm_blsol_slot_t;

typedef struct cors_shm_rtcm_slot {
    uint32_t len;
    int      srcid;
    uint8_t  data[MCORS_RTCM_MAX_PAYLOAD];
} cors_shm_rtcm_slot_t;

typedef struct cors_shm_header {
    uint32_t magic;
    uint32_t version;
    uint32_t worker_count;
    uint32_t active_workers;
    int      src_count;
    int      shutdown_flag;
    pthread_mutex_t nav_lock;
    pthread_mutex_t obs_lock;
    pthread_mutex_t blsol_lock;
    pthread_mutex_t rtcm_lock;
    int      nav_valid;
    eph_t    nav_eph[MAXSAT * 4];
    geph_t   nav_geph[NSATGLO * 2];
    seph_t   nav_seph[NSATSBS * 2];
    int      nav_glo_fcn[32];
    cors_shm_src_entry_t src_table[MCORS_MAX_SOURCES];
    cors_shm_obs_slot_t obs[MCORS_MAX_OBS_SLOTS];
    cors_shm_blsol_slot_t blsols[MCORS_MAX_BLSOL_SLOTS];
    uint32_t rtcm_write_idx;
    uint32_t rtcm_read_idx;
    cors_shm_rtcm_slot_t rtcm_ring[MCORS_RTCM_RING_SLOTS];
} cors_shm_header_t;

/* Opaque handle wrapping a mapped shared-memory segment. */
typedef struct cors_shm {
    char   name[64];
    int    creator;
    size_t size;
    void  *base;
    cors_shm_header_t *hdr;
} cors_shm_t;

/* -------------------------------------------------------------------------- */
/* Control interface (supervisor <-> worker pipes) — cors_ci_t in cors.h       */
/* -------------------------------------------------------------------------- */

typedef struct cors_ci_client cors_ci_client_t;

/* -------------------------------------------------------------------------- */
/* Multi-process coordinator                                                  */
/* -------------------------------------------------------------------------- */

typedef struct mcors_worker {
    int            id;
    uv_process_t   process;
    uv_process_options_t options;
    char           partition_file[MAXSTRPATH];
    char           ci_pipe_path[MAXSTRPATH];
    int            state;       /* 0=stopped, 1=running */
} mcors_worker_t;

typedef struct mcors_opt {
    int  worker_count;          /* 0 = auto (ceil(sources / 1024))           */
    char shm_name[64];
    char ci_pipe_path[256];
    char exec_path[MAXSTRPATH]; /* cors-mengine binary for uv_spawn            */
    char work_dir[MAXSTRPATH];  /* partition files directory                   */
} mcors_opt_t;

typedef struct mcors {
    mcors_opt_t    opt;
    cors_opt_t     cors_opt;
    char           config_file[MAXSTRPATH];
    cors_shm_t    *shm;
    cors_ci_t      ci;
    cors_t         cors;
    mcors_worker_t workers[MCORS_MAX_WORKERS];
    int            worker_count;
    int            state;
    uv_thread_t    sync_thread;
    uv_async_t    *shutdown;
} mcors_t;

/* -------------------------------------------------------------------------- */
/* Shared memory API                                                          */
/* -------------------------------------------------------------------------- */

/**
 * Create a new POSIX shared-memory segment and map it.
 * Initializes header, mutexes (process-shared), and source table.
 *
 * @param shm      Output handle (zeroed on input).
 * @param name     Segment name (no leading slash on Linux shm_open).
 * @param workers  Planned worker count (stored in header).
 * @return 1 on success, 0 on failure.
 */
EXPORT int cors_shm_create(cors_shm_t *shm, const char *name, int workers);

/**
 * Open an existing shared-memory segment created by cors_shm_create().
 *
 * @param shm  Output handle.
 * @param name Segment name.
 * @return 1 on success, 0 on failure.
 */
EXPORT int cors_shm_open(cors_shm_t *shm, const char *name);

/**
 * Unmap and close/unlink the segment (creator unlinks).
 *
 * @param shm Shared-memory handle.
 */
EXPORT void cors_shm_close(cors_shm_t *shm);

/**
 * Publish navigation update from a worker into shared memory.
 * Thread-safe; copies ephemeris via cors_updnav logic on the shm nav blob.
 *
 * @param shm     Mapped segment handle.
 * @param nav     Source navigation data.
 * @param ephsat  Satellite index.
 * @param ephset  Ephemeris set index.
 */
EXPORT void cors_shm_publish_nav(cors_shm_t *shm, const nav_t *nav, int ephsat, int ephset);

/**
 * Merge shared navigation into a local cors_nav_t (supervisor sync).
 *
 * @param shm      Mapped segment.
 * @param cors_nav Local navigation store.
 */
EXPORT void cors_shm_sync_nav(cors_shm_t *shm, cors_nav_t *cors_nav);

/**
 * Publish observation epoch from worker to shared memory.
 *
 * @param shm   Mapped segment.
 * @param obsd  Observation array.
 * @param n     Number of observations.
 * @param srcid Source identifier.
 */
EXPORT void cors_shm_publish_obs(cors_shm_t *shm, const obsd_t *obsd, int n, int srcid);

/**
 * Pull observation updates into local cors_obs_t (supervisor).
 *
 * @param shm      Mapped segment.
 * @param cors_obs Local observation store.
 * @return Number of slots synchronized this call.
 */
EXPORT int cors_shm_sync_obs(cors_shm_t *shm, cors_obs_t *cors_obs);

/**
 * Publish RTK baseline solution from worker.
 *
 * @param shm         Mapped segment.
 * @param rtk         RTK solution.
 * @param base_srcid  Base station source id.
 * @param rover_srcid Rover source id.
 */
EXPORT void cors_shm_publish_blsol(cors_shm_t *shm, const rtk_t *rtk,
                                   int base_srcid, int rover_srcid);

/**
 * Pull baseline solutions into local cors_blsols_t.
 *
 * @param shm    Mapped segment.
 * @param blsols Local baseline solution table.
 * @return Number of solutions synchronized.
 */
EXPORT int cors_shm_sync_blsols(cors_shm_t *shm, cors_blsols_t *blsols);

/**
 * Enqueue raw RTCM payload for supervisor NTRIP agent relay.
 *
 * @param shm   Mapped segment.
 * @param data  RTCM bytes.
 * @param n     Payload length (must be <= MCORS_RTCM_MAX_PAYLOAD).
 * @param srcid Originating source id.
 * @return 1 if queued, 0 if ring full or invalid input.
 */
EXPORT int cors_shm_publish_rtcm(cors_shm_t *shm, const uint8_t *data, int n, int srcid);

/**
 * Drain RTCM ring buffer and invoke callback for each message.
 *
 * @param shm     Mapped segment.
 * @param cb      Callback(userdata, data, n, srcid); return ignored.
 * @param userdata Opaque pointer passed to callback.
 * @return Number of messages delivered.
 */
EXPORT int cors_shm_drain_rtcm(cors_shm_t *shm,
                               int (*cb)(void *userdata, const uint8_t *data, int n, int srcid),
                               void *userdata);

/**
 * Lookup global source id by station name in the shared source table.
 *
 * @param shm  Mapped segment.
 * @param name Station name.
 * @return Source id, or 0 if not found.
 */
EXPORT int cors_shm_find_srcid(cors_shm_t *shm, const char *name);

/* -------------------------------------------------------------------------- */
/* Control interface API                                                        */
/* -------------------------------------------------------------------------- */

/**
 * Start a named-pipe server for worker control channels.
 *
 * @param ci         Control interface object (zeroed on input).
 * @param loop       libuv loop hosting the server.
 * @param pipe_path  Filesystem path (max 255 chars).
 * @return 1 on success, 0 on failure.
 */
EXPORT int cors_ci_start(cors_ci_t *ci, uv_loop_t *loop, const char *pipe_path);

/**
 * Stop pipe server and close all worker connections.
 *
 * @param ci Control interface.
 */
EXPORT void cors_ci_close(cors_ci_t *ci);

/**
 * Worker connects to supervisor control pipe.
 *
 * @param loop       libuv loop.
 * @param pipe_path  Supervisor pipe path.
 * @param pipe       Output connected pipe (caller allocates uv_pipe_t).
 * @return 1 on success, 0 on failure.
 */
EXPORT int cors_ci_connect(uv_loop_t *loop, const char *pipe_path, uv_pipe_t *pipe);

/**
 * Send a length-prefixed command to a worker (supervisor side).
 * Message format on wire: [uint32_t len][payload bytes], len <= MCORS_CI_MAX_MSG.
 *
 * @param ci        Control interface with connected client.
 * @param worker_id Target worker id.
 * @param msg       Null-terminated or binary command buffer.
 * @param len       Payload length.
 * @return 1 on success, 0 on failure.
 */
EXPORT int cors_ci_send(cors_ci_t *ci, int worker_id, const char *msg, uint32_t len);

/**
 * Request graceful shutdown of all workers via shared-memory flag.
 *
 * @param shm Mapped segment.
 */
EXPORT void cors_shm_request_shutdown(cors_shm_t *shm);

/**
 * Returns 1 if supervisor requested shutdown via shared memory.
 *
 * @param shm Mapped segment.
 */
EXPORT int cors_shm_shutdown_requested(cors_shm_t *shm);

/* -------------------------------------------------------------------------- */
/* Multi-process coordinator API                                                */
/* -------------------------------------------------------------------------- */

/**
 * Initialize mcors options with secure defaults.
 *
 * @param opt Output options (zeroed on input).
 */
EXPORT void mcors_init_opt(mcors_opt_t *opt);

/**
 * Load CORS config, partition NTRIP sources, populate shm source table,
 * and write per-worker partition files under opt.work_dir.
 *
 * @param mcors     Coordinator (zeroed on input).
 * @param cors_opt  Loaded cors_opt_t from cors_loadopts().
 * @param mopt      Multi-process options.
 * @param config_file Path to main cors.conf (passed to workers).
 * @return Number of workers planned, or 0 on error.
 */
EXPORT int mcors_prepare(mcors_t *mcors, const cors_opt_t *cors_opt, const mcors_opt_t *mopt,
                          const char *config_file);

/**
 * Spawn worker processes and start supervisor CORS services.
 *
 * @param mcors Coordinator prepared by mcors_prepare().
 * @return 1 on success, 0 on failure.
 */
EXPORT int mcors_start(mcors_t *mcors);

/**
 * Graceful shutdown: signal workers, stop supervisor, reap children.
 *
 * @param mcors Coordinator.
 */
EXPORT void mcors_close(mcors_t *mcors);

/**
 * Run worker process main loop (NTRIP shard + RTK, publish to shm).
 *
 * @param worker_id       Worker index (0-based).
 * @param shm_name        Shared-memory segment name.
 * @param partition_file  NTRIP sources subset file.
 * @param cors_opt        Full CORS configuration.
 * @param ci_pipe_path    Supervisor control pipe path.
 * @return Process exit code (0 success).
 */
EXPORT int mcors_worker_run(int worker_id, const char *shm_name,
                            const char *partition_file, const cors_opt_t *cors_opt,
                            const char *ci_pipe_path);

/**
 * Partition NTRIP sources file into worker subset files.
 * Also fills shm->hdr->src_table with global source ids.
 *
 * @param sources_file Master sources file path.
 * @param work_dir     Directory for partition outputs.
 * @param worker_count Number of workers (if 0, auto-calculated).
 * @param shm          Shared memory (must be created).
 * @return Worker count used, or 0 on error.
 */
EXPORT int mcors_partition_sources(const char *sources_file, const char *work_dir,
                                   int worker_count, cors_shm_t *shm);

/* -------------------------------------------------------------------------- */
/* CORS lifecycle variants for multi-process roles                              */
/* -------------------------------------------------------------------------- */

/**
 * Start CORS in worker role: NTRIP shard, RTCM, PNT, SRTK; publishes to shm.
 *
 * @param cors      CORS instance.
 * @param opt       Configuration.
 * @param shm       Attached shared memory.
 * @param worker_id Worker index.
 * @return 1 on success, 0 on failure.
 */
EXPORT int cors_start_worker(cors_t *cors, const cors_opt_t *opt,
                             cors_shm_t *shm, int worker_id);

/**
 * Start CORS in supervisor role: agent, monitor, NRTK, VRS; consumes shm.
 *
 * @param cors  CORS instance.
 * @param opt   Configuration.
 * @param shm   Attached shared memory.
 * @return 1 on success, 0 on failure.
 */
EXPORT int cors_start_supervisor(cors_t *cors, const cors_opt_t *opt, cors_shm_t *shm);

/**
 * Load NTRIP source table without starting caster threads (supervisor).
 *
 * @param ntrip        NTRIP subsystem.
 * @param cors         Parent CORS instance.
 * @param sources_file Sources list file.
 * @return Number of sources loaded, or -1 on error.
 */
EXPORT int cors_ntrip_load_sources(cors_ntrip_t *ntrip, cors_t *cors, const char *sources_file);

/**
 * Load partition sources for a worker; source ids resolved via shm table.
 *
 * @param ntrip          NTRIP subsystem.
 * @param cors           Parent CORS instance.
 * @param partition_file Worker subset file.
 * @param shm            Shared memory with global id table.
 * @param worker_id      Worker index.
 * @return Number of sources loaded, or -1 on error.
 */
EXPORT int cors_ntrip_load_partition(cors_ntrip_t *ntrip, cors_t *cors,
                                     const char *partition_file, cors_shm_t *shm,
                                     int worker_id);

/**
 * Start NTRIP caster threads after sources are loaded (worker or single-process).
 *
 * @param ntrip NTRIP subsystem with populated source table.
 * @return 1 on success, 0 on failure.
 */
EXPORT int cors_ntrip_start_casters(cors_ntrip_t *ntrip);

#ifdef __cplusplus
}
#endif
#endif /* MCORS_H */
