#include "game.h"

#include <stdio.h>
#include <stdlib.h>

static bool in_bounds(int row, int col, int size)
{
    return row >= 0 && row < size && col >= 0 && col < size;
}

static int chebyshev_distance(int a_row, int a_col, int b_row, int b_col)
{
    int d_row = abs(a_row - b_row);
    int d_col = abs(a_col - b_col);
    return d_row > d_col ? d_row : d_col;
}

void game_init(GameState *state, int board_size)
{
    int row;
    int col;

    if (board_size < 3)
        board_size = 3;
    if (board_size > ATAXX_MAX_BOARD_SIZE)
        board_size = ATAXX_MAX_BOARD_SIZE;
    state->board_size = board_size;

    for (row = 0; row < ATAXX_MAX_BOARD_SIZE; ++row)
    {
        for (col = 0; col < ATAXX_MAX_BOARD_SIZE; ++col)
        {
            state->board[row][col] = PLAYER_NONE;
        }
    }

    state->board[0][0] = PLAYER_ONE;
    state->board[board_size - 1][board_size - 1] = PLAYER_ONE;
    state->board[0][board_size - 1] = PLAYER_TWO;
    state->board[board_size - 1][0] = PLAYER_TWO;
    state->current_player = PLAYER_ONE;
    state->turn_count = 0;
}

void game_print(const GameState *state)
{
    int row;
    int col;
    for (row = 0; row < state->board_size; ++row)
    {
        for (col = 0; col < state->board_size; ++col)
        {
            char token = '.';
            if (state->board[row][col] == PLAYER_ONE)
            {
                token = 'X';
            }
            else if (state->board[row][col] == PLAYER_TWO)
            {
                token = 'O';
            }
            printf("%c ", token);
        }
        printf("\n");
    }
}

int game_generate_moves(const GameState *state, Move moves[], int max_moves)
{
    int row;
    int col;
    int target_row;
    int target_col;
    int count = 0;

    for (row = 0; row < state->board_size; ++row)
    {
        for (col = 0; col < state->board_size; ++col)
        {
            if (state->board[row][col] != state->current_player)
            {
                continue;
            }
            for (target_row = row - 2; target_row <= row + 2; ++target_row)
            {
                for (target_col = col - 2; target_col <= col + 2; ++target_col)
                {
                    int distance;
                    if (!in_bounds(target_row, target_col, state->board_size))
                    {
                        continue;
                    }
                    if (state->board[target_row][target_col] != PLAYER_NONE)
                    {
                        continue;
                    }
                    distance = chebyshev_distance(row, col, target_row, target_col);
                    if (distance < 1 || distance > 2)
                    {
                        continue;
                    }
                    if (count < max_moves)
                    {
                        moves[count].from_row = row;
                        moves[count].from_col = col;
                        moves[count].to_row = target_row;
                        moves[count].to_col = target_col;
                        moves[count].is_pass = false;
                    }
                    ++count;
                }
            }
        }
    }

    if (count == 0 && max_moves > 0)
    {
        moves[0].from_row = -1;
        moves[0].from_col = -1;
        moves[0].to_row = -1;
        moves[0].to_col = -1;
        moves[0].is_pass = true;
        return 1;
    }

    return count;
}

bool game_apply_move(GameState *state, Move move)
{
    int row;
    int col;
    if (move.is_pass)
    {
        state->current_player = player_opponent(state->current_player);
        ++state->turn_count;
        return true;
    }

    if (!in_bounds(move.from_row, move.from_col, state->board_size) || !in_bounds(move.to_row, move.to_col, state->board_size))
    {
        return false;
    }

    if (state->board[move.from_row][move.from_col] != state->current_player)
    {
        return false;
    }

    if (state->board[move.to_row][move.to_col] != PLAYER_NONE)
    {
        return false;
    }

    if (chebyshev_distance(move.from_row, move.from_col, move.to_row, move.to_col) == 1)
    {
        state->board[move.to_row][move.to_col] = state->current_player;
    }
    else
    {
        state->board[move.to_row][move.to_col] = state->current_player;
        state->board[move.from_row][move.from_col] = PLAYER_NONE;
    }

    for (row = move.to_row - 1; row <= move.to_row + 1; ++row)
    {
        for (col = move.to_col - 1; col <= move.to_col + 1; ++col)
        {
            if (!in_bounds(row, col, state->board_size))
            {
                continue;
            }
            if (state->board[row][col] == player_opponent(state->current_player))
            {
                state->board[row][col] = state->current_player;
            }
        }
    }

    state->current_player = player_opponent(state->current_player);
    ++state->turn_count;
    return true;
}

bool game_is_terminal(const GameState *state)
{
    Move moves[ATAXX_MAX_MOVES];
    GameState copy = *state;
    int moves_current = game_generate_moves(&copy, moves, ATAXX_MAX_MOVES);

    if (moves_current != 1 || !moves[0].is_pass)
    {
        return false;
    }

    copy.current_player = player_opponent(copy.current_player);
    moves_current = game_generate_moves(&copy, moves, ATAXX_MAX_MOVES);
    return moves_current == 1 && moves[0].is_pass;
}

int game_score(const GameState *state, Player player)
{
    int row;
    int col;
    int total = 0;
    for (row = 0; row < state->board_size; ++row)
    {
        for (col = 0; col < state->board_size; ++col)
        {
            if (state->board[row][col] == player)
            {
                ++total;
            }
        }
    }
    return total;
}

uint64_t game_hash(const GameState *state)
{
    int row;
    int col;
    uint64_t hash = 1469598103934665603ull;

    for (row = 0; row < state->board_size; ++row)
    {
        for (col = 0; col < state->board_size; ++col)
        {
            hash ^= (uint64_t)state->board[row][col] + (uint64_t)(row * state->board_size + col + 1);
            hash *= 1099511628211ull;
        }
    }
    hash ^= (uint64_t)state->current_player;
    return hash;
}