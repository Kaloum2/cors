/*------------------------------------------------------------------------------
 * source_rinex.h : daily RINEX OBS from physical NTRIP source stations
 *-----------------------------------------------------------------------------*/
#ifndef CORS_SOURCE_RINEX_H
#define CORS_SOURCE_RINEX_H

#include "cors.h"

EXPORT void cors_source_rinex_obs(int srcid, const char *name,
                                  const obsd_t *obs, int n,
                                  const nav_t *nav, const double pos[3]);
EXPORT void cors_source_rinex_nav(int srcid, const nav_t *nav, int ephsat, int ephset);
EXPORT void cors_source_rinex_sta(int srcid, const sta_t *sta);
EXPORT void cors_source_rinex_end(int srcid);
EXPORT void cors_source_rinex_end_by_name(const char *name);
EXPORT void cors_source_rinex_sync_disk(void);
EXPORT void cors_source_rinex_fini(void);

#endif
