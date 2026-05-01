#ifndef GAME_H
#define GAME_H

#include "common.h"

typedef struct Move
{
    int from_row;
    int from_col;
    int to_row;
    int to_col;
    bool is_pass;
} Move;

typedef struct GameState
{
    Player board[ATAXX_MAX_BOARD_SIZE][ATAXX_MAX_BOARD_SIZE];
    int board_size;
    Player current_player;
    int turn_count;
} GameState;

void game_init(GameState *state, int board_size);
void game_print(const GameState *state);
int game_generate_moves(const GameState *state, Move moves[], int max_moves);
bool game_apply_move(GameState *state, Move move);
bool game_is_terminal(const GameState *state);
int game_score(const GameState *state, Player player);
uint64_t game_hash(const GameState *state);

#endif