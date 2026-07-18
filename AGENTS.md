# AGENTS.md — CORS Engine

Guide pour agents IA (Cursor, Claude Code, Codex, etc.).

## Projet

Moteur network RTK/VRS en C (`cors-engine`). Dépôt autonome, source de vérité pour rtn-platform.

## Démarrage rapide

```bash
# Explorer (obligatoire avant grep/read massif)
graphify query "<question architecture>"
graphify path "ntripagent.c" "corr.c"

# Build
mkdir -p build && cd build && cmake .. && cmake --build . -j$(nproc) --target cors-engine

# Tests rapides
./test/test_rtcm_fkp
```

## Docs essentielles

| Doc | Usage |
|-----|-------|
| `doc/PLAN_ACTIONS.md` | Priorités courantes |
| `doc/DECISIONS.md` | ADR — ne pas violer |
| `doc/FONCTIONNALITES.md` | État livraison |
| `doc/dev/architecture-modes.md` | Spec modes corr |
| `graphify-out/GRAPH_REPORT.md` | Cartographie code |

## Règles Cursor

`.cursor/rules/` — voir `doc/README.md` § Règles Cursor

**Importées** (awesome-cursorrules) : `.cursor/rules/imported/` — anti-overengineering, anti-sycophancy, clean-code, cpp, docker, python, gitflow, security-devsecops, …

## Skills projet (`.cursor/skills/`)

| Skill | Usage |
|-------|-------|
| `graphify-explore` | Cartographie avant exploration |
| `security-review` | `/review-security` — policy, sidecar |
| `bugbot-review` | `/review-bugbot` — bugs, régressions NTRIP |
| `babysit-pr` | PR merge-ready, CI |
| `centipede-idf-test` | Validation terrain IDF |

Skills globales Cursor : `~/.cursor/skills-cursor/` (review, babysit, canvas, sdk, …)

## Interdits

- Modifier `lib/` vendored sans demande
- Committer secrets ou créer commits sans demande
- Logique modes corr dans `ntripagent.c` (utiliser `src/corr/`)
- Casser contrats monitor/CLI documentés dans `doc/MONITOR.md`

## Après changements code

```bash
graphify update .
```
