/*
 * agent_minimax.c — Ataxx agent: negamax + alpha-beta + transposition table
 *
 * Pedagogical implementation demonstrating:
 *   1. Heuristic evaluation (piece count, corner/edge control, mobility)
 *   2. Minimax search with bounded depth (negamax formulation)
 *   3. Alpha-beta pruning
 *   4. Transposition table backed by the reference AVL tree (Zobrist hashing)
 *
 * Plugin symbol: agent_choose_move
 */

#include "agent.h"
#include "avl.h"

#include <stdio.h>
#include <stdlib.h>

/* ── Search parameters ────────────────────────────────────────────── */
#define DEFAULT_DEPTH 4

/* ── Evaluation weights ───────────────────────────────────────────── */
#define W_PIECE     10   /* piece advantage                           */
#define W_CORNER    20   /* corner square bonus                       */
#define W_EDGE       5   /* edge (non-corner) square bonus            */
#define W_MOBILITY   3   /* empty square reachable by clone           */

/* ── 8-directional neighbour offsets ─────────────────────────────── */
static const int DR[8] = {-1, -1, -1,  0,  0,  1,  1,  1};
static const int DC[8] = {-1,  0,  1, -1,  1, -1,  0,  1};

/* ── Transposition table ──────────────────────────────────────────── */
/*
 * Key = Zobrist hash XOR (depth << 48).
 * Embedding the depth in the key ensures that a score cached at depth D
 * is never reused for a search at a different depth D', which would give
 * an incorrect result (a shallow evaluation mistaken for a deep one).
 *
 * Only exact scores are stored (see 'did_cutoff' below), so retrieved
 * values are always sound and can be returned directly.
 */
static AvlTree s_tt;
static int     s_tt_ready = 0;

/* ── Diagnostics (written to stderr after each root call) ─────────── */
static long s_nodes;

/* ================================================================== */
/*  Heuristic evaluation                                              */
/* ================================================================== */

/*
 * Returns a score from the perspective of state->current_player.
 * Positive  → current player is ahead.
 * Negative  → current player is behind.
 *
 * Three criteria are combined linearly:
 *   Piece count   — primary driver of the score.
 *   Corner/edge   — stable squares that resist flipping.
 *   Clone mobility — number of distinct empty squares reachable by a
 *                    distance-1 clone; larger mobility means more options.
 */
static int evaluate(const GameState *state)
{
    Player me  = state->current_player;
    Player opp = player_opponent(me);
    int    bs  = state->board_size;
    int    r, c, k;

    int my_pieces = 0, opp_pieces = 0;
    int my_corner = 0, opp_corner = 0;
    int my_edge   = 0, opp_edge   = 0;
    int my_mob    = 0, opp_mob    = 0;

    for (r = 0; r < bs; ++r) {
        for (c = 0; c < bs; ++c) {
            Player cell   = state->board[r][c];
            int    corner = (r == 0 || r == bs - 1) && (c == 0 || c == bs - 1);
            int    edge   = !corner && (r == 0 || r == bs - 1 || c == 0 || c == bs - 1);

            if (cell == me) {
                ++my_pieces;
                my_corner += corner;
                my_edge   += edge;
            } else if (cell == opp) {
                ++opp_pieces;
                opp_corner += corner;
                opp_edge   += edge;
            } else {
                /*
                 * Empty square: count it once for each player that has at
                 * least one piece adjacent (clone reachability).
                 */
                int adj_me = 0, adj_opp = 0;
                for (k = 0; k < 8; ++k) {
                    int nr = r + DR[k], nc = c + DC[k];
                    if (nr < 0 || nr >= bs || nc < 0 || nc >= bs) continue;
                    if (state->board[nr][nc] == me)  adj_me  = 1;
                    if (state->board[nr][nc] == opp) adj_opp = 1;
                    if (adj_me && adj_opp) break;   /* short-circuit */
                }
                my_mob  += adj_me;
                opp_mob += adj_opp;
            }
        }
    }

    return W_PIECE    * (my_pieces - opp_pieces)
         + W_CORNER   * (my_corner - opp_corner)
         + W_EDGE     * (my_edge   - opp_edge)
         + W_MOBILITY * (my_mob    - opp_mob);
}

