/*------------------------------------------------------------------------------
 * nav_rinex.h : daily mixed RINEX NAV from combined broadcast ephemerides
 *-----------------------------------------------------------------------------*/
#ifndef CORS_NAV_RINEX_H
#define CORS_NAV_RINEX_H

#include "cors.h"

EXPORT void cors_nav_rinex_feed(const nav_t *nav, int ephsat, int ephset);
EXPORT void cors_nav_rinex_check_day(void);
EXPORT void cors_nav_rinex_fini(void);

#endif
