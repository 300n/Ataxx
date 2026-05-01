/* ================================================================== */
/*  Ataxx Tournament Engine                                           */
/*  ----------------------------------------------------------------  */
/*  Discovers all agent plugins in plugins/, runs a World-Cup-style   */
/*  tournament (round-robin group stage then single-elimination       */
/*  knockout), and writes a self-contained HTML report.               */
/* ================================================================== */

#include "agent.h"
#include "agent_loader.h"
#include "game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#endif

/* ================================================================== */
/*  Constants                                                         */
/* ================================================================== */

#define MAX_AGENTS 32
#define MAX_GAMES 2048
#define PLUGINS_DIR "plugins"
#define DEFAULT_LIMIT 100
#define DEFAULT_DEPTH 3
#define DEFAULT_OUTPUT "tournament_report.html"

/* ================================================================== */
/*  Types                                                             */
/* ================================================================== */

/* One recorded move + resulting board */
typedef struct
{
  Move move;
  GameState state_after;
} MoveRecord;

/* Full record of a single game */
typedef struct
{
  int p1_idx; /* index into agents[] */
  int p2_idx;
  int p1_score;
  int p2_score;
  MoveRecord *moves;
  int move_count;
  char phase_label[80]; /* e.g. "Group Stage" / "Semi-final 1" */
} GameRecord;

/* An agent entry (plugin or built-in random) */
#define AGENT_KIND_RANDOM 0
#define AGENT_KIND_PLUGIN 1

typedef struct
{
  int kind;
  AgentPlugin plugin; /* only if kind == AGENT_KIND_PLUGIN */
  char name[64];
} AgentEntry;

/* Standing in the group stage */
typedef struct
{
  int idx; /* agent index */
  int played, wins, draws, losses;
  int points;
  int score_for, score_against;
} Standing;

/* Knockout match */
typedef struct
{
  int seed1; /* agent index (-1 = bye) */
  int seed2;
  int game_id_leg1; /* index into all_games[] */
  int game_id_leg2;
  int agg1, agg2; /* aggregate scores        */
  int winner;     /* agent index of winner   */
} KnockoutMatch;

/* ================================================================== */
/*  Globals                                                           */
/* ================================================================== */

static AgentEntry agents[MAX_AGENTS];
static int agent_count = 0;

static GameRecord all_games[MAX_GAMES];
static int game_count = 0;

static Standing standings[MAX_AGENTS];
static int standing_count = 0;

/* Knockout bracket: up to 5 rounds (32 -> 16 -> 8 -> 4 -> 2 -> 1) */
#define MAX_KO_ROUNDS 6
static KnockoutMatch ko_bracket[MAX_KO_ROUNDS][MAX_AGENTS / 2];
static int ko_round_size[MAX_KO_ROUNDS];
static int ko_rounds = 0;

/* Third-place match */
static KnockoutMatch third_place_match;
static int has_third_place = 0;

/* Settings */
static int cfg_board_size = ATAXX_BOARD_SIZE;
static int cfg_move_limit = DEFAULT_LIMIT;
static int cfg_depth = DEFAULT_DEPTH;
static char cfg_output[512] = DEFAULT_OUTPUT;

/* ================================================================== */
/*  Agent helpers                                                     */
/* ================================================================== */

static Move agent_get_move(int idx, const GameState *state, AgentContext *ctx)
{
  if (agents[idx].kind == AGENT_KIND_RANDOM)
    return agent_random_choose_move(state);
  return agents[idx].plugin.choose_move(state, ctx);
}

static const char *agent_name(int idx)
{
  return agents[idx].name;
}

/* ================================================================== */
/*  Plugin discovery (cross-platform)                                 */
/* ================================================================== */

static void extract_name(const char *filename, char *buf, size_t bsz)
{
  const char *dot = strrchr(filename, '.');
  size_t len = dot ? (size_t)(dot - filename) : strlen(filename);
  if (len >= bsz)
    len = bsz - 1;
  memcpy(buf, filename, len);
  buf[len] = '\0';
}

