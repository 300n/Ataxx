#include "agent.h"
#include "agent_loader.h"
#include "game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Agent selection ─────────────────────────────────────────────── */

#define AGENT_RANDOM 0
#define AGENT_PLUGIN 1

typedef struct
{
    int kind;           /* AGENT_RANDOM or AGENT_PLUGIN */
    AgentPlugin plugin; /* only used when kind == AGENT_PLUGIN */
} AgentSlot;

static Move agent_slot_get_move(AgentSlot *slot, const GameState *state,
                                AgentContext *ctx)
{
    if (slot->kind == AGENT_RANDOM)
        return agent_random_choose_move(state);
    if (slot->kind == AGENT_PLUGIN)
        return slot->plugin.choose_move(state, ctx);
    /* fallback */
    {
        Move pass = {0, 0, 0, 0, true};
        return pass;
    }
}

static const char *agent_slot_name(const AgentSlot *slot)
{
    if (slot->kind == AGENT_RANDOM)
        return "random";
    if (slot->kind == AGENT_PLUGIN)
        return slot->plugin.name;
    return "???";
}

static bool parse_agent(const char *arg, AgentSlot *slot)
{
    const char *detail;

    if (strcmp(arg, "random") == 0)
    {
        slot->kind = AGENT_RANDOM;
        return true;
    }
    if (strcmp(arg, "student") == 0)
    {
        fprintf(stderr, "La strategie integree 'student' a ete retiree. Construisez votre agent avec 'make student_plugin ...' puis chargez le plugin.\n");
        return false;
    }

    slot->kind = AGENT_PLUGIN;
    if (!strchr(arg, '/') && !strchr(arg, '\\'))
    {
        char path[300];
#ifdef _WIN32
        if (strrchr(arg, '.') != NULL)
            snprintf(path, sizeof(path), "plugins\\%s", arg);
        else
            snprintf(path, sizeof(path), "plugins\\%s.dll", arg);
#else
        if (strrchr(arg, '.') != NULL)
            snprintf(path, sizeof(path), "plugins/%s", arg);
        else
            snprintf(path, sizeof(path), "plugins/%s.so", arg);
#endif
        if (agent_plugin_load(&slot->plugin, path))
            return true;
    }

    if (agent_plugin_load(&slot->plugin, arg))
        return true;

    fprintf(stderr, "Impossible de charger le plugin: %s\n", arg);
    detail = agent_plugin_last_error();
    if (detail[0] != '\0')
        fprintf(stderr, "%s\n", detail);
    return false;
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  --p1 <strategy>   Strategy for player 1 (default: random)\n");
    printf("  --p2 <strategy>   Strategy for player 2 (default: random)\n");
    printf("  --depth <N>       Search depth limit     (default: 3)\n");
    printf("  --limit <N>       Maximum number of turns (default: 100)\n");
    printf("  --size <N>        Board size %d-%d        (default: %d)\n",
           3, ATAXX_MAX_BOARD_SIZE, ATAXX_BOARD_SIZE);
    printf("\nStrategies:\n");
    printf("  random            Built-in random agent\n");
    printf("  <name>            Plugin from plugins/ folder (e.g. agent_foo)\n");
    printf("  <path.dll/.so>    Load a shared-library plugin by path\n");
}

int main(int argc, char *argv[])
{
    GameState state;
    AgentContext context;
    AgentSlot p1, p2;
    int move_limit = 100;
    int board_size = ATAXX_BOARD_SIZE;
    int i;

    /* defaults */
    p1.kind = AGENT_RANDOM;
    p2.kind = AGENT_RANDOM;
    context.depth_limit = 3;

    /* parse arguments */
    for (i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
        {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--p1") == 0 && i + 1 < argc)
        {
            if (!parse_agent(argv[++i], &p1))
                return 1;
        }
        else if (strcmp(argv[i], "--p2") == 0 && i + 1 < argc)
        {
            if (!parse_agent(argv[++i], &p2))
                return 1;
        }
        else if (strcmp(argv[i], "--depth") == 0 && i + 1 < argc)
        {
            context.depth_limit = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc)
        {
            move_limit = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc)
        {
            board_size = atoi(argv[++i]);
        }
        else
        {
            fprintf(stderr, "Option inconnue: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    printf("Player 1 (X): %s\n", agent_slot_name(&p1));
    printf("Player 2 (O): %s\n", agent_slot_name(&p2));
    printf("Depth: %d, Limit: %d, Board: %dx%d\n\n", context.depth_limit, move_limit, board_size, board_size);

    game_init(&state, board_size);

    while (!game_is_terminal(&state) && state.turn_count < move_limit)
    {
        Move move;
        printf("Tour %d - joueur %d\n", state.turn_count + 1, state.current_player);
        game_print(&state);
        printf("\n");

        if (state.current_player == PLAYER_ONE)
        {
            move = agent_slot_get_move(&p1, &state, &context);
        }
        else
        {
            move = agent_slot_get_move(&p2, &state, &context);
        }

        if (!game_apply_move(&state, move))
        {
            fprintf(stderr, "Coup invalide detecte.\n");
            return 1;
        }
    }

    game_print(&state);
    printf("Score final X=%d O=%d\n", game_score(&state, PLAYER_ONE), game_score(&state, PLAYER_TWO));

    /* unload plugins */
    if (p1.kind == AGENT_PLUGIN)
        agent_plugin_unload(&p1.plugin);
    if (p2.kind == AGENT_PLUGIN)
        agent_plugin_unload(&p2.plugin);
    return 0;
}