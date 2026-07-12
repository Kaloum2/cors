/*------------------------------------------------------------------------------
 * corr.h : correction service layer — modes, mountpoints, handlers
 *
 * Couche d'abstraction entre l'agent NTRIP et les producteurs RTCM (VRS,
 * NRTK, FKP, relay…). Spécification canonique : doc/dev/architecture-modes.md
 *
 * author  : cors-engine
 * version : $Revision:$ $Date:$
 * history : 2026/06/27 1.0  new (spec only — implémentation progressive)
 *-----------------------------------------------------------------------------*/
#ifndef CORR_H
#define CORR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cors.h"

/* -------------------------------------------------------------------------- */
/* Modes de correction                                                        */
/* -------------------------------------------------------------------------- */

typedef enum cors_corr_mode {
    CORS_CORR_RELAY        = 0,  /* relay RTCM d'une base physique nommée      */
    CORS_CORR_NEAR         = 1,  /* base physique la plus proche (GGA)       */
    CORS_CORR_VRS_FIXED    = 2,  /* VRS à position fixe (addvsta)            */
    CORS_CORR_VRS_DYNAMIC  = 3,  /* VRS à la position GGA du rover           */
    CORS_CORR_FKP          = 4,  /* FKP réseau (RTCM 1034/1035)              */
    CORS_CORR_MAC          = 5,  /* Master-Auxiliary Concept                 */
    CORS_CORR_IMAX         = 6,  /* iMAX Trimble (phase 2, stub)             */
    CORS_CORR_AUTO         = 7   /* sélection automatique (mountpoint AUTO)  */
} cors_corr_mode_t;

#define CORS_CORR_MODE_MAX  8

/* Résultat de la politique utilisateur (auth + droits + région) */
typedef enum cors_corr_policy_result {
    CORS_CORR_POLICY_OK       = 0,
    CORS_CORR_POLICY_UNAUTH   = 1,
    CORS_CORR_POLICY_FORBID   = 2,  /* mode non autorisé pour l'utilisateur   */
    CORS_CORR_POLICY_REGION   = 3,  /* hors zone autorisée                    */
    CORS_CORR_POLICY_QUOTA    = 4,  /* quota sessions / débit dépassé         */
    CORS_CORR_POLICY_UNKNOWN  = 5   /* mountpoint inconnu                     */
} cors_corr_policy_result_t;

/* Registre d'un mountpoint NTRIP (fichier conf/mountpoints) */
typedef struct cors_mountpoint_def {
    char name[32];              /* nom mountpoint NTRIP (ex. RTCM32, VRS)   */
    cors_corr_mode_t mode;      /* mode de service                            */
    char format[32];            /* format RTCM annoncé (sourcetable)          */
    char source[64];            /* RELAY : nom source physique dans info_tbl  */
    char priority[64];          /* AUTO  : chaîne priorité (VRS,MAC,FKP,…)   */
    char desc[128];             /* description libre (sourcetable)            */
    int  legacy_type;           /* rétrocompat conn->type (0=auto, 1, 2)     */
    UT_hash_handle hh;
} cors_mountpoint_def_t;

typedef struct cors_corr_session cors_corr_session_t;

/* Vtable d'un handler de correction */
typedef struct cors_corr_service {
    cors_corr_mode_t mode;
    const char      *name;      /* identifiant lisible (ex. "vrs_dynamic")   */

    int  (*attach)(cors_corr_session_t *sess);
    int  (*on_gga)(cors_corr_session_t *sess, const double pos[3]);
    int  (*produce)(cors_corr_session_t *sess, uint8_t *buf, int max_len,
                    const nav_t *nav);
    void (*detach)(cors_corr_session_t *sess);

    /* Ligne STR sourcetable NTRIP pour ce mode (retourne nb octets écrits) */
    int  (*sourcetable_str)(const struct cors_corr_service *svc,
                            const cors_mountpoint_def_t *mntdef,
                            char *buf, int max_len);
} cors_corr_service_t;

/* Session de correction liée à une connexion NTRIP agent */
struct cors_corr_session {
    cors_ntrip_conn_t      *conn;       /* connexion agent (non possédée)      */
    cors_corr_mode_t        mode;       /* mode effectif (AUTO → mode résolu)  */
    cors_corr_mode_t        req_mode;   /* mode demandé par le mountpoint      */
    const cors_mountpoint_def_t *mntdef; /* entrée registre (NULL si legacy)   */
    const cors_corr_service_t   *svc;   /* handler actif                       */
    void                   *priv;       /* état interne du handler               */
    double                  pos[3];     /* dernière position rover (ecef)        */
    gtime_t                 time_gga;   /* horodatage dernière GGA             */
    int                     attached;   /* attach() a réussi                     */
    UT_hash_handle          hh;         /* index sessions actives (futur)      */
};

