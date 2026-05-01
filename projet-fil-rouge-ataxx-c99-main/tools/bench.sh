#!/usr/bin/env bash
# tools/bench.sh — Minimax complexity benchmark (Linux / macOS)
#
# Mesure le nombre de noeuds visités et le temps écoulé en fonction
# de la profondeur de recherche. Produit bench_results.csv exploitable
# dans Python / Excel pour la section "analyse de complexité" du rapport.
#
# Usage :
#   bash tools/bench.sh [MIN_DEPTH=1] [MAX_DEPTH=7] [BOARD_SIZE=5] [MOVE_LIMIT=50]
#
# Exemple :
#   bash tools/bench.sh 1 6

set -euo pipefail

# Force le séparateur décimal à '.' quelle que soit la locale système.
export LC_NUMERIC=C

# ── Racine du projet ──────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
cd "$ROOT"

# ── Paramètres ────────────────────────────────────────────────────────
MIN_D="${1:-1}"
MAX_D="${2:-7}"
BOARD="${3:-5}"
LIMIT="${4:-50}"
CSV="bench_results.csv"

# ── Build ─────────────────────────────────────────────────────────────
printf '=== Building ===\n'
make -s ataxx_tournament            || make ataxx_tournament
make -s student_plugin SRC=agent_minimax.c NAME=minimax \
    || make student_plugin SRC=agent_minimax.c NAME=minimax
make -s agent_random_plugin 2>/dev/null || true   # optionnel, non bloquant
printf 'Build OK\n\n'

# ── Timer en millisecondes (python3 → perl → date secondes) ──────────
ms_now() {
    python3 -c 'import time; print(int(time.time()*1000))' 2>/dev/null && return
    perl -MTime::HiRes=time -e 'print int(time()*1000)' 2>/dev/null && return
    echo $(( $(date +%s) * 1000 ))
}

# ── En-têtes ──────────────────────────────────────────────────────────
printf 'Ataxx Minimax Benchmark  |  plateau %dx%d  |  limite %d coups\n' \
    "$BOARD" "$BOARD" "$LIMIT"
printf 'Profondeurs %d..%d\n\n' "$MIN_D" "$MAX_D"

FMT='%-7s %8s %16s %14s %14s %10s %14s\n'
# shellcheck disable=SC2059
printf "$FMT" 'Prof.' 'Coups' 'TotalNoeuds' 'MoyNoeuds' 'MaxNoeuds' 'Temps(s)' 'Noeuds/s'
# shellcheck disable=SC2059
printf "$FMT" '-----' '------' '-----------' '---------' '---------' '--------' '--------'

# ── CSV ───────────────────────────────────────────────────────────────
printf 'depth,moves,total_nodes,avg_nodes,max_nodes,min_nodes,time_s,nodes_per_s\n' \
    > "$CSV"

# ── Boucle principale ─────────────────────────────────────────────────
d="$MIN_D"
while [ "$d" -le "$MAX_D" ]; do

    TMPLOG="$(mktemp)"
    TMPREP="$(mktemp)"

    T0="$(ms_now)"
    ./ataxx_tournament \
        --size  "$BOARD" \
        --limit "$LIMIT" \
        --depth "$d" \
        --output "$TMPREP" \
        2>"$TMPLOG" >/dev/null || true
    T1="$(ms_now)"

    rm -f "$TMPREP"
    ELAPSED_MS=$(( T1 - T0 ))

    # Extraire les compteurs de noeuds depuis les lignes [minimax]
    # Format attendu : [minimax] depth=N nodes=M score=S tt_entries=K
    NODES_LIST="$(grep -o 'nodes=[0-9]*' "$TMPLOG" 2>/dev/null \
        | grep -o '[0-9]*' || true)"
    rm -f "$TMPLOG"

    if [ -z "$NODES_LIST" ]; then
        printf '%-7s  aucune sortie minimax — plugins/agent_minimax.so est-il compilé ?\n' "$d"
        d=$(( d + 1 ))
        continue
    fi

    # Statistiques via awk
    STATS="$(printf '%s\n' "$NODES_LIST" | awk '
        BEGIN { n=0; sum=0; mx=0; mn=2^53 }
        { v=$1+0; n++; sum+=v
          if (v>mx) mx=v
          if (v<mn) mn=v }
        END { printf "%d %d %.0f %d %d", n, sum, (n>0?sum/n:0), mx, mn }
    ')"
    read -r MOVES TOTAL AVG MAX MIN <<< "$STATS"

    ELAPSED_S="$(awk "BEGIN{printf \"%.3f\", $ELAPSED_MS/1000}")"
    NPS="$(awk "BEGIN{
        if ($ELAPSED_MS>0) printf \"%.0f\", $TOTAL*1000/$ELAPSED_MS
        else               print \"-\"
    }")"

    # shellcheck disable=SC2059
    printf "$FMT" "$d" "$MOVES" "$TOTAL" "$AVG" "$MAX" "${ELAPSED_S}s" "$NPS"
    printf '%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$d" "$MOVES" "$TOTAL" "$AVG" "$MAX" "$MIN" "$ELAPSED_S" "$NPS" \
        >> "$CSV"

    d=$(( d + 1 ))
done

# ── Résumé ────────────────────────────────────────────────────────────
printf '\nRésultats écrits dans %s\n\n' "$CSV"
printf 'Analyse de complexité :\n'
printf '  - Minimax pur          : O(b^d)\n'
printf '  - Alpha-beta (cas moyen): O(b^(3d/4))\n'
printf '  - Alpha-beta (meilleur) : O(b^(d/2))\n'
printf '  où b = facteur de branchement moyen, d = profondeur.\n\n'
printf 'Conseil : tracer avg_nodes en fonction de depth sur échelle logarithmique\n'
printf '  → la pente donne log(b_eff). Comparer à la borne théorique.\n'
