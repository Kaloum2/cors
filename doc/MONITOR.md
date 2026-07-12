# Monitor TCP — port 7999

Contrat d'interface pour les clients monitor (rtn-platform `bridge/cors_monitor.py`, outils ops).

**Configuration :** `monitor-port = 7999` dans `conf/cors.conf`  
**Implémentation :** `src/monitor/monitor.c`

---

## Connexion

1. Ouvrir TCP vers `127.0.0.1:7999` (ou hôte Docker `cors-engine`).
2. **Envoyer une commande de subscription** — le serveur ne pousse rien avant réception client.
3. Lire la réponse (snapshot ou flux jusqu'à fermeture).

Variable rtn-platform : `CORS_MONITOR_COMMAND` (défaut `MONITOR-BSTADISTR all`).

---

## Commandes

### MONITOR-BSTADISTR

```text
MONITOR-BSTADISTR all
```

Retourne des enregistrements stations au format brace-delimited CSV :

```text
{id,address,province,city,lat,lon,height,itrf,type}
```

Exemple :

```text
{1,IPGP,IDF,Paris,48.84500,2.35600,80.0,0,0}
{2,VRS_PAR,IDF,Paris,48.85000,2.35000,75.0,0,1}
```

| Champ | Signification |
|-------|---------------|
| `id` | Identifiant numérique |
| `address` | Nom mountpoint / station |
| `province`, `city` | Métadonnées lieu |
| `lat`, `lon`, `height` | Coordonnées |
| `itrf` | Code epoch ITRF |
| `type` | `0` = station physique, `1` = VRS |

**Parser rtn-platform :** regex `_BSTADISTR_RECORD` dans `cors_monitor.py`.

Réponse **vide** (aucune station) : connexion fermée sans ligne — comportement normal, pas d'erreur.

---

### MONITOR-SOURCE

```text
MONITOR-SOURCE all
```

État des connexions sources upstream (`addsource`). Utilisé pour compter stations connectées.

---

### MONITOR-CORR

```text
MONITOR-CORR showsessions
MONITOR-CORR showmode_stats
```

Supervision des sessions modes correction (`src/corr/supervision.c`).

| Sous-commande | Contenu |
|---------------|---------|
| `showsessions` | Sessions actives (mountpoint, mode, rover, durée) |
| `showmode_stats` | Compteurs par mode (VRS, FKP, MAC, AUTO, …) |

> **Note rtn-platform :** non consommé par le bridge P1 — extension P2 pour supervision UI.

Détail : [dev/supervision-modes.md](dev/supervision-modes.md)

---

## Fallback CLI (bridge)

Si le monitor est vide ou injoignable, rtn-platform agrège via sidecar :7998 :

| Commande | Métrique extraite |
|----------|-------------------|
| `showbls` | Nombre baselines (`stat=FIX\|FLOAT\|…`) |
| `showdtrigs` | Nombre triangles (`^\s*\d+:\s+\S+`) |
| `showvsta` | VRS actives |
| `sourceinfo all` | Stations connectées |

### Exemple sortie `showbls` (référence)

```text
  IPGP-OUIL  length=  25.3 km  stat=FIX  ratio=  3.2  ...
  OUIL-GEODATA  length=  42.1 km  stat=FIX  ratio=  2.8  ...
```

### Exemple sortie `showdtrigs` (référence)

```text
  0: IPGP OUIL GEODATA
  1: OUIL GEODATA RICE
  ...
```

### Regex bridge (référence)

| Commande | Pattern | Métrique |
|----------|---------|----------|
| `showbls` | `stat=(FIX\|FLOAT\|…)` | baselines actives |
| `showdtrigs` | `^\s*\d+:\s+\S+` | triangles Delaunay |
| `showvsta` | lignes non vides | VRS configurées |
| `sourceinfo all` | `connected` / `no source` | sources upstream |

Validation automatisée lab : `PYTHONPATH=scripts python3 scripts/smoke_sidecar_int06.py`

> Ne pas modifier ces formats sans mettre à jour `bridge/cors_monitor.py` et ce document.

---

## Timeouts recommandés (clients)

| Opération | Timeout |
|-----------|---------|
| Connexion TCP | 3 s |
| Lecture snapshot | 3 s |
| CLI fallback (par commande) | 15 s |

---

## Sécurité

- Monitor :7999 écoute en **loopback** par défaut — ne pas exposer sur Internet sans tunnel.
- Pas d'authentification sur le port monitor — confiance réseau privé / Docker internal network.

Voir [SECURITY.md](SECURITY.md).

---

## Évolutions

| Version | Changement |
|---------|------------|
| v1 (actuel) | BSTADISTR, MONITOR-SOURCE, MONITOR-CORR |
| v1.1 (2026-07) | Contrat figé INT-01 ; formats CLI fallback documentés dans tests_validation.md |
| v2 (proposé) | Champ `schema_version`, export triangles JSON |

Proposition : [DECISIONS.md](DECISIONS.md) PROP-01.

---

*Référence intégration : [RTN_PLATFORM.md](RTN_PLATFORM.md)*