/* Registre global des mountpoints chargé depuis la configuration */
typedef struct cors_corr_registry {
    cors_mountpoint_def_t *mnt_tbl;     /* hash par name (uthash)               */
    int                    n_mnt;       /* nombre d'entrées explicites          */
} cors_corr_registry_t;

/* Contexte de résolution (agent + registre + cors engine) */
typedef struct cors_corr_ctx {
    cors_ntrip_agent_t   *agent;
    cors_corr_registry_t *registry;
    cors_t               *cors;
} cors_corr_ctx_t;

typedef struct cors_corr_cfg {
    double vrs_dynamic_move_thresh_m;
    int    auto_vrs_min_fixed_bls;
    int    auto_mac_min_fixed_bls;
    int    auto_fkp_min_stations;
    int    source_rinex_enable;
    char   source_rinex_dir[MAXSTRPATH];
    double source_rinex_flush_sec; /* periodic NAV flush; 0=midnight/shutdown only; max 86400 */
} cors_corr_cfg_t;

#define CORS_RINEX_FLUSH_SEC_MAX  86400.0

/* Profil utilisateur étendu (évolution future de conf/agentuser) */
typedef struct cors_corr_user_policy {
    char user[64];
    char allowed_modes[128];    /* liste « VRS|FKP|RELAY » ou « * »           */
    char region[32];            /* identifiant région (ex. IDF)               */
    int  max_sessions;          /* 0 = illimité                               */
    UT_hash_handle hh;
} cors_corr_user_policy_t;

/* -------------------------------------------------------------------------- */
/* Rétrocompatibilité ntripagent.c (conn->type binaire actuel)                */
/* -------------------------------------------------------------------------- */

#define CORS_CORR_LEGACY_TYPE_RELAY   1   /* info_tbl[0] match (relay ou VRS fixe) */
#define CORS_CORR_LEGACY_TYPE_NEAR    2   /* mountpoint RTCM32                       */

/* Mappe conn->type + nom mountpoint vers un mode canonique */
EXPORT cors_corr_mode_t cors_corr_mode_from_legacy(int conn_type,
                                                   const char *mntpnt,
                                                   int is_vrs_fixed);

/* Valeur legacy pour un mode (0 si pas de mapping direct) */
EXPORT int cors_corr_legacy_type_for_mode(cors_corr_mode_t mode);

/* -------------------------------------------------------------------------- */
/* Registre mountpoints                                                       */
/* -------------------------------------------------------------------------- */

EXPORT int  cors_corr_registry_init(cors_corr_registry_t *reg);
EXPORT void cors_corr_registry_free(cors_corr_registry_t *reg);

/* Charge conf/mountpoints ; fusionne entrées explicites + défauts legacy */
EXPORT int  cors_corr_registry_load(cors_corr_registry_t *reg,
                                    const char *file);

EXPORT const cors_mountpoint_def_t *
            cors_corr_registry_find(const cors_corr_registry_t *reg,
                                    const char *mntpnt);

/* Ajoute ou remplace une entrée (runtime, tests) */
EXPORT int  cors_corr_registry_add(cors_corr_registry_t *reg,
                                   const cors_mountpoint_def_t *def);

/* -------------------------------------------------------------------------- */
/* Résolution mountpoint → service                                            */
/* -------------------------------------------------------------------------- */

/*
 * Résout un nom de mountpoint :
 *   1. entrée explicite dans le registre ;
 *   2. sinon nom présent dans info_tbl → RELAY ou VRS_FIXED ;
 *   3. sinon RTCM32 → NEAR ;
 *   4. sinon NULL (mountpoint inconnu).
 *
 * @param is_vrs_fixed 1 si le nom correspond à une station virtuelle addvsta.
 * @param scratch      Buffer sortie si résolution legacy (non alloué).
 * @return Pointeur registre ou scratch ; NULL si inconnu.
 */
EXPORT const cors_mountpoint_def_t *
            cors_corr_resolve_mountpoint(const cors_corr_ctx_t *ctx,
                                           const char *mntpnt,
                                           int is_vrs_fixed,
                                           cors_mountpoint_def_t *scratch);

/* Retourne le handler (vtable) pour un mode donné ; NULL si non implémenté */
EXPORT const cors_corr_service_t *
            cors_corr_service_for_mode(cors_corr_mode_t mode);

/* Chaîne lisible du mode (logs, traces) */
EXPORT const char *cors_corr_mode_str(cors_corr_mode_t mode);

/* Parse mode depuis chaîne config (type=vrs_dynamic, fkp, …) */
EXPORT cors_corr_mode_t cors_corr_mode_parse(const char *str);

