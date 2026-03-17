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
    
    // Movimento do navio
    int current_col = 0;
    int direction = 1;  // 1 = direita, -1 = esquerda
    time_t last_move = time(NULL);
    
    while (!shutdown_flag) {
        // Verificar se deve mover
        time_t now = time(NULL);
        if (now - last_move >= MIN_TIME_PER_COLUMN) {
            // Mover navio
            current_col += direction;
            
            // Verificar limites e reverter direção se necessário
            if (current_col >= BOARD_COLS) {
                current_col = 0;
                direction = 1;
            } else if (current_col < 0) {
                current_col = BOARD_COLS - 1;
                direction = -1;
            }
            
            // Criar evento
            ShipEvent event;
            event.ship_id = ship_id;
            event.ship_type = ship_type;
            event.row = ship_row;
            event.col = current_col;
            event.timestamp = now;
            
            // Enviar via FIFO
            if (write(fifo_fd, &event, sizeof(ShipEvent)) != sizeof(ShipEvent)) {
                perror("write FIFO");
            }
            
            printf("[NAVIO %d] Moveu para coluna %d\n", ship_id, current_col);
            last_move = now;
        }
        
        // Dormir um pouco antes de verificar novamente
        sleep(1);
    }
    
    close(fifo_fd);
    printf("[NAVIO %d] Encerrado\n", ship_id);
    
    return 0;
}
