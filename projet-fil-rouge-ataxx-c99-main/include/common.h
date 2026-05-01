#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ATAXX_BOARD_SIZE 5
#define ATAXX_MAX_BOARD_SIZE 9
#define ATAXX_MAX_MOVES 1024
#define ATAXX_INF 1000000

typedef enum Player
{
    PLAYER_NONE = 0,
    PLAYER_ONE = 1,
    PLAYER_TWO = 2
} Player;

static inline Player player_opponent(Player player)
{
    return player == PLAYER_ONE ? PLAYER_TWO : PLAYER_ONE;
}

#endif