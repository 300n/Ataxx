# Projet Fil Rouge - IA de jeu Ataxx en C99

Projet pedagogique en C99 autour d'un environnement de jeu de type Ataxx.
Le depot contient un moteur minimal, une interface de test, une AVL de reference,
et l'infrastructure necessaire pour developper et evaluer un agent base sur minimax
et l'elagage alpha-beta.

## Contenu

- `src/` : moteur, chargeur d'agents, TUI et agents fournis
- `include/` : interfaces publiques
- `tests/` : tests unitaires simples
- `docs/` : sujet, protocole et guide des plugins
- `tools/` : scripts utilitaires

## Compilation

Sous Windows ou Linux, depuis le dossier `project/` :

```sh
make
```

Cibles utiles :

- `make ataxx_cli`
- `make ataxx_harness`
- `make ataxx_tournament`
- `make test_avl`
- `make test_tui`
- `make student_plugin SRC=my_agent.c NAME=custom`
- `make tournament` — build and run the tournament in one step

Le depot ne fournit plus d'agent etudiant integre. Les strategies etudiantes
sont attendues sous forme de plugins construits avec `student_plugin`.

## Objectif pedagogique

Le travail attendu porte principalement sur :

- une fonction d'evaluation argumentee
- un minimax a profondeur bornee
- un alpha-beta correct
- l'integration d'une structure equilibree
- une evaluation experimentale et une analyse de complexite

Le detail du sujet est disponible dans `docs/project_subject.md`.

## Tournoi

Le programme `ataxx_tournament` organise un tournoi de type Coupe du Monde entre
tous les agents presents dans le dossier `plugins/` (plus l'agent aleatoire
integre).

### Format

1. **Phase de groupes** : poule unique, chaque agent affronte chaque adversaire
   deux fois (une fois en tant que Joueur 1, une fois en tant que Joueur 2).
   Victoire = 3 pts, nul = 1 pt, defaite = 0 pt.
2. **Phase eliminatoire** : les meilleurs agents sont places dans un tableau a
   elimination directe (quarts, demi-finales, finale). Chaque confrontation se
   joue en aller-retour (2 matchs), le vainqueur est decide au score cumule.
   Un match pour la 3e place est egalement joue.

### Lancer un tournoi

```sh
# 1. Compiler les plugins dans plugins/
make agent_random_plugin
make student_plugin SRC=my_agent.c NAME=alpha_beta
# etc.

# 2. Compiler et lancer le tournoi
make tournament

# Ou avec des options personnalisees
./ataxx_tournament --size 5 --limit 100 --depth 3 --output results.html
```

### Options

| Option     | Defaut                   | Description                      |
|------------|--------------------------|----------------------------------|
| `--size`   | 5                        | Taille du plateau (3-9)          |
| `--limit`  | 100                      | Nombre max de tours par partie   |
| `--depth`  | 3                        | Profondeur de recherche          |
| `--output` | `tournament_report.html` | Fichier de rapport HTML          |

### Rapport HTML

Le rapport genere contient :

- **Podium** avec le champion, le finaliste et la 3e place
- **Classement de la phase de groupes** (points, victoires, nuls, defaites,
  buts pour/contre, difference)
- **Tableau eliminatoire** avec les resultats agrege
- **Details de chaque partie** sous forme d'accordeons depliables montrant
  chaque coup avec le plateau illustre apres le coup