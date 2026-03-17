#ifndef BOARD_H
#define BOARD_H

#include "config.h"
#include "ship.h"

// Estrutura para rastrear um ataque
typedef struct {
    int row;
    int col;
    double timestamp;
    char attacker_ip[20];
    int result;     // 0=água, 1=acerto, 2=repetido
    int ship_type;  // tipo do navio se acerto
} AttackRecord;

// Estrutura para representar o tabuleiro
typedef struct {
    Ship ships[TOTAL_NAVIOS];
    int alive_ships;
    int board_state[BOARD_ROWS][BOARD_COLS];  // estado de cada posição
    AttackRecord attacks[BOARD_ROWS * BOARD_COLS];  // histórico de ataques
    int attack_count;
    int score_against; // pontos sofridos
} Board;

// Funções
Board* board_init();
void board_add_ship(Board* board, Ship ship);
int board_shoot(Board* board, int row, int col, const char* attacker_ip);
int board_is_hit(Board* board, int row, int col);
void board_destroy_ship(Board* board, int ship_id);
void board_free(Board* board);
int board_get_ship_at(Board* board, int row, int col);

#endif // BOARD_H