static void discover_agents(void)
{
  /* Always include the built-in random agent */
  agents[0].kind = AGENT_KIND_RANDOM;
  snprintf(agents[0].name, sizeof(agents[0].name), "random");
  agent_count = 1;

#ifdef _WIN32
  {
    WIN32_FIND_DATAA fd;
    HANDLE hFind;
    char pattern[280];
    snprintf(pattern, sizeof(pattern), "%s\\*.dll", PLUGINS_DIR);
    hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
      return;
    do
    {
      char path[280];
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        continue;
      if (agent_count >= MAX_AGENTS)
        break;
      snprintf(path, sizeof(path), "%s\\%s", PLUGINS_DIR, fd.cFileName);
      agents[agent_count].kind = AGENT_KIND_PLUGIN;
      if (agent_plugin_load(&agents[agent_count].plugin, path))
      {
        extract_name(fd.cFileName, agents[agent_count].name, 64);
        printf("  Loaded: %s\n", agents[agent_count].name);
        ++agent_count;
      }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
  }
#else
  {
    DIR *dir = opendir(PLUGINS_DIR);
    struct dirent *ent;
    if (!dir)
      return;
    while ((ent = readdir(dir)) != NULL && agent_count < MAX_AGENTS)
    {
      char path[280];
      size_t nlen = strlen(ent->d_name);
      if (nlen <= 3 || strcmp(ent->d_name + nlen - 3, ".so") != 0)
        continue;
      snprintf(path, sizeof(path), "%s/%s", PLUGINS_DIR, ent->d_name);
      agents[agent_count].kind = AGENT_KIND_PLUGIN;
      if (agent_plugin_load(&agents[agent_count].plugin, path))
      {
        extract_name(ent->d_name, agents[agent_count].name, 64);
        printf("  Loaded: %s\n", agents[agent_count].name);
        ++agent_count;
      }
    }
    closedir(dir);
  }
#endif
}

/* ================================================================== */
/*  Play a single game, recording every move                          */
/* ================================================================== */

static int play_game(int p1, int p2, const char *label)
{
  GameState state;
  AgentContext ctx;
  GameRecord *gr;
  int id;

  if (game_count >= MAX_GAMES)
  {
    fprintf(stderr, "Too many games!\n");
    return -1;
  }

  id = game_count++;
  gr = &all_games[id];
  gr->p1_idx = p1;
  gr->p2_idx = p2;
  snprintf(gr->phase_label, sizeof(gr->phase_label), "%s", label);

  /* Allocate move buffer */
  gr->moves = (MoveRecord *)calloc((size_t)cfg_move_limit + 2, sizeof(MoveRecord));
  gr->move_count = 0;

  ctx.depth_limit = cfg_depth;
  game_init(&state, cfg_board_size);

  while (!game_is_terminal(&state) && state.turn_count < cfg_move_limit)
  {
    Move move;
    if (state.current_player == PLAYER_ONE)
      move = agent_get_move(p1, &state, &ctx);
    else
      move = agent_get_move(p2, &state, &ctx);

    if (!game_apply_move(&state, move))
    {
      fprintf(stderr, "Invalid move in game %d (turn %d)\n",
              id, state.turn_count);
      break;
    }

    /* Record */
    gr->moves[gr->move_count].move = move;
    gr->moves[gr->move_count].state_after = state;
    gr->move_count++;
  }

  gr->p1_score = game_score(&state, PLAYER_ONE);
  gr->p2_score = game_score(&state, PLAYER_TWO);
  return id;
}

/* ================================================================== */
/*  Group stage: full round-robin                                     */
/* ================================================================== */

static void run_group_stage(void)
{
  int i, j;
  char label[80];
  int match_num = 0;

  printf("\n=== GROUP STAGE (round-robin) ===\n");

  /* Initialise standings */
  standing_count = agent_count;
  for (i = 0; i < agent_count; ++i)
  {
    standings[i].idx = i;
    standings[i].played = standings[i].wins = 0;
    standings[i].draws = standings[i].losses = 0;
    standings[i].points = 0;
    standings[i].score_for = standings[i].score_against = 0;
  }

  /* Each pair plays twice (home & away) */
  for (i = 0; i < agent_count; ++i)
  {
    for (j = 0; j < agent_count; ++j)
    {
      GameRecord *gr;
      int gid;
      if (i == j)
        continue;
      ++match_num;
      snprintf(label, sizeof(label), "Group Match %d", match_num);
      printf("  %s vs %s (P1 vs P2)...",
             agent_name(i), agent_name(j));
      fflush(stdout);

      gid = play_game(i, j, label);
      if (gid < 0)
        continue;
      gr = &all_games[gid];

      printf(" %d-%d\n", gr->p1_score, gr->p2_score);

      /* Update standings for i (as P1) */
      standings[i].played++;
      standings[i].score_for += gr->p1_score;
      standings[i].score_against += gr->p2_score;

      /* Update standings for j (as P2) */
      standings[j].played++;
      standings[j].score_for += gr->p2_score;
      standings[j].score_against += gr->p1_score;

      if (gr->p1_score > gr->p2_score)
      {
        standings[i].wins++;
        standings[i].points += 3;
        standings[j].losses++;
      }
      else if (gr->p1_score < gr->p2_score)
      {
        standings[j].wins++;
        standings[j].points += 3;
        standings[i].losses++;
      }
      else
      {
        standings[i].draws++;
        standings[i].points += 1;
        standings[j].draws++;
        standings[j].points += 1;
      }
    }
  }
}

/* standing comparison for qsort */
static int cmp_standing(const void *a, const void *b)
{
  const Standing *sa = (const Standing *)a;
  const Standing *sb = (const Standing *)b;
  /* Higher points first */
  if (sb->points != sa->points)
    return sb->points - sa->points;
  /* Then score difference */
  int diff_a = sa->score_for - sa->score_against;
  int diff_b = sb->score_for - sb->score_against;
  if (diff_b != diff_a)
    return diff_b - diff_a;
  /* Then total score */
  return sb->score_for - sa->score_for;
}

static void sort_standings(void)
{
  qsort(standings, (size_t)standing_count, sizeof(Standing), cmp_standing);
}

/* ================================================================== */
/*  Knockout stage                                                    */
/* ================================================================== */

/* Largest power of 2 <= n */
static int power_of_two_floor(int n)
{
  int p = 1;
  while (p * 2 <= n)
    p *= 2;
  return p;
}

static const char *ko_round_name(int round, int total_rounds)
{
  int remaining = total_rounds - round;
  if (remaining == 1)
    return "Final";
  if (remaining == 2)
    return "Semi-final";
  if (remaining == 3)
    return "Quarter-final";
  if (remaining == 4)
    return "Round of 16";
  return "Knockout";
}

static void run_knockout(void)
{
  int qualifiers, r, m;
  int losers_semifinal[2] = {-1, -1};

  if (agent_count < 2)
    return;

  qualifiers = power_of_two_floor(agent_count);
  if (qualifiers < 2)
    qualifiers = 2;
  if (qualifiers > standing_count)
    qualifiers = standing_count;

  printf("\n=== KNOCKOUT STAGE (%d qualifiers) ===\n", qualifiers);

  /* Seed from standings (already sorted) */
  ko_rounds = 0;

  /* First round */
  ko_round_size[0] = qualifiers / 2;
  for (m = 0; m < ko_round_size[0]; ++m)
  {
    int seed_hi = standings[m].idx;                  /* top seed */
    int seed_lo = standings[qualifiers - 1 - m].idx; /* bottom seed */
    ko_bracket[0][m].seed1 = seed_hi;
    ko_bracket[0][m].seed2 = seed_lo;
  }
  ko_rounds = 1;

  /* Play rounds until we have a winner */
  for (r = 0; ko_round_size[r] >= 1; ++r)
  {
    int matches = ko_round_size[r];
    const char *rname = ko_round_name(r, ko_rounds + (matches > 1 ? 1 : 0));
    char label[80];

    /* Recalculate round name now that we know total */
    {
      /* Count total rounds */
      int tmp = matches;
      int tr = r + 1;
      while (tmp > 1)
      {
        tmp /= 2;
        tr++;
      }
      rname = ko_round_name(r, tr);
    }

    printf("\n--- %s ---\n", rname);

    for (m = 0; m < matches; ++m)
    {
      KnockoutMatch *km = &ko_bracket[r][m];
      GameRecord *g1, *g2;
      int gid1, gid2;

      snprintf(label, sizeof(label), "%s - Match %d (Leg 1)", rname, m + 1);
      printf("  %s vs %s (Leg 1)...", agent_name(km->seed1), agent_name(km->seed2));
      fflush(stdout);
      gid1 = play_game(km->seed1, km->seed2, label);
      g1 = &all_games[gid1];
      printf(" %d-%d\n", g1->p1_score, g1->p2_score);

      snprintf(label, sizeof(label), "%s - Match %d (Leg 2)", rname, m + 1);
      printf("  %s vs %s (Leg 2)...", agent_name(km->seed2), agent_name(km->seed1));
      fflush(stdout);
      gid2 = play_game(km->seed2, km->seed1, label);
      g2 = &all_games[gid2];
      printf(" %d-%d\n", g2->p1_score, g2->p2_score);

      km->game_id_leg1 = gid1;
      km->game_id_leg2 = gid2;
      /* Aggregate: seed1's total pieces vs seed2's total pieces */
      km->agg1 = g1->p1_score + g2->p2_score;
      km->agg2 = g1->p2_score + g2->p1_score;

      if (km->agg1 > km->agg2)
        km->winner = km->seed1;
      else if (km->agg2 > km->agg1)
        km->winner = km->seed2;
      else
        km->winner = km->seed1; /* higher seed advances on tie */

      printf("  -> Aggregate: %s %d - %d %s => Winner: %s\n",
             agent_name(km->seed1), km->agg1,
             km->agg2, agent_name(km->seed2),
             agent_name(km->winner));
    }

    /* Prepare next round */
    if (matches >= 2)
    {
      int next = matches / 2;
      ko_round_size[r + 1] = next;
      ko_rounds = r + 2;
      for (m = 0; m < next; ++m)
      {
        ko_bracket[r + 1][m].seed1 = ko_bracket[r][2 * m].winner;
        ko_bracket[r + 1][m].seed2 = ko_bracket[r][2 * m + 1].winner;
      }

      /* Track semi-final losers for third place match */
      if (next == 1)
      {
        /* This was the semi-final round */
        for (m = 0; m < matches; ++m)
        {
          KnockoutMatch *km = &ko_bracket[r][m];
          losers_semifinal[m] = (km->winner == km->seed1) ? km->seed2 : km->seed1;
        }
      }
    }
    else
    {
      break; /* final has been played */
    }
  }

  /* Third-place match */
  if (losers_semifinal[0] >= 0 && losers_semifinal[1] >= 0)
  {
    GameRecord *g1, *g2;
    int gid1, gid2;
    char label[80];
    has_third_place = 1;
    third_place_match.seed1 = losers_semifinal[0];
    third_place_match.seed2 = losers_semifinal[1];

    printf("\n--- Third-place match ---\n");
    snprintf(label, sizeof(label), "Third-place Match (Leg 1)");
    printf("  %s vs %s (Leg 1)...", agent_name(losers_semifinal[0]), agent_name(losers_semifinal[1]));
    fflush(stdout);
    gid1 = play_game(losers_semifinal[0], losers_semifinal[1], label);
    g1 = &all_games[gid1];
    printf(" %d-%d\n", g1->p1_score, g1->p2_score);

    snprintf(label, sizeof(label), "Third-place Match (Leg 2)");
    printf("  %s vs %s (Leg 2)...", agent_name(losers_semifinal[1]), agent_name(losers_semifinal[0]));
    fflush(stdout);
    gid2 = play_game(losers_semifinal[1], losers_semifinal[0], label);
    g2 = &all_games[gid2];
    printf(" %d-%d\n", g2->p1_score, g2->p2_score);

    third_place_match.game_id_leg1 = gid1;
    third_place_match.game_id_leg2 = gid2;
    third_place_match.agg1 = g1->p1_score + g2->p2_score;
    third_place_match.agg2 = g1->p2_score + g2->p1_score;
    if (third_place_match.agg1 >= third_place_match.agg2)
      third_place_match.winner = third_place_match.seed1;
    else
      third_place_match.winner = third_place_match.seed2;
    printf("  -> 3rd place: %s\n", agent_name(third_place_match.winner));
  }
}

/* ================================================================== */
/*  HTML report generation                                            */
/* ================================================================== */

/* ---------- CSS ---------- */
static void html_write_css(FILE *f)
{
  /* Split across multiple fputs calls to stay within C99 string length limits */
  fputs("<style>\n"
        "  :root {\n"
        "    --bg: #0f1117; --surface: #1a1d27; --surface2: #242836;\n"
        "    --border: #2e3348; --text: #e1e4ed; --text2: #8b90a5;\n"
        "    --accent: #6c5ce7; --accent2: #a29bfe;\n"
        "    --p1: #3498db; --p1bg: rgba(52,152,219,.15);\n"
        "    --p2: #e74c3c; --p2bg: rgba(231,76,60,.15);\n"
        "    --win: #2ecc71; --draw: #f39c12; --loss: #e74c3c;\n"
        "    --gold: #f1c40f; --silver: #bdc3c7; --bronze: #cd7f32;\n"
        "  }\n"
        "  * { box-sizing: border-box; margin: 0; padding: 0; }\n"
        "  body {\n"
        "    font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;\n"
        "    background: var(--bg); color: var(--text);\n"
        "    max-width: 1200px; margin: 0 auto; padding: 20px;\n"
        "  }\n"
        "  h1 { text-align: center; font-size: 2.2em; margin: 30px 0 10px;\n"
        "       background: linear-gradient(135deg, var(--accent), var(--accent2));\n"
        "       -webkit-background-clip: text; -webkit-text-fill-color: transparent;\n"
        "       background-clip: text; }\n"
        "  .subtitle { text-align: center; color: var(--text2); margin-bottom: 30px; }\n"
        "  h2 { color: var(--accent2); border-bottom: 2px solid var(--border);\n"
        "       padding-bottom: 8px; margin: 40px 0 20px; }\n"
        "  h3 { color: var(--text); margin: 20px 0 12px; }\n"
        "  .podium { display: flex; justify-content: center; align-items: flex-end;\n"
        "            gap: 20px; margin: 30px 0 40px; }\n"
        "  .podium-place { text-align: center; border-radius: 12px;\n"
        "                  padding: 20px 30px; background: var(--surface); }\n"
        "  .podium-place.first  { order: 2; min-height: 200px;\n"
        "    border: 2px solid var(--gold); box-shadow: 0 0 20px rgba(241,196,15,.2); }\n"
        "  .podium-place.second { order: 1; min-height: 160px;\n"
        "    border: 2px solid var(--silver); }\n"
        "  .podium-place.third  { order: 3; min-height: 120px;\n"
        "    border: 2px solid var(--bronze); }\n"
        "  .podium-rank { font-size: 2em; font-weight: 700; }\n"
        "  .podium-name { font-size: 1.2em; margin-top: 8px; }\n"
        "  .podium-pts  { color: var(--text2); font-size: .9em; }\n"
        "  .first  .podium-rank { color: var(--gold); }\n"
        "  .second .podium-rank { color: var(--silver); }\n"
        "  .third  .podium-rank { color: var(--bronze); }\n",
        f);

  fputs("  table { width: 100%; border-collapse: collapse; margin: 15px 0;\n"
        "          background: var(--surface); border-radius: 8px; overflow: hidden; }\n"
        "  th { background: var(--surface2); color: var(--accent2);\n"
        "       padding: 12px 10px; text-align: left; font-weight: 600;\n"
        "       font-size: .85em; text-transform: uppercase; letter-spacing: .5px; }\n"
        "  td { padding: 10px; border-top: 1px solid var(--border); }\n"
        "  tr:hover td { background: rgba(108,92,231,.05); }\n"
        "  .rank-cell { font-weight: 700; color: var(--accent2); width: 40px; }\n"
        "  th[title] { cursor: help; }\n"
        "  .table-legend { font-size: .8em; color: var(--text2); margin: -8px 0 20px; display: flex; flex-wrap: wrap; gap: 12px; }\n"
        "  .table-legend span { white-space: nowrap; }\n"
        "  .table-legend abbr { border-bottom: 1px dotted var(--text2); cursor: help; font-style: normal; }\n"
        "  .w { color: var(--win); font-weight: 600; }\n"
        "  .d { color: var(--draw); font-weight: 600; }\n"
        "  .l { color: var(--loss); font-weight: 600; }\n"
        "  .bracket-round { margin: 15px 0; }\n"
        "  .bracket-match {\n"
        "    background: var(--surface); border: 1px solid var(--border);\n"
        "    border-radius: 8px; padding: 12px 16px; margin: 8px 0;\n"
        "    display: inline-block; min-width: 320px; vertical-align: top;\n"
        "    margin-right: 16px;\n"
        "  }\n"
        "  .bracket-match .vs { color: var(--text2); font-size: .85em; }\n"
        "  .bracket-match .winner-line { color: var(--win); font-weight: 700; }\n"
        "  .bracket-match .loser-line { color: var(--text2); }\n"
        "  details {\n"
        "    margin: 6px 0; border: 1px solid var(--border); border-radius: 8px;\n"
        "    background: var(--surface); overflow: hidden;\n"
        "  }\n"
        "  summary {\n"
        "    padding: 12px 16px; cursor: pointer; font-weight: 600;\n"
        "    background: var(--surface2); user-select: none;\n"
        "    list-style: none; display: flex; justify-content: space-between;\n"
        "    align-items: center;\n"
        "  }\n"
        "  summary::-webkit-details-marker { display: none; }\n"
        "  summary::after { content: '\\25B6'; transition: transform .2s;\n"
        "                   color: var(--accent2); font-size: .8em; }\n"
        "  details[open] > summary::after { transform: rotate(90deg); }\n"
        "  details[open] > summary { border-bottom: 1px solid var(--border); }\n",
        f);

  fputs("  .game-detail { padding: 16px; }\n"
        "  .moves-grid { display: flex; flex-wrap: wrap; gap: 12px;\n"
        "                justify-content: flex-start; }\n"
        "  .move-card {\n"
        "    background: var(--surface2); border-radius: 6px; padding: 8px;\n"
        "    text-align: center; font-size: .8em; border: 1px solid var(--border);\n"
        "  }\n"
        "  .move-card .move-num { color: var(--accent2); font-weight: 700;\n"
        "                         margin-bottom: 4px; }\n"
        "  .move-card .move-desc { color: var(--text2); margin-bottom: 6px;\n"
        "                          font-size: .85em; }\n"
        "  .board-grid { display: inline-grid; gap: 2px; padding: 2px;\n"
        "               background: var(--border); border-radius: 4px; }\n"
        "  .bcell {\n"
        "    width: 22px; height: 22px; border-radius: 3px;\n"
        "    display: flex; align-items: center; justify-content: center;\n"
        "    font-size: 12px; font-weight: 700;\n"
        "  }\n"
        "  .bcell-e { background: var(--surface); }\n"
        "  .bcell-1 { background: var(--p1); color: #fff; }\n"
        "  .bcell-2 { background: var(--p2); color: #fff; }\n"
        "  .pill {\n"
        "    display: inline-block; padding: 2px 8px; border-radius: 10px;\n"
        "    font-size: .8em; font-weight: 600;\n"
        "  }\n"
        "  .pill-w { background: rgba(46,204,113,.15); color: var(--win); }\n"
        "  .pill-d { background: rgba(243,156,18,.15); color: var(--draw); }\n"
        "  .pill-l { background: rgba(231,76,60,.15);  color: var(--loss); }\n"
        "  .score-big { font-size: 1.3em; font-weight: 700; }\n"
        "  .game-score-header {\n"
        "    display: flex; justify-content: center; align-items: center;\n"
        "    gap: 20px; margin: 10px 0;\n"
        "  }\n"
        "  .game-score-header .name-p1 { color: var(--p1); font-weight: 600;\n"
        "                                 text-align: right; flex: 1; }\n"
        "  .game-score-header .name-p2 { color: var(--p2); font-weight: 600;\n"
        "                                 text-align: left; flex: 1; }\n"
        "</style>\n",
        f);
}

/* ---------- Board as HTML ---------- */
static void html_write_board(FILE *f, const GameState *st)
{
  int r, c;
  fprintf(f, "<div class=\"board-grid\" style=\"grid-template-columns:repeat(%d,22px)\">\n",
          st->board_size);
  for (r = 0; r < st->board_size; ++r)
  {
    for (c = 0; c < st->board_size; ++c)
    {
      if (st->board[r][c] == PLAYER_ONE)
        fprintf(f, "  <div class=\"bcell bcell-1\">X</div>\n");
      else if (st->board[r][c] == PLAYER_TWO)
        fprintf(f, "  <div class=\"bcell bcell-2\">O</div>\n");
      else
        fprintf(f, "  <div class=\"bcell bcell-e\"></div>\n");
    }
  }
  fprintf(f, "</div>\n");
}

/* ---------- Move description ---------- */
static void format_move(const Move *m, char *buf, size_t bsz)
{
  if (m->is_pass)
  {
    snprintf(buf, bsz, "Pass");
    return;
  }
  /* Use algebraic notation: col=A-I, row=1-9 */
  snprintf(buf, bsz, "%c%d %s %c%d",
           'A' + m->from_col, m->from_row + 1,
           (abs(m->to_row - m->from_row) <= 1 &&
            abs(m->to_col - m->from_col) <= 1)
               ? "clone&rarr;"
               : "jump&rarr;",
           'A' + m->to_col, m->to_row + 1);
}

/* ---------- Podium ---------- */
static void html_write_podium(FILE *f)
{
  int final_round = ko_rounds - 1;
  int champion = -1, runner_up = -1, third = -1;

  if (ko_rounds > 0 && ko_round_size[final_round] == 1)
  {
    KnockoutMatch *fin = &ko_bracket[final_round][0];
    champion = fin->winner;
    runner_up = (fin->winner == fin->seed1) ? fin->seed2 : fin->seed1;
  }
  if (has_third_place)
    third = third_place_match.winner;

  /* Fallback if no knockout (only 1 agent) */
  if (champion < 0 && standing_count > 0)
    champion = standings[0].idx;
  if (runner_up < 0 && standing_count > 1)
    runner_up = standings[1].idx;
  if (third < 0 && standing_count > 2)
    third = standings[2].idx;

  fprintf(f, "<div class=\"podium\">\n");
  if (runner_up >= 0)
    fprintf(f, "  <div class=\"podium-place second\"><div class=\"podium-rank\">2</div>"
               "<div class=\"podium-name\">%s</div><div class=\"podium-pts\">Runner-up</div></div>\n",
            agent_name(runner_up));
  if (champion >= 0)
    fprintf(f, "  <div class=\"podium-place first\"><div class=\"podium-rank\">&#127942;</div>"
               "<div class=\"podium-name\">%s</div><div class=\"podium-pts\">Champion</div></div>\n",
            agent_name(champion));
  if (third >= 0)
    fprintf(f, "  <div class=\"podium-place third\"><div class=\"podium-rank\">3</div>"
               "<div class=\"podium-name\">%s</div><div class=\"podium-pts\">Third place</div></div>\n",
            agent_name(third));
  fprintf(f, "</div>\n");
}

/* ---------- Group standings table ---------- */
static void html_write_standings(FILE *f)
{
  int i;
  fprintf(f, "<h2>&#9917; Group Stage Standings</h2>\n");
  fprintf(f, "<table>\n<tr>"
             "<th title=\"Rank\">#</th>"
             "<th title=\"Agent name\">Agent</th>"
             "<th title=\"Matches played\">P</th>"
             "<th title=\"Wins\">W</th>"
             "<th title=\"Draws\">D</th>"
             "<th title=\"Losses\">L</th>"
             "<th title=\"Goals / pieces scored (score for)\">GF</th>"
             "<th title=\"Goals / pieces conceded (score against)\">GA</th>"
             "<th title=\"Goal difference (GF - GA)\">GD</th>"
             "<th title=\"Points (Win=3, Draw=1, Loss=0)\">Pts</th>"
             "</tr>\n");
  for (i = 0; i < standing_count; ++i)
  {
    Standing *s = &standings[i];
    int gd = s->score_for - s->score_against;
    fprintf(f, "<tr><td class=\"rank-cell\">%d</td><td><strong>%s</strong></td>"
               "<td>%d</td><td class=\"w\">%d</td><td class=\"d\">%d</td>"
               "<td class=\"l\">%d</td><td>%d</td><td>%d</td>"
               "<td>%s%d</td><td><strong>%d</strong></td></tr>\n",
            i + 1, agent_name(s->idx), s->played,
            s->wins, s->draws, s->losses,
            s->score_for, s->score_against,
            gd >= 0 ? "+" : "", gd, s->points);
  }
  fprintf(f, "</table>\n");
  fputs("<p class=\"table-legend\">\n"
        "  <span><abbr title=\"Matches played\">P</abbr> = Played</span>\n"
        "  <span><abbr title=\"Wins\">W</abbr> = Won</span>\n"
        "  <span><abbr title=\"Draws\">D</abbr> = Drawn</span>\n"
        "  <span><abbr title=\"Losses\">L</abbr> = Lost</span>\n"
        "  <span><abbr title=\"Goals / pieces scored\">GF</abbr> = Goals For</span>\n"
        "  <span><abbr title=\"Goals / pieces conceded\">GA</abbr> = Goals Against</span>\n"
        "  <span><abbr title=\"Goal difference (GF &minus; GA)\">GD</abbr> = Goal Diff</span>\n"
        "  <span><abbr title=\"Points (Win=3, Draw=1, Loss=0)\">Pts</abbr> = Points</span>\n"
        "</p>\n",
        f);
}

/* ---------- Knockout bracket ---------- */
static void html_write_bracket(FILE *f)
{
  int r, m;
  if (ko_rounds == 0)
    return;
  fprintf(f, "<h2>&#127942; Knockout Stage</h2>\n");

  for (r = 0; r < ko_rounds; ++r)
  {
    const char *rname;
    {
      int rem = ko_rounds - r;
      if (rem == 1)
        rname = "Final";
      else if (rem == 2)
        rname = "Semi-finals";
      else if (rem == 3)
        rname = "Quarter-finals";
      else
        rname = "Knockout Round";
    }
    fprintf(f, "<h3>%s</h3>\n<div class=\"bracket-round\">\n", rname);

    for (m = 0; m < ko_round_size[r]; ++m)
    {
      KnockoutMatch *km = &ko_bracket[r][m];
      const char *c1 = (km->winner == km->seed1) ? "winner-line" : "loser-line";
      const char *c2 = (km->winner == km->seed2) ? "winner-line" : "loser-line";
      fprintf(f,
              "<div class=\"bracket-match\">"
              "<div class=\"%s\">%s &mdash; %d</div>"
              "<div class=\"vs\">vs (aggregate)</div>"
              "<div class=\"%s\">%s &mdash; %d</div>"
              "</div>\n",
              c1, agent_name(km->seed1), km->agg1,
              c2, agent_name(km->seed2), km->agg2);
    }
    fprintf(f, "</div>\n");
  }

  if (has_third_place)
  {
    KnockoutMatch *tp = &third_place_match;
    const char *c1 = (tp->winner == tp->seed1) ? "winner-line" : "loser-line";
    const char *c2 = (tp->winner == tp->seed2) ? "winner-line" : "loser-line";
    fprintf(f, "<h3>Third-place Match</h3>\n<div class=\"bracket-round\">\n"
               "<div class=\"bracket-match\">"
               "<div class=\"%s\">%s &mdash; %d</div>"
               "<div class=\"vs\">vs (aggregate)</div>"
               "<div class=\"%s\">%s &mdash; %d</div>"
               "</div>\n</div>\n",
            c1, agent_name(tp->seed1), tp->agg1,
            c2, agent_name(tp->seed2), tp->agg2);
  }
}

/* ---------- All game details (accordion) ---------- */
static void html_write_game_details(FILE *f)
{
  int g, m;
  fprintf(f, "<h2>&#128214; Detailed Game Results</h2>\n");
  fprintf(f, "<p style=\"color:var(--text2)\">Click on a match to expand and view the move-by-move replay.</p>\n");

  for (g = 0; g < game_count; ++g)
  {
    GameRecord *gr = &all_games[g];
    const char *p1n = agent_name(gr->p1_idx);
    const char *p2n = agent_name(gr->p2_idx);
    const char *result_class;
    const char *result_text;

    if (gr->p1_score > gr->p2_score)
    {
      result_class = "pill-w";
      result_text = "P1 wins";
    }
    else if (gr->p1_score < gr->p2_score)
    {
      result_class = "pill-l";
      result_text = "P2 wins";
    }
    else
    {
      result_class = "pill-d";
      result_text = "Draw";
    }

    fprintf(f, "<details>\n<summary>\n"
               "<span>%s &mdash; "
               "<span style=\"color:var(--p1)\">%s</span> "
               "<span class=\"score-big\">%d &ndash; %d</span> "
               "<span style=\"color:var(--p2)\">%s</span>"
               "</span>\n"
               "<span class=\"pill %s\">%s</span>\n"
               "</summary>\n",
            gr->phase_label,
            p1n, gr->p1_score, gr->p2_score, p2n,
            result_class, result_text);

    fprintf(f, "<div class=\"game-detail\">\n");

    /* Score header */
    fprintf(f, "<div class=\"game-score-header\">\n"
               "  <div class=\"name-p1\">%s (X)</div>\n"
               "  <div class=\"score-big\">%d &ndash; %d</div>\n"
               "  <div class=\"name-p2\">%s (O)</div>\n"
               "</div>\n",
            p1n, gr->p1_score, gr->p2_score, p2n);

    /* Moves grid */
    fprintf(f, "<div class=\"moves-grid\">\n");
    for (m = 0; m < gr->move_count; ++m)
    {
      char desc[64];
      MoveRecord *mr = &gr->moves[m];
      Player who = (m % 2 == 0) ? PLAYER_ONE : PLAYER_TWO;
      /* Determine who played: the state_after has already switched,
         so the player who moved is the opponent of current_player */
      if (m > 0)
        who = player_opponent(mr->state_after.current_player);
      else if (mr->state_after.current_player == PLAYER_TWO)
        who = PLAYER_ONE;
      else
        who = PLAYER_TWO;

      format_move(&mr->move, desc, sizeof(desc));

      fprintf(f, "<div class=\"move-card\">\n"
                 "  <div class=\"move-num\" style=\"color:var(--%s)\">Turn %d (%s)</div>\n"
                 "  <div class=\"move-desc\">%s</div>\n",
              who == PLAYER_ONE ? "p1" : "p2",
              m + 1,
              who == PLAYER_ONE ? "X" : "O",
              desc);
      html_write_board(f, &mr->state_after);
      fprintf(f, "</div>\n");
    }
    fprintf(f, "</div>\n"); /* moves-grid */
    fprintf(f, "</div>\n"); /* game-detail */
    fprintf(f, "</details>\n");
  }
}

/* ---------- Full report ---------- */
static void generate_html_report(void)
{
  FILE *f = fopen(cfg_output, "w");
  if (!f)
  {
    fprintf(stderr, "Cannot open %s for writing\n", cfg_output);
    return;
  }

  fprintf(f, "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
             "<meta charset=\"UTF-8\">\n"
             "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
             "<title>Ataxx Tournament Report</title>\n");
  html_write_css(f);
  fprintf(f, "</head>\n<body>\n");

  fprintf(f, "<h1>Ataxx World Cup</h1>\n");
  fprintf(f, "<p class=\"subtitle\">%d agents &bull; %dx%d board &bull; "
             "%d-move limit &bull; depth %d</p>\n",
          agent_count, cfg_board_size, cfg_board_size,
          cfg_move_limit, cfg_depth);

  html_write_podium(f);
  html_write_standings(f);
  html_write_bracket(f);
  html_write_game_details(f);

  fprintf(f, "\n<p style=\"text-align:center;color:var(--text2);margin:40px 0;\">"
             "Generated by ataxx_tournament</p>\n");
  fprintf(f, "</body>\n</html>\n");
  fclose(f);
  printf("\nReport written to %s\n", cfg_output);
}

/* ================================================================== */
/*  Main                                                              */
/* ================================================================== */

static void print_usage(const char *prog)
{
  printf("Usage: %s [options]\n", prog);
  printf("Options:\n");
  printf("  --size <N>    Board size %d-%d (default: %d)\n",
         3, ATAXX_MAX_BOARD_SIZE, ATAXX_BOARD_SIZE);
  printf("  --limit <N>   Move limit per game (default: %d)\n", DEFAULT_LIMIT);
  printf("  --depth <N>   Agent search depth  (default: %d)\n", DEFAULT_DEPTH);
  printf("  --output <F>  Output HTML file    (default: %s)\n", DEFAULT_OUTPUT);
  printf("\nAll .dll/.so plugins in %s/ are auto-discovered.\n", PLUGINS_DIR);
  printf("The built-in random agent is always included.\n");
}

int main(int argc, char *argv[])
{
  int i, g;

  /* Parse arguments */
  for (i = 1; i < argc; ++i)
  {
    if ((strcmp(argv[i], "--help") == 0) || (strcmp(argv[i], "-h") == 0))
    {
      print_usage(argv[0]);
      return 0;
    }
    if (strcmp(argv[i], "--size") == 0 && i + 1 < argc)
      cfg_board_size = atoi(argv[++i]);
    else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc)
      cfg_move_limit = atoi(argv[++i]);
    else if (strcmp(argv[i], "--depth") == 0 && i + 1 < argc)
      cfg_depth = atoi(argv[++i]);
    else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc)
      snprintf(cfg_output, sizeof(cfg_output), "%s", argv[++i]);
    else
    {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      print_usage(argv[0]);
      return 1;
    }
  }

  printf("Ataxx Tournament Engine\n");
  printf("Board: %dx%d | Move limit: %d | Depth: %d\n",
         cfg_board_size, cfg_board_size, cfg_move_limit, cfg_depth);
  printf("Output: %s\n\n", cfg_output);

  /* Discover agents */
  printf("Discovering agents in %s/...\n", PLUGINS_DIR);
  discover_agents();
  printf("Found %d agent(s)\n", agent_count);

  if (agent_count < 2)
  {
    printf("Need at least 2 agents to run a tournament.\n");
    printf("Build some plugins with: make student_plugin SRC=... NAME=...\n");
    /* Clean up */
    for (i = 0; i < agent_count; ++i)
      if (agents[i].kind == AGENT_KIND_PLUGIN)
        agent_plugin_unload(&agents[i].plugin);
    return 1;
  }

  /* Seed random for the random agent */
  srand((unsigned)time(NULL));

  /* Run tournament */
  run_group_stage();
  sort_standings();
  run_knockout();

  /* Generate report */
  generate_html_report();

  /* Cleanup */
  for (g = 0; g < game_count; ++g)
    free(all_games[g].moves);
  for (i = 0; i < agent_count; ++i)
    if (agents[i].kind == AGENT_KIND_PLUGIN)
      agent_plugin_unload(&agents[i].plugin);

  return 0;
}
