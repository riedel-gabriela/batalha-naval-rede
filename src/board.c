#include "board.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

Board* board_init() {
    Board* board = (Board*)malloc(sizeof(Board));
    if (!board) return NULL;
    
    board->alive_ships = TOTAL_NAVIOS;
    board->attack_count = 0;
    board->score_against = 0;
    
    // Inicializar tabuleiro
    for (int i = 0; i < BOARD_ROWS; i++) {
        for (int j = 0; j < BOARD_COLS; j++) {
            board->board_state[i][j] = EMPTY;
        }
    }
    
    // Inicializar array de navios
    memset(board->ships, 0, sizeof(board->ships));
    
    return board;
}

void board_add_ship(Board* board, Ship ship) {
    for (int i = 0; i < TOTAL_NAVIOS; i++) {
        if (board->ships[i].id == 0) {
            board->ships[i] = ship;
            board->board_state[ship.row][ship.col] = SHIP;
            return;
        }
    }
}

int board_get_ship_at(Board* board, int row, int col) {
    for (int i = 0; i < TOTAL_NAVIOS; i++) {
        if (board->ships[i].alive && 
            board->ships[i].row == row && 
            board->ships[i].col == col) {
            return i;
        }
    }
    return -1;
}

int board_is_hit(Board* board, int row, int col) {
    for (int i = 0; i < board->attack_count; i++) {
        if (board->attacks[i].row == row && board->attacks[i].col == col) {
            return 1;
        }
    }
    return 0;
}

int board_shoot(Board* board, int row, int col, const char* attacker_ip) {
    if (row < 0 || row >= BOARD_ROWS || col < 0 || col >= BOARD_COLS) {
        return -1;  // Fora do tabuleiro
    }
    
    // Verificar se já foi atacado
    if (board_is_hit(board, row, col)) {
        AttackRecord* attack = &board->attacks[board->attack_count++];
        attack->row = row;
        attack->col = col;
        attack->timestamp = (double)time(NULL);
        attack->result = 2;  // repetido
        strcpy(attack->attacker_ip, attacker_ip);
        return 2;  // repetido
    }
    
    // Verificar se há navio
    int ship_idx = board_get_ship_at(board, row, col);
    
    AttackRecord* attack = &board->attacks[board->attack_count++];
    attack->row = row;
    attack->col = col;
    attack->timestamp = (double)time(NULL);
    strcpy(attack->attacker_ip, attacker_ip);
    
    if (ship_idx != -1) {
        // Acerto!
        attack->result = 1;
        attack->ship_type = board->ships[ship_idx].type;
        board->score_against += board->ships[ship_idx].type;
        
        // Destruir navio
        board->ships[ship_idx].alive = 0;
        board->alive_ships--;
        
        return board->ships[ship_idx].type;  // Retorna tipo do navio
    } else {
        // Água
        attack->result = 0;
        return 0;  // água
    }
}

void board_destroy_ship(Board* board, int ship_id) {
    for (int i = 0; i < TOTAL_NAVIOS; i++) {
        if (board->ships[i].id == ship_id && board->ships[i].alive) {
            board->ships[i].alive = 0;
            board->alive_ships--;
            return;
        }
    }
}

void board_free(Board* board) {
    if (board) free(board);
}
