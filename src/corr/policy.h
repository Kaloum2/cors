/*------------------------------------------------------------------------------
 * policy.h : internal helpers for src/corr/policy.c
 *-----------------------------------------------------------------------------*/
#ifndef CORS_POLICY_I_H
#define CORS_POLICY_I_H

#include "policy.h"

int cors_user_region_contains(const cors_user_region_t *region, const double pos[3]);

#endif
