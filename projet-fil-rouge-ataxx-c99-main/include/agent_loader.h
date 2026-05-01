#ifndef AGENT_LOADER_H
#define AGENT_LOADER_H

#include "game.h"
#include "agent.h"

/* ── Function pointer type matching the agent plugin interface ───── */

typedef Move (*AgentChooseMoveFunc)(const GameState *state,
                                    AgentContext *context);

/* ── Plugin handle ────────────────────────────────────────────────── */

typedef struct AgentPlugin
{
  void *handle;                    /* dlopen / LoadLibrary handle  */
  AgentChooseMoveFunc choose_move; /* resolved function pointer    */
  char name[64];                   /* display name (from filename) */
} AgentPlugin;

/*
 * Load a shared library from `path` and resolve the symbol
 *   Move agent_choose_move(const GameState *, AgentContext *)
 *
 * On success: fills *plugin and returns true.
 * On failure: prints a diagnostic to stderr and returns false.
 */
bool agent_plugin_load(AgentPlugin *plugin, const char *path);

/*
 * Return a human-readable description of the most recent plugin loader
 * failure. Returns an empty string if no failure has been recorded.
 */
const char *agent_plugin_last_error(void);

/*
 * Unload a previously loaded plugin. Safe to call on a zeroed struct.
 */
void agent_plugin_unload(AgentPlugin *plugin);

#endif /* AGENT_LOADER_H */
