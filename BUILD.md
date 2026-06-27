# Compilation de CORS

Guide pour compiler **cors-engine** et **cors-mengine** à partir des sources.

## Prérequis

| Outil | Version minimale | Rôle |
|-------|------------------|------|
| CMake | 3.16 | Système de build |
| Compilateur C | GCC ou Clang | Code C du projet |
| gfortran | récent | LAPACK (Fortran) |
| libsqlite3-dev | — | Base de données SQLite (Linux) |
| make / ninja | — | Backend de build |

**Linux / Ubuntu / WSL :**

```bash
sudo apt update
sudo apt install build-essential cmake gfortran libsqlite3-dev
```

**Windows :** CMake + un toolchain C/Fortran compatible (Visual Studio avec composants C++, ou [MSYS2](https://www.msys2.org/) avec `mingw-w64-x86_64-gcc`, `mingw-w64-x86_64-gfortran`, `mingw-w64-x86_64-cmake`).

## Dépendance LAPACK (obligatoire)

Le dossier `lib/lapack/` a été retiré du dépôt Git (commit `9da37f6`) pour alléger le repo. Il doit être présent **localement** avant la compilation :

```bash
git clone https://github.com/Reference-LAPACK/lapack.git lib/lapack
```

Vérifier que `lib/lapack/CMakeLists.txt` existe, puis lancer le build.

> Alternative : utiliser les binaires précompilés dans `bin/linux/` ou `bin/win/` sans compiler.

## Compilation (Linux / WSL)

Depuis la racine du projet :

```bash
mkdir -p build
cd build
cmake ..
cmake --build . -j$(nproc)
```

Build en mode Debug (débogage avec GDB) :

```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build . -j$(nproc)
```

Build en mode Release (défaut si non spécifié) :

```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j$(nproc)
```

## Compilation (Windows)

Avec Visual Studio / CMake :

```powershell
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

Avec MSYS2 MinGW64 :

```bash
mkdir -p build && cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

Les exécutables se trouvent dans `build/` (ou `build/Release/` selon le générateur).

## Cibles produites

| Cible | Description |
|-------|-------------|
| `cors-engine` | Moteur CORS principal (console interactive) |
| `cors-mengine` | Moteur multi-processus (superviseur + workers, voir [doc/MENGINE.md](doc/MENGINE.md)) |
| `cors` | Bibliothèque statique du projet |
| `uv` / `uv_a` | libuv (événements / I/O async) |
| `lapack` | LAPACK (compilé depuis `lib/lapack/`) |

Compiler une cible précise :

```bash
cmake --build build --target cors-engine
```

## Tests

Les exécutables de test sont définis dans `test/CMakeLists.txt` :

```bash
cmake --build build --target test_cors test_rtkpos test_dtrignet
./build/test_cors
```

Cibles disponibles : `test_cors`, `test_bstas_nearest_range`, `test_dtrignet`, `test_rtcm_decoder`, `test_rtcm_encoder`, `test_rtkpos`.

## cors-mengine (multi-processus)

```bash
./cors-mengine -o ../conf/cors.conf -w 2 -t 1 -s
```

Voir [doc/MENGINE.md](doc/MENGINE.md) pour l'architecture, les options et l'API.

## Lancer cors-engine après compilation

```bash
cd build
./cors-engine -o ../conf/cors.conf -t 1 -p 9000
```

Avec la configuration d'exemple locale `conf2/` :

```bash
./cors-engine -o ../conf2/cors.conf2 -t 1
```

Puis dans la console interactive :

```
cors-engine> start
cors-engine> sourceinfo all
```

Voir `bin/README.md` pour la liste complète des commandes.

Options de lancement :

```
cors-engine -o <fichier.conf> -t <niveau_trace> -p <port> -m <mode_avancé>
```

| Option | Description |
|--------|-------------|
| `-o` | Fichier de configuration |
| `-t` | Niveau de trace (0 = off, 1+ = détail croissant) |
| `-p` | Port du serveur |
| `-m` | Mode avancé (0 ou 1) |
| `-s` | Démarrage automatique du serveur |

Les fichiers de trace sont créés sous la forme `cors_engine_YYYYMMDDhhmm.trace` dans le répertoire courant.

## Binaires précompilés

Des builds prêts à l'emploi sont versionnés dans :

- `bin/linux/` — Linux (x86_64)
- `bin/win/` — Windows

```bash
cd bin/linux
./cors-engine -o ../../conf/cors.conf -t 1
```

## Structure du build

```
cors/
├── CMakeLists.txt          # Configuration principale
├── include/                # Headers publics
├── src/                    # Sources C
├── lib/
│   ├── libuv/              # Inclus dans le repo
│   ├── lapack/             # À cloner localement
│   ├── triangulator/
│   └── kdtree/
├── conf/                   # Configuration par défaut
└── build/                  # Répertoire de compilation (gitignored)
    ├── cors-engine
    └── cors-mengine
```

## Dépannage

### `add_subdirectory lib/lapack` — dossier introuvable

Cloner LAPACK (voir section [Dépendance LAPACK](#dépendance-lapack-obligatoire)).

### Erreur `sqlite3` / `pthread`

```bash
sudo apt install libsqlite3-dev
```

### Erreur `gfortran` / `quadmath`

```bash
sudo apt install gfortran
```

### Git affiche des centaines de fichiers modifiés sans changement visible

Différence de fins de ligne CRLF (Windows) vs LF (Linux). Normaliser avec un `.gitattributes` (`* text=auto eol=lf`) ou configurer Git :

```bash
# WSL / Linux
git config core.autocrlf input

# Windows natif
git config core.autocrlf true
```

### Compilation lente

La première compilation de LAPACK prend plusieurs minutes. Les builds suivants sont incrémentaux :

```bash
cmake --build build -j$(nproc)
```

## Références

- [README.md](README.md) — présentation du projet
- [bin/README.md](bin/README.md) — commandes cors-engine
- [doc/CORS软件使用简介.pdf](doc/CORS软件使用简介.pdf) — manuel d'utilisation (chinois)
- [RTKLIB](http://www.rtklib.com)
- [Reference LAPACK](https://github.com/Reference-LAPACK/lapack)
