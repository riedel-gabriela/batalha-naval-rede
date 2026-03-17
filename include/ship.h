#ifndef SHIP_H
#define SHIP_H

#include "config.h"
#include <time.h>

// Estrutura para representar um navio
typedef struct {
    int id;              // Identificador único
    int type;            // Tipo (5=porta-aviões, 3=submarino, 2=fragata)
    int row;             // Linha fixa
    int col;             // Coluna atual
    int alive;           // 1=vivo, 0=destruído
    time_t last_change;  // Última vez que mudou de coluna
} Ship;

// Estrutura para comunicação via FIFO
typedef struct {
    int ship_id;
    int ship_type;
    int row;
    int col;
    time_t timestamp;
} ShipEvent;

#endif // SHIP_H
