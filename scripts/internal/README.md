# Internal repository (Option A)

Public code lives on `origin` (`Kaloum2/cors`). Private governance docs, lab
credentials, and field-test scripts live in a **separate private repo**
(`cors-internal`).

## Layout

| Clone | Remote `origin` | Remote `public` | Tracks private files |
|-------|-----------------|-----------------|----------------------|
| `cors/` | public | — | no (`.gitignore`) |
| `cors-internal/` | private internal | public upstream | yes |

## One-time setup

1. Create an **empty private** GitHub repository (e.g. `Kaloum2/cors-internal`).

2. From the public clone:

```bash
./scripts/internal/bootstrap-internal-repo.sh git@github.com:Kaloum2/cors-internal.git
cd ../cors-internal
git push -u origin develop
```

3. Keep `/home/bruno/PROJECTS/cors-private-local/` as an optional offline snapshot
   (not required once `cors-internal` exists).

## Daily workflow

**Public changes** (code, MONITOR.md, CI):

```bash
cd cors
# edit, commit, push
git push origin develop
```

**Sync internal with public**:

```bash
cd ../cors-internal
./scripts/internal/sync-from-public.sh
```

**Internal-only changes** (plans, validation checklist, lab passwords):

```bash
cd cors-internal
# edit doc/PLAN_ACTIONS.md, conf/validation/agentusers-lab, etc.
git add ...
git commit -m "docs: update validation checklist"
git push origin develop
```

Never push `cors-internal` to the public remote.

## What stays where

| Content | Public `cors` | Private `cors-internal` |
|---------|:-------------:|:-----------------------:|
| `src/`, CI, Docker, MONITOR.md | yes | yes (via merge) |
| PLAN_ACTIONS, FONCTIONNALITES, tests_validation | no | yes |
| `doc/recap/`, `doc/dev/`, user_doc | no | yes |
| `conf/validation/agentusers-lab`, `*centipede*` | no | yes |
| `test/e2e/` | no | yes |

## Pre-scrub backup

Historical mirror (contains old secrets):  
`/home/bruno/PROJECTS/cors-backup-pre-scrub-20260712.git`  
Do not publish; optional reference only.
