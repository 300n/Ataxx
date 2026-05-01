#include "agent.h"

Move agent_random_choose_move(const GameState *state)
{
  Move moves[ATAXX_MAX_MOVES];
  int count = game_generate_moves(state, moves, ATAXX_MAX_MOVES);
  if (count <= 0)
  {
    Move fallback = {0, 0, 0, 0, true};
    return fallback;
  }
  return moves[0];
}

Move agent_choose_move(const GameState *state, AgentContext *context)
{
  (void)context;
  return agent_random_choose_move(state);
}