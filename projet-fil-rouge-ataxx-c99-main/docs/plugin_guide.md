# Guide des Plugins Agent

## De quoi s'agit-il ?

Le programme de tournoi (`ataxx_harness`) peut charger votre agent IA **à
l'exécution** à partir d'une bibliothèque partagée compilée : un fichier `.dll`
sous Windows, ou `.so` sous Linux. Cela signifie que vous pouvez soumettre
votre IA sans partager votre code source : le programme charge simplement votre
fichier compilé et appelle la fonction de votre agent pour demander des coups.

Ce guide vous accompagne pas à pas, de l'installation d'un compilateur au
chargement de votre agent dans le programme de tournoi.

---

## Table des matières

1. [Mise en place de l'environnement de compilation](#1-mise-en-place-de-lenvironnement-de-compilation)
2. [Comprendre l'interface du plugin](#2-comprendre-linterface-du-plugin)
3. [Écrire votre agent](#3-écrire-votre-agent)
4. [Compiler votre plugin](#4-compiler-votre-plugin)
5. [Charger votre plugin dans le programme](#5-charger-votre-plugin-dans-le-programme)
6. [Dépannage](#6-dépannage)
7. [Règles importantes](#7-règles-importantes)

---

## 1. Mise en place de l'environnement de compilation

Avant de pouvoir compiler quoi que ce soit, vous avez besoin d'un
**compilateur C** installé sur votre machine. Le projet utilise le standard
**C99**. Voici les instructions pour les configurations les plus courantes.

### 1.1 Windows — MinGW-w64 via MSYS2 (recommandé)

Nous recommandons d'installer GCC via **MSYS2**, car c'est simple à mettre en
place sous Windows, cela fournit aussi `make`, et l'environnement UCRT64
convient bien au projet.

#### Option A — MSYS2 via winget (recommandé)

1. Depuis **PowerShell** ou l'Invite de commandes, installez MSYS2 :

   ```
   winget install msys2.msys2
   ```

   MSYS2 s'installe par défaut dans `C:\msys64`.

2. Toujours dans **PowerShell**, placez-vous dans le dossier du projet et
   lancez le script de configuration fourni via le shell MSYS2 :

   ```powershell
   cd C:\chemin\vers\projet
   C:\msys64\msys2_shell.cmd -ucrt64 -defterm -no-start -here -c "bash tools/setup_msys2.sh"
   ```

   > **Important :** `C:\msys64\msys2_shell.cmd` lance un shell MSYS2 depuis
   > PowerShell. Ne tentez **pas** d'exécuter `bash tools/setup_msys2.sh`
   > directement dans PowerShell — les outils internes de MSYS2 ne sont
   > disponibles que dans le shell MSYS2.

   Si le terminal se ferme ou demande un redémarrage, réexécutez la même
   commande.

3. Le script met à jour MSYS2 puis installe le compilateur GCC et `make`.
   Une fois terminé, vous pouvez compiler votre plugin.

4. Pour la suite (compilation, etc.), vous avez deux options :
   - **Depuis PowerShell** : le compilateur se trouve dans
     `C:\msys64\ucrt64\bin`. Ajoutez-le à votre PATH de session :
     ```powershell
     $env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
     gcc --version
     ```
   - **Depuis le terminal MSYS2 UCRT64** : ouvrez-le via le Menu Démarrer
     (recherchez « MSYS2 UCRT64 »). GCC et `make` y sont directement
     disponibles.

> **Astuce :** Si vous utilisez le terminal MSYS2, le chemin de votre projet
> doit être au format Unix. S'il est sur OneDrive, il ressemblera à
> `/c/Users/VotreNom/OneDrive/...`.

#### Option B — Installeur indépendant MinGW-w64

1. Téléchargez la dernière version depuis
   <https://github.com/niXman/mingw-builds-binaries/releases>.
   Choisissez le fichier dont le nom contient :
   - `x86_64` (64 bits)
   - `posix` (modèle de threads)
   - `ucrt` (runtime C — recommandé sous Windows 10+)

   Par exemple : `x86_64-13.2.0-release-posix-seh-ucrt-rt_v11-rev1.7z`

2. Extrayez l'archive dans un chemin simple **sans espaces**, par exemple
   `C:\mingw64`.

3. Ajoutez le dossier `bin` à votre variable d'environnement **PATH** :
   - Ouvrez *Démarrer → Paramètres → Système → Informations système →
     Paramètres système avancés*.
   - Cliquez sur *Variables d'environnement*.
   - Sous *Variables utilisateur*, sélectionnez **Path** et cliquez sur
     *Modifier*.
   - Cliquez sur *Nouveau* et tapez `C:\mingw64\bin`.
   - Cliquez sur OK dans toutes les fenêtres.

4. Ouvrez une **nouvelle** fenêtre Invite de commandes ou PowerShell et
   vérifiez :

   ```
   gcc --version
   ```

   Vous devriez voir quelque chose comme `gcc.exe (MinGW-W64 ...) 13.2.0`.

#### Option C — via Code::Blocks (voir section 1.3 ci-dessous)

---

### 1.2 Linux (Ubuntu / Debian)

GCC est généralement pré-installé. Sinon :

```bash
sudo apt update
sudo apt install build-essential
```

Vérifiez avec :

```bash
gcc --version
```

---

### 1.3 Windows — Code::Blocks

Code::Blocks est un IDE gratuit qui intègre MinGW. Si vous préférez un
environnement graphique à la ligne de commande, suivez ces étapes.

#### Installer Code::Blocks avec MinGW

1. Allez sur <https://www.codeblocks.org/downloads/binaries/>.
2. Téléchargez l'installeur dont le nom contient **mingw-setup**
   (par ex. `codeblocks-20.03mingw-setup.exe`). Cette version inclut GCC.
3. Lancez l'installeur et acceptez les options par défaut.

#### Créer un projet pour votre plugin agent

Code::Blocks ne propose pas nativement un workflow « bibliothèque partagée en
ligne de commande ». Voici trois approches :

**Méthode A — Utiliser le terminal de Code::Blocks (recommandé)**

1. Ouvrez Code::Blocks.
2. Allez dans *Tools → Configure Tools → Add*.
3. Créez un outil appelé **Build Plugin** avec la commande :
   ```
   cmd /c "cd /d $(PROJECT_DIR) && if not exist plugins mkdir plugins && gcc -std=c99 -shared -Iinclude -o plugins\agent_monnom.dll mon_agent.c src/game.c src/avl.c"
   ```
   (Remplacez `mon_agent.c` et `monnom` par votre fichier et nom d'équipe.)
4. Lancez-le depuis *Tools → Build Plugin*.

**Méthode B — Créer un projet Code::Blocks de type bibliothèque partagée**

1. *File → New → Project → Shared Library → Go*.
2. Choisissez un titre (par ex. `agent_monnom`) et placez le dossier dans
   votre répertoire de projet.
3. Vérifiez que le compilateur sélectionné est **GNU GCC Compiler**.
4. Une fois le projet créé :
   - *Project → Properties → Build targets* :
     vérifiez que le type est **Dynamic Library**.
   - *Project → Build options → Compiler settings → Other compiler options* :
     ajoutez `-std=c99`.
   - *Project → Build options → Search directories → Compiler* :
     ajoutez le dossier `include` du projet (par ex. `..\include` ou le
     chemin absolu).
5. Ajoutez ces fichiers sources au projet (clic droit → *Add files*) :
   - Votre fichier agent (par ex. `mon_agent.c`)
   - `src/game.c`
   - `src/avl.c`
6. **N'ajoutez PAS** `src/main.c` ni `src/main_harness.c` — ils contiennent
   `main()` et provoqueront des erreurs de l'éditeur de liens dans une
   bibliothèque.
7. Compilez avec *Build → Build* (ou Ctrl+F9).
8. Copiez ensuite le fichier `.dll` produit dans le dossier `plugins/` du
   projet si votre configuration Code::Blocks l'écrit ailleurs (par ex.
   `bin/Debug/` ou `bin/Release/`). Le programme de tournoi liste les plugins
   à partir de ce dossier `plugins/`.

**Méthode C — Utiliser le GCC intégré à Code::Blocks en ligne de commande**

Le compilateur intégré à Code::Blocks se trouve généralement dans :
```
C:\Program Files\CodeBlocks\MinGW\bin\gcc.exe
```

Vous pouvez ajouter ce dossier `bin` a votre PATH (voir section 1.1, etape 3)
puis utiliser les commandes en ligne de la section 4.

#### Vérifier que le compilateur fonctionne

Ouvrez *Settings → Compiler → Toolchain executables* et vérifiez :
- **Compiler's installation directory** pointe vers un dossier MinGW valide.
- **C compiler** est réglé sur `gcc.exe`.
- **Linker for dynamic libs** est réglé sur `gcc.exe` (ou `g++.exe`).

---

### 1.4 Windows — Visual Studio (MSVC)

Le compilateur MSVC de Visual Studio peut aussi produire des fichiers `.dll`,
mais les commandes de compilation diffèrent. **Nous recommandons MinGW** pour
ce projet car le code utilise des fonctionnalités C99 et des options GCC
partout.

Si vous devez utiliser MSVC :
```
if not exist plugins mkdir plugins
cl /std:c11 /LD /Iinclude mon_agent.c src\game.c src\avl.c /Fe:plugins\agent_monnom.dll
```

> **Remarque :** MSVC ne supporte que C11 (et non C99), mais le code du projet
> est compatible. Le flag `/LD` indique à MSVC de produire une DLL. Vous devrez
> peut-être aussi ajouter `__declspec(dllexport)` devant votre fonction — voir
> la section Dépannage.

---

## 2. Comprendre l'interface du plugin

Le programme charge votre `.dll` / `.so` et recherche **un seul** symbole
(fonction) par son nom :

```
agent_choose_move
```

Votre fichier doit définir une fonction avec **exactement cette signature** :

```c
#include "game.h"
#include "agent.h"

Move agent_choose_move(const GameState *state, AgentContext *context);
```

### Signification des paramètres

| Paramètre | Type | Description |
|-----------|------|-------------|
| `state` | `const GameState *` | Le plateau actuel, le joueur courant et le compteur de tours. **Lecture seule** — ne le modifiez pas. |
| `context` | `AgentContext *` | Le harness ne règle plus la profondeur via le menu. Si votre agent a besoin d'une profondeur de recherche, définissez-la directement dans votre fonction. |

### Ce que vous retournez

Une structure `Move` :

```c
typedef struct Move {
    int from_row;   /* ligne de la pièce à déplacer (indexée à partir de 0) */
    int from_col;   /* colonne de cette pièce                               */
    int to_row;     /* ligne de destination                                  */
    int to_col;     /* colonne de destination                                */
    bool is_pass;   /* mettre à true UNIQUEMENT si aucun coup légal          */
} Move;
```

- **Clone** (distance 1) : la pièce est dupliquée — l'originale reste en
  place, une copie apparaît à la destination.
- **Saut** (distance 2) : la pièce se déplace — la case d'origine devient
  vide.
- Après le placement, toutes les pièces adverses adjacentes sont capturées.

Vous pouvez utiliser `game_generate_moves()` pour obtenir la liste de tous les
coups légaux du joueur courant :

```c
Move moves[ATAXX_MAX_MOVES];
int count = game_generate_moves(state, moves, ATAXX_MAX_MOVES);
```

Vous pouvez utiliser `game_apply_move()` sur une **copie** de l'état pour
simuler des coups :

```c
GameState copie = *state;               /* copie par valeur */
game_apply_move(&copie, un_coup);       /* modifie la copie, pas l'original */
```

---

## 3. Écrire votre agent

### Partir d'un squelette minimal

Créez un nouveau fichier `mon_agent.c` et définissez directement la fonction
exportée du plugin :

```c
Move agent_choose_move(const GameState *state, AgentContext *context)
```

Le chargeur de plugins recherche exactement ce symbole dans la DLL ou le
fichier `.so`. Vous pouvez partir de l'exemple minimal ci-dessous puis ajouter
votre évaluation, minimax, alpha-beta, etc.

### Exemple minimal

Voici le plus petit plugin valide possible (il joue simplement le premier coup
légal) :

```c
#include "game.h"
#include "agent.h"

Move agent_choose_move(const GameState *state, AgentContext *context)
{
    Move moves[ATAXX_MAX_MOVES];
    int count;
    (void)context;  /* inutilisé pour l'instant */

    count = game_generate_moves(state, moves, ATAXX_MAX_MOVES);
    if (count > 0 && !moves[0].is_pass) {
        return moves[0];
    }

    /* aucun coup légal — passer */
    {
        Move pass = {0, 0, 0, 0, true};
        return pass;
    }
}
```

### Fonctions disponibles

Votre plugin est lié au moteur de jeu, les fonctions suivantes des headers du
projet sont donc disponibles :

| Fonction | Header | Description |
|----------|--------|-------------|
| `game_generate_moves(state, moves, max)` | `game.h` | Remplit `moves[]` avec les coups légaux, retourne le nombre |
| `game_apply_move(state, move)` | `game.h` | Applique un coup à un état (le modifie). Retourne `false` si invalide |
| `game_is_terminal(state)` | `game.h` | `true` si aucun joueur ne peut jouer |
| `game_score(state, player)` | `game.h` | Compte les pièces appartenant à `player` |
| `game_hash(state)` | `game.h` | Hash 64 bits de type Zobrist du plateau |
| `player_opponent(player)` | `common.h` | Retourne l'adversaire |
| `avl_init`, `avl_insert`, `avl_find`, `avl_destroy` | `avl.h` | Arbre AVL (pour tables de transposition, etc.) |

---

## 4. Compiler votre plugin

### 4.1 Ligne de commande — Linux

```bash
cd /chemin/vers/projet
mkdir -p plugins
gcc -std=c99 -shared -fPIC -Iinclude \
   -o plugins/agent_monnom.so  mon_agent.c  src/game.c  src/avl.c
```

- `-shared` indique à GCC de produire une bibliothèque partagée au lieu d'un
  exécutable.
- `-fPIC` génère du code indépendant de la position (obligatoire pour `.so`
  sous Linux).
- `-Iinclude` indique à GCC où trouver `game.h`, `agent.h`, etc.

### 4.2 Ligne de commande — Windows (MinGW)

```bash
cd C:\chemin\vers\projet
if not exist plugins mkdir plugins
gcc -std=c99 -shared -Iinclude -o plugins\agent_monnom.dll mon_agent.c src/game.c src/avl.c
```

> Sous Windows, `-fPIC` n'est pas nécessaire (tout le code DLL Windows est
> indépendant de la position par défaut).

### 4.3 Raccourci Makefile

Si vous avez `make` installé (il est fourni avec MSYS2 et la plupart des
distributions Linux) :

```bash
make student_plugin SRC=mon_agent.c NAME=monnom
```

Cela produit `plugins/agent_monnom.dll` (Windows) ou
`plugins/agent_monnom.so` (Linux/MacOS).


### 4.4 Code::Blocks

Voir la **section 1.3** ci-dessus pour les instructions détaillées avec
Code::Blocks.

### 4.5 Vérifier votre compilation

Après la compilation, vous devriez avoir un fichier du type
`plugins/agent_monnom.dll` (ou `.so`). Vérifiez sa taille — il devrait faire
au moins quelques Ko. S'il fait 0 octet, la compilation a échoué
silencieusement.

Sous Windows, vous pouvez vérifier que votre symbole est bien exporté :

```
objdump -p plugins\agent_monnom.dll | findstr agent_choose_move
```

Sous Linux :

```bash
nm -D plugins/agent_monnom.so | grep agent_choose_move
```

Vous devriez voir une ligne contenant `agent_choose_move` avec un drapeau `T`
(ce qui signifie que c'est un symbole texte/code exporté).

---

## 5. Charger votre plugin dans le programme

1. Compilez le programme de tournoi si ce n'est pas déjà fait :
   ```
   make ataxx_harness
   ```
   Ou sous Windows sans make :
   ```
   gcc -std=c99 -Wall -O2 -Iinclude -o ataxx_harness.exe ^
         src/main_harness.c src/game.c src/avl.c src/agents/agent_random.c ^
         src/tui.c src/agent_loader.c
   ```

2. Lancez le programme :
   ```
   ./ataxx_harness        # Linux
   .\ataxx_harness.exe    # Windows (PowerShell ou Cmd)
   ```

3. Vous verrez le **Menu principal** :
   ```
   +----------------------------------------+
   |      ATAXX - Tournament Arena          |
   |                                        |
   |  Player 1 agent: Random [built-in]    |
   |  Player 2 agent: Random [built-in]    |
   |  Move limit:< 100          >          |
   |  Board size:< 5            >          |
   |  [ENTER] Start game                   |
   |                                        |
   |  [ENTER] Open plugin selection         |
   |  [ESC] Quit                           |
   +----------------------------------------+
   ```

4. Placez votre `.dll` / `.so` dans le dossier `plugins/` du projet.
   Avec le Makefile, c'est déjà le comportement par défaut.

5. Utilisez **↑ ↓** pour sélectionner la ligne *Player 1* ou *Player 2*, puis
   appuyez sur **Entrée**. Une fenêtre de sélection de plugin s'ouvre avec
   uniquement les plugins détectés dans `plugins/`.

6. Choisissez votre plugin avec **↑ ↓**, puis validez avec **Entrée**.
   Le plugin est chargé à ce moment-là et assigné directement au joueur
   sélectionné. La stratégie intégrée `Random` reste la valeur par défaut,
   mais le sélecteur de joueur ne propose que des plugins du dossier
   `plugins/`.

7. Revenez au menu principal, puis appuyez sur **Entrée** sur la ligne
   *Start game* pour lancer la partie.

### Utiliser le programme en ligne de commande

Le binaire `ataxx_cli` accepte aussi les plugins depuis le dossier
`plugins/`.

Exemples :

```bash
./ataxx_cli --p1 agent_monnom --p2 random       # Linux
.\ataxx_cli.exe --p1 agent_monnom --p2 random   # Windows
```

Vous pouvez aussi passer un chemin explicite vers le fichier `.dll` / `.so`
si nécessaire.

### Commandes pendant une partie

| Touche | Action |
|--------|--------|
| **Espace** | Passer en mode pas-à-pas (avance d'un tour à chaque appui) |
| **P** | Pause / reprendre le jeu automatique |
| **+** | Diminuer le délai (plus rapide) |
| **-** | Augmenter le délai (plus lent) |
| **Q** | Quitter vers le menu |

---

## 6. Dépannage

### « symbol 'agent_choose_move' not found »

Votre fonction n'est pas exportée. Causes fréquentes :

- **Faute de frappe dans le nom de la fonction.** Il doit être exactement
  `agent_choose_move`. Vérifiez l'orthographe et la casse.
- **La fonction exportée n'a pas le bon nom.** Elle doit s'appeler
   exactement `agent_choose_move`. Si vous repartez d'un ancien squelette avec
   un autre nom de fonction, renommez-la avant de compiler.
- **Utilisation de MSVC sans `__declspec(dllexport)`.** Si vous compilez avec
  Visual Studio, ajoutez ceci devant votre fonction :
  ```c
  __declspec(dllexport)
  Move agent_choose_move(const GameState *state, AgentContext *context)
  ```
  Avec GCC/MinGW ce n'est pas nécessaire — toutes les fonctions sont exportées
  par défaut.

### « LoadLibrary failed » / « dlopen failed »

- Vérifiez que le chemin du fichier est correct et que le fichier existe.
- Dans `ataxx_harness`, seuls les fichiers présents dans le dossier
   `plugins/` apparaissent dans le sélecteur de stratégies. Si votre plugin
   n'apparaît pas, vérifiez son emplacement et son extension.
- Sous Windows, si vous voyez le code d'erreur 126, une **dépendance est
  manquante**. Votre DLL a besoin du même runtime C que celui utilisé pour la
  compiler. Assurez-vous d'utiliser la même version de GCC pour compiler le
  programme et votre plugin.
- Avec `ataxx_cli`, vous pouvez soit passer un nom de plugin présent dans
   `plugins/`, soit un chemin explicite vers le fichier `.dll` / `.so`.

### Erreurs de compilation : « undefined reference to game_... »

Vous avez oublié d'inclure les fichiers sources du moteur dans votre commande
de compilation. Assurez-vous de compiler `src/game.c` et `src/avl.c` avec
votre fichier agent :

```
gcc ... mon_agent.c src/game.c src/avl.c
```

### Erreurs de compilation : « multiple definition of main »

Vous avez accidentellement inclus `src/main.c` ou `src/main_harness.c` dans
votre compilation. **N'incluez aucun fichier qui définit `main()`** — votre
plugin est une bibliothèque, pas un programme.

### Code::Blocks indique « gcc not found »

Allez dans *Settings → Compiler → Toolchain executables* et réglez le chemin
du compilateur sur le dossier `bin` de MinGW. Si vous avez installé
Code::Blocks avec l'installeur `mingw-setup`, il se trouve généralement dans :
```
C:\Program Files\CodeBlocks\MinGW\bin
```

### « Coup invalide » pendant la partie

Votre agent a retourné un coup qui n'est pas légal. Conseils de débogage :

- Utilisez `game_generate_moves()` pour obtenir la liste des coups légaux et
  vérifiez que vous ne retournez que l'un d'entre eux.
- Affichez des informations de débogage sur `stderr` (jamais sur `stdout`) :
  ```c
  fprintf(stderr, "DEBUG: coup choisi (%d,%d)->(%d,%d)\n",
          move.from_row, move.from_col, move.to_row, move.to_col);
  ```

---

## 7. Règles importantes

1. **Nom de la fonction** : doit être exactement `agent_choose_move`.
2. **Pas de stdout** : votre plugin ne doit **pas** écrire sur `stdout`
   (`printf`, etc.). Utilisez `fprintf(stderr, ...)` pour le débogage.
3. **Pas de stdin** : votre plugin ne doit **pas** lire depuis `stdin`
   (`scanf`, etc.).
4. **Réponse en un seul coup** : la fonction est appelée une fois par tour
   et doit retourner exactement un `Move`.
5. **Pas de fuites d'état global** : vous pouvez utiliser des variables
   `static` et `malloc` dans votre agent, mais nettoyez après vous. Le
   programme peut appeler votre fonction de nombreuses fois au cours de
   plusieurs parties.
6. **Un coup invalide = défaite** : si votre fonction retourne un coup
   illégal, la partie se termine immédiatement par une défaite pour votre
   agent.
7. **Restez rapide** : il n'y a pas de limite de temps stricte actuellement,
   mais visez moins d'une seconde par coup. Un agent qui bloque gèlera le
   programme.
8. **C99 uniquement** : évitez les fonctionnalités C++, les VLA dans les
   headers, ou les extensions spécifiques au compilateur qui ne fonctionneront
   pas avec `gcc -std=c99 -pedantic`.

---

## 8. Lancer un tournoi

Le programme `ataxx_tournament` organise automatiquement un tournoi de type
**Coupe du Monde** entre tous les plugins présents dans `plugins/` (l'agent
aléatoire intégré est toujours inclus).

### Format du tournoi

1. **Phase de groupes** — poule unique en round-robin : chaque agent affronte
   chaque adversaire deux fois (une fois en tant que Joueur 1, une fois en
   tant que Joueur 2). Points : victoire = 3, nul = 1, défaite = 0.
2. **Phase éliminatoire** — les meilleurs agents sont placés dans un tableau à
   élimination directe. Chaque confrontation se joue en aller-retour (2 matchs,
   un comme chaque joueur). Le vainqueur est décidé au score cumulé. En cas
   d'égalité, la tête de série la mieux classée avance. Un match pour la 3e
   place est également joué.

### Utilisation

```bash
# Compiler les plugins souhaités
make agent_random_plugin
make student_plugin SRC=mon_agent.c NAME=alpha_beta

# Compiler et lancer le tournoi en une commande
make tournament

# Ou lancer manuellement avec des options
./ataxx_tournament --size 5 --limit 100 --depth 3 --output results.html
```

### Options

| Option     | Défaut                   | Description                     |
|------------|--------------------------|----------------------------------|
| `--size`   | 5                        | Taille du plateau (3-9)          |
| `--limit`  | 100                      | Nombre max de tours par partie   |
| `--depth`  | 3                        | Profondeur de recherche          |
| `--output` | `tournament_report.html` | Fichier de rapport HTML          |

### Rapport HTML

Le rapport généré (`tournament_report.html` par défaut) est un fichier HTML
autonome contenant :

- Un **podium** avec le champion, le finaliste et la 3e place
- Le **classement de la phase de groupes** avec points, victoires, nuls,
  défaites, buts pour/contre et différence
- Le **tableau éliminatoire** avec les scores agrégés
- Les **détails de chaque partie** sous forme d'onglets accordéon dépliables
  qui montrent chaque coup avec l'état du plateau illustré après chaque coup
