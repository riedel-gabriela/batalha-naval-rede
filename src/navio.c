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
    
    // Movimento do navio desabilitado: posição fixa
    int current_col = 0;

    // Enviar posição inicial (uma vez)
    ShipEvent event;
    event.ship_id = ship_id;
    event.ship_type = ship_type;
    event.row = ship_row;
    event.col = current_col;
    event.timestamp = time(NULL);

    if (write(fifo_fd, &event, sizeof(ShipEvent)) != sizeof(ShipEvent)) {
        perror("write FIFO");
    } else {
        printf("[NAVIO %d] Posicionado em (%d,%d) e aguardando\n", ship_id, ship_row, current_col);
    }

    while (!shutdown_flag) {
        // Manter navio no lugar sem mover
        sleep(1);
    }
    
    close(fifo_fd);
    printf("[NAVIO %d] Encerrado\n", ship_id);
    
    return 0;
}
