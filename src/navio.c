#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include "config.h"
#include "ship.h"

volatile int shutdown_flag = 0;

void signal_handler(int sig) {
    (void)sig;
    shutdown_flag = 1;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Uso: %s <id> <tipo> <linha>\n", argv[0]);
        return 1;
    }
    
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    
    int ship_id = atoi(argv[1]);
    int ship_type = atoi(argv[2]);
    int ship_row = atoi(argv[3]);
    
    printf("[NAVIO %d] Iniciado - Tipo: %d, Linha: %d\n", ship_id, ship_type, ship_row);
    
    // Abrir FIFO para escrita
    int fifo_fd = open(FIFO_PATH, O_WRONLY);
    if (fifo_fd == -1) {
        perror("open FIFO");
        return 1;
    }
    
    // Posição inicial
    int current_col = 0;
    int direction = 1; // 1 = direita, -1 = esquerda

    // Enviar posição inicial
    ShipEvent event;
    event.ship_id = ship_id;
    event.ship_type = ship_type;
    event.row = ship_row;
    event.col = current_col;
    event.timestamp = time(NULL);

    if (write(fifo_fd, &event, sizeof(ShipEvent)) != sizeof(ShipEvent)) {
        perror("write FIFO");
    } else {
        printf("[NAVIO %d] Posicionado em (%d,%d)\n", ship_id, ship_row, current_col);
    }

    // Loop de movimentação: permanece pelo menos 5s por coluna
    while (!shutdown_flag) {
        sleep(MIN_TIME_PER_COLUMN);
        
        if (shutdown_flag) break;
        
        // Mover para coluna contígua
        int next_col = current_col + direction;
        
        // Inverter direção nas bordas
        if (next_col >= BOARD_COLS) {
            direction = -1;
            next_col = current_col - 1;
        } else if (next_col < 0) {
            direction = 1;
            next_col = current_col + 1;
        }
        
        current_col = next_col;
        
        // Enviar nova posição via FIFO
        event.ship_id = ship_id;
        event.ship_type = ship_type;
        event.row = ship_row;
        event.col = current_col;
        event.timestamp = time(NULL);
        
        if (write(fifo_fd, &event, sizeof(ShipEvent)) != sizeof(ShipEvent)) {
            perror("write FIFO");
            // Tentar reabrir FIFO
            close(fifo_fd);
            fifo_fd = open(FIFO_PATH, O_WRONLY);
            if (fifo_fd == -1) {
                perror("reopen FIFO");
                break;
            }
        } else {
            printf("[NAVIO %d] Moveu para (%d,%d)\n", ship_id, ship_row, current_col);
        }
    }
    
    close(fifo_fd);
    printf("[NAVIO %d] Encerrado\n", ship_id);
    
    return 0;
}
