#ifndef CONFIG_H
#define CONFIG_H

// Configurações do tabuleiro
#define BOARD_ROWS 10
#define BOARD_COLS 10

// Tipos de navios
#define PORTA_AVIOES 5
#define SUBMARINO 3
#define FRAGATA 2

// Número de navios
#define NUM_PORTA_AVIOES 1
#define NUM_SUBMARINOS 2
#define NUM_FRAGATAS 3
#define TOTAL_NAVIOS (NUM_PORTA_AVIOES + NUM_SUBMARINOS + NUM_FRAGATAS)

// Tempo em segundos que navio permanece em uma coluna
#define MIN_TIME_PER_COLUMN 5

// Porta do servidor web
#define WEB_SERVER_PORT 8080

// FIFO do servidor
#define FIFO_PATH "/tmp/batalha_naval_fifo"

// Limite de tiros por equipe
#define MAX_SHOTS 100

// Estados de posição
#define EMPTY 0
#define SHIP 1
#define HIT 2
#define WATER 3

#endif // CONFIG_H