/* -------------------------------------------------------------------------- */
/* Politique utilisateur (lab : conf/agentusers + policy.c)                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Vérifie droits utilisateur avant ou pendant une session rover.
 * @param ctx             Contexte corr (registre + agent + moteur).
 * @param user            Nom utilisateur NTRIP (après auth Basic).
 * @param mode            Mode de correction demandé (VRS, FKP, …).
 * @param pos             Position rover ECEF (3) ; NULL si inconnue.
 * @param active_sessions Nombre de sessions actives pour quota ; <0 ignore le quota.
 * @return CORS_CORR_POLICY_OK ou code de refus (→ HTTP 403 côté agent).
 */
EXPORT cors_corr_policy_result_t
            cors_corr_policy_check(const cors_corr_ctx_t *ctx,
                                   const char *user,
                                   cors_corr_mode_t mode,
                                   const double pos[3],
                                   int active_sessions);

/** @brief Libellé lisible d'un résultat policy (logs, traces lab). */
EXPORT const char *cors_corr_policy_result_str(cors_corr_policy_result_t result);

/**
 * @brief Résout le mode effectif d'un mountpoint (registre + legacy conn->type).
 * @param legacy_conn_type Valeur conn->type héritée (1=relay, 2=near, 3=corr).
 */
EXPORT cors_corr_mode_t cors_corr_mode_for_mountpoint(const cors_corr_ctx_t *ctx,
                                                      const char *mntpnt,
                                                      int legacy_conn_type);

/* -------------------------------------------------------------------------- */
/* Cycle de vie session (appelé par ntripagent sur chaque connexion rover)    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Attache un handler de correction à une connexion authentifiée.
 * @return 1 si attach() du service a réussi, 0 sinon (policy ou handler absent).
 */
EXPORT int  cors_corr_session_attach(cors_corr_session_t *sess,
                                     const cors_corr_ctx_t *ctx,
                                     cors_ntrip_conn_t *conn,
                                     const cors_mountpoint_def_t *mntdef);

/** @brief Met à jour la position rover (GGA décodée) et notifie le handler. */
EXPORT int  cors_corr_session_on_gga(cors_corr_session_t *sess,
                                     const double pos[3]);

/** @brief Produit un paquet RTCM ; retourne nb octets écrits dans buf (0 si rien). */
EXPORT int  cors_corr_session_produce(cors_corr_session_t *sess,
                                      uint8_t *buf, int max_len,
                                      const nav_t *nav);

/** @brief Libère l'état handler (detach) ; idempotent. */
EXPORT void cors_corr_session_detach(cors_corr_session_t *sess);

/* -------------------------------------------------------------------------- */
/* Sourcetable NTRIP                                                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief Construit le corps SOURCETABLE filtré par policy utilisateur.
 * @param user NULL = liste publique (sans filtre modes/région).
 * @return Nombre d'octets écrits dans buf (sans en-tête HTTP).
 */
EXPORT int  cors_corr_sourcetable_build(const cors_corr_ctx_t *ctx,
                                        const cors_ntrip_user_t *user,
                                        char *buf, int max_len);

/* -------------------------------------------------------------------------- */
/* Initialisation moteur (lab : cors_start → cors_corr_init)                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialise registre mountpoints, policy et supervision.
 * Chemins lus depuis cors->opt (mountpoints-file, corr-conf-file).
 */
EXPORT int  cors_corr_init(cors_t *cors, cors_ntrip_agent_t *agent);

/** @brief Arrête sessions actives et libère registre (shutdown propre). */
EXPORT void cors_corr_close(void);

/** @brief Accès lecture au contexte global (NULL avant cors_corr_init). */
EXPORT const cors_corr_ctx_t *cors_corr_ctx_get(void);

/* -------------------------------------------------------------------------- */
/* Helpers connexion NTRIP (wrappers session par connexion rover)             */
/* -------------------------------------------------------------------------- */

/** @brief Démarre une session corr après ICY 200 OK (mountpoint résolu). */
EXPORT int  cors_corr_conn_begin(cors_ntrip_conn_t *conn,
                                 const cors_mountpoint_def_t *mntdef);

/** @brief Relaye une GGA vers la session (VRS dyn, NEAR, géofence). */
EXPORT int  cors_corr_conn_gga(cors_ntrip_conn_t *conn, const double *pos);

/** @brief Termine la session et retire l'entrée du registre actif. */
EXPORT void cors_corr_conn_end(cors_ntrip_conn_t *conn);

/** @brief Nom mountpoint effectif (AUTO résolu) dans mntpnt[n]. */
EXPORT int  cors_corr_conn_output(cors_ntrip_conn_t *conn, char *mntpnt, int n);

/** @brief Encode et envoie RTCM pour cette connexion si données disponibles. */
EXPORT int  cors_corr_conn_push(cors_ntrip_conn_t *conn);

/** @brief Configuration corr chargée depuis conf/corr.conf. */
EXPORT const cors_corr_cfg_t *cors_corr_cfg_get(void);

#ifdef __cplusplus
}
#endif
#endif /* CORR_H */