/* ================================================================== */
/*  Transposition table helpers                                       */
/* ================================================================== */

static uint64_t tt_make_key(const GameState *state, int depth)
{
    return game_hash(state) ^ ((uint64_t)(unsigned)depth << 48);
}

static int tt_get(const GameState *state, int depth, int *out)
{
    return avl_find(&s_tt, tt_make_key(state, depth), out);
}

static void tt_put(const GameState *state, int depth, int score)
{
    avl_insert(&s_tt, tt_make_key(state, depth), score);
}

/* ================================================================== */
/*  Move ordering                                                     */
/* ================================================================== */

/*
 * Partition 'moves[0..count-1]' in-place so that clone moves
 * (Chebyshev distance 1) precede jump moves (distance 2).
 *
 * Rationale: clones place a piece on the destination AND keep the
 * source, so they immediately increase the piece count and capture
 * adjacent opponent pieces.  Searching them first raises the lower
 * bound (alpha) quickly, producing more beta-cutoffs in alpha-beta.
 */
static void order_moves(Move *moves, int count)
{
    int lo = 0, hi = count - 1;

    while (lo < hi) {
        int dr, dc;

        /* Advance lo while it points at a clone. */
        dr = moves[lo].to_row - moves[lo].from_row;  if (dr < 0) dr = -dr;
        dc = moves[lo].to_col - moves[lo].from_col;  if (dc < 0) dc = -dc;
        if (!moves[lo].is_pass && dr <= 1 && dc <= 1) { ++lo; continue; }

        /* Retreat hi while it points at a jump. */
        dr = moves[hi].to_row - moves[hi].from_row;  if (dr < 0) dr = -dr;
        dc = moves[hi].to_col - moves[hi].from_col;  if (dc < 0) dc = -dc;
        if (moves[hi].is_pass || dr > 1 || dc > 1)   { --hi; continue; }

        /* lo → jump, hi → clone: swap them. */
        { Move tmp = moves[lo]; moves[lo] = moves[hi]; moves[hi] = tmp; }
        ++lo; --hi;
    }
}

/* ================================================================== */
/*  Negamax with alpha-beta pruning                                   */
/* ================================================================== */

/*
 * Returns the best score achievable from 'state' for state->current_player.
 *
 * Convention (negamax):
 *   • The caller's perspective is always "maximise my score."
 *   • Each recursive call inverts the sign (the child maximises from
 *     the opponent's point of view, which minimises from ours).
 *   • alpha: best score the maximiser has secured so far (lower bound).
 *   • beta:  best score the minimiser has secured so far (upper bound).
 *
 * Alpha-beta pruning (β-cutoff):
 *   When a child returns a score ≥ beta, the minimiser at the parent
 *   level already has a better refutation and will never choose this
 *   branch.  We prune the remaining siblings immediately.
 *
 * Only exact scores (no cutoff occurred) are cached.  Storing a bound
 * as if it were exact would cause the TT to return incorrect values and
 * break the correctness of alpha-beta at higher levels.
 */
static int negamax(const GameState *state, int depth, int alpha, int beta)
{
    Move moves[ATAXX_MAX_MOVES];
    int  count, i, best, val, cached;
    int  did_cutoff;

    ++s_nodes;

    /* ── Terminal: both players cannot move ─────────────────────── */
    if (game_is_terminal(state)) {
        int mp = game_score(state, state->current_player);
        int op = game_score(state, player_opponent(state->current_player));
        if (mp > op) return  ATAXX_INF - 1;
        if (mp < op) return -(ATAXX_INF - 1);
        return 0;
    }

    /* ── Depth limit: heuristic leaf ────────────────────────────── */
    if (depth == 0)
        return evaluate(state);

    /* ── Transposition table lookup ─────────────────────────────── */
    if (tt_get(state, depth, &cached))
        return cached;

    /* ── Generate moves ──────────────────────────────────────────── */
    count = game_generate_moves(state, moves, ATAXX_MAX_MOVES);

    /*
     * Forced pass: the current player has no real moves this turn.
     * The opponent must still receive the full remaining depth budget,
     * so depth is NOT decremented for pass moves.
     *
     * Termination guarantee: game_is_terminal (checked above) returns
     * true when both players are forced to pass, so we cannot recurse
     * indefinitely here.
     */
    if (count == 1 && moves[0].is_pass) {
        GameState child = *state;
        game_apply_move(&child, moves[0]);
        return -negamax(&child, depth, -beta, -alpha);
    }

    /* ── Alpha-beta search over real moves ──────────────────────── */
    order_moves(moves, count);

    best       = -ATAXX_INF;
    did_cutoff = 0;

    for (i = 0; i < count; ++i) {
        GameState child = *state;           /* copy-on-write: no undo needed */
        game_apply_move(&child, moves[i]);
        val = -negamax(&child, depth - 1, -beta, -alpha);

        if (val > best)  best  = val;
        if (val > alpha) alpha = val;
        if (alpha >= beta) {
            did_cutoff = 1;
            break;  /* β-cutoff: remaining siblings are irrelevant */
        }
    }

    /* Cache only exact scores so the TT stays sound. */
    if (!did_cutoff)
        tt_put(state, depth, best);

    return best;
}

/* ================================================================== */
/*  Plugin entry point                                                */
/* ================================================================== */

/*
 * Called once per turn by the harness.
 * Returns the move judged best by negamax at 'depth_limit' plies.
 *
 * The transposition table is reset at the start of each call so that
 * stale entries from a previous game cannot corrupt the current search.
 * Within a single call, the TT eliminates redundant re-evaluation of
 * positions reachable via multiple move sequences (transpositions).
 */
Move agent_choose_move(const GameState *state, AgentContext *context)
{
    Move moves[ATAXX_MAX_MOVES];
    int  count, i, depth, best_score, val;
    Move best_move;

    depth = (context && context->depth_limit > 0)
            ? context->depth_limit
            : DEFAULT_DEPTH;

    /* Reset transposition table. */
    if (!s_tt_ready) {
        avl_init(&s_tt);
        s_tt_ready = 1;
    } else {
        avl_destroy(&s_tt);
        avl_init(&s_tt);
    }
    s_nodes = 0;

    count = game_generate_moves(state, moves, ATAXX_MAX_MOVES);

    /* Forced pass at root: no choice available. */
    if (count == 1 && moves[0].is_pass)
        return moves[0];

    /* ── Root search ─────────────────────────────────────────────── */
    /*
     * We call negamax with a full [-INF, +INF] window at the root so
     * that every child receives an exact score, not a bound.  This lets
     * us compare scores across root moves reliably.
     *
     * Move ordering is applied here too so that stronger moves are
     * explored first; if two moves share the same score, the first one
     * found (a clone, after ordering) is kept as best_move.
     */
    order_moves(moves, count);

    best_score = -ATAXX_INF - 1;
    best_move  = moves[0];   /* safe fallback */

    for (i = 0; i < count; ++i) {
        GameState child = *state;
        game_apply_move(&child, moves[i]);
        val = -negamax(&child, depth - 1, -ATAXX_INF, ATAXX_INF);
        if (val > best_score) {
            best_score = val;
            best_move  = moves[i];
        }
    }

    fprintf(stderr, "[minimax] depth=%d nodes=%ld score=%d tt_entries=%zu\n",
            depth, s_nodes, best_score, s_tt.size);

    return best_move;
}
