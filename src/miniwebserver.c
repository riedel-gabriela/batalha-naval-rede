#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include "config.h"
#include "board.h"
#include "ship.h"

// Variáveis globais
Board* board = NULL;
pid_t ship_pids[TOTAL_NAVIOS];
int fifo_fd = -1;
volatile int shutdown_flag = 0;

// Protótipos
void signal_handler(int sig);
void create_ships();
void handle_http_request(int client_socket, const char* request);
void send_json_response(int client_socket, const char* json);
void read_fifo_events(int fifo_fd);
void destroy_ships();

// Conversão de tipo de navio para string
const char* get_ship_type_name(int type) {
    switch(type) {
        case PORTA_AVIOES: return "porta_avioes";
        case SUBMARINO: return "submarino";
        case FRAGATA: return "fragata";
        default: return "desconhecido";
    }
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Inicializar tabuleiro
    board = board_init();
    if (!board) {
        fprintf(stderr, "Erro ao inicializar tabuleiro\n");
        return 1;
    }
    
    // Remover FIFO antiga se existir
    unlink(FIFO_PATH);
    
    // Criar FIFO
    if (mkfifo(FIFO_PATH, 0666) == -1 && errno != EEXIST) {
        perror("mkfifo");
        return 1;
    }
    
    // Criar e inicializar navios
    int porta_avioes_row = 1;
    int submarino_rows[] = {3, 5};
    int fragata_rows[] = {2, 4, 6};
    
    int ship_count = 0;
    
    // Adicionar porta-aviões
    Ship porta_avioes = {
        .id = 100,
        .type = PORTA_AVIOES,
        .row = porta_avioes_row,
        .col = 0,
        .alive = 1,
        .last_change = time(NULL)
    };
    board_add_ship(board, porta_avioes);
    ship_pids[ship_count++] = -1;
    
    // Adicionar submarinos
    for (int i = 0; i < NUM_SUBMARINOS; i++) {
        Ship submarino = {
            .id = 200 + i,
            .type = SUBMARINO,
            .row = submarino_rows[i],
            .col = 0,
            .alive = 1,
            .last_change = time(NULL)
        };
        board_add_ship(board, submarino);
        ship_pids[ship_count++] = -1;
    }
    
    // Adicionar fragatas
    for (int i = 0; i < NUM_FRAGATAS; i++) {
        Ship fragata = {
            .id = 300 + i,
            .type = FRAGATA,
            .row = fragata_rows[i],
            .col = 0,
            .alive = 1,
            .last_change = time(NULL)
        };
        board_add_ship(board, fragata);
        ship_pids[ship_count++] = -1;
    }
    
    printf("Tabuleiro inicializado com %d navios\n", board->alive_ships);
    
    // Abrir FIFO para leitura não-bloqueante
    fifo_fd = open(FIFO_PATH, O_RDONLY | O_NONBLOCK);
    if (fifo_fd == -1) {
        perror("open FIFO");
        // Não retornar, servidor pode rodar sem eventos de navio
    }

    // Criar servidor HTTP
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("socket");
        return 1;
    }
    
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        return 1;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(WEB_SERVER_PORT);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        return 1;
    }
    
    if (listen(server_socket, 128) == -1) {
        perror("listen");
        return 1;
    }
    
    printf("Servidor HTTP iniciado na porta %d\n", WEB_SERVER_PORT);
    
    // Loop principal
    while (!shutdown_flag) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_socket, &readfds);
        int max_fd = server_socket;

        if (fifo_fd != -1) {
            FD_SET(fifo_fd, &readfds);
            if (fifo_fd > max_fd) max_fd = fifo_fd;
        }
        
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        
        int activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);
        
        if (activity > 0) {
            if (fifo_fd != -1 && FD_ISSET(fifo_fd, &readfds)) {
                read_fifo_events(fifo_fd);
            }

            if (FD_ISSET(server_socket, &readfds)) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                
                int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
                if (client_socket != -1) {
                    // Ler requisição HTTP (sem timeout - apenas try once)
                    char buffer[8192];
                    ssize_t bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
                    
                    if (bytes > 0) {
                        buffer[bytes] = '\0';
                        handle_http_request(client_socket, buffer);
                    }
                    
                    // Fechar socket corretamente
                    close(client_socket);
                }
            }
        }
    }
    
    // Limpeza
    close(server_socket);
    board_free(board);
    unlink(FIFO_PATH);
    
    printf("Servidor encerrado\n");
    return 0;
}

void signal_handler(int sig) {
    printf("\nRecebido sinal %d, encerrando...\n", sig);
    shutdown_flag = 1;
}

void handle_http_request(int client_socket, const char* request) {
    char method[16], path[256], version[16];
    char* saveptr;
    char* req_copy = strdup(request);
    
    memset(method, 0, sizeof(method));
    memset(path, 0, sizeof(path));
    memset(version, 0, sizeof(version));
    
    // Parse da primeira linha
    char* line = strtok_r(req_copy, "\n", &saveptr);
    if (line) {
        sscanf(line, "%15s %255s %15s", method, path, version);
    }
    
    // Processar requisição
    if (strncmp(method, "GET", 3) == 0) {
        if (strncmp(path, "/estado_local", 13) == 0) {
            // Endpoint: /estado_local
            // Construir JSON com tabuleiro e acertos
            char json[16384];
            char acertos_str[10000] = "";
            
            // Construir lista de navios
            char navios_str[10000] = "";
            int navios_first = 1;
            for (int i = 0; i < TOTAL_NAVIOS; i++) {
                if (board->ships[i].alive) {
                    const char* tipo = "desconhecido";
                    if (board->ships[i].type == PORTA_AVIOES) tipo = "porta_avioes";
                    else if (board->ships[i].type == SUBMARINO) tipo = "submarino";
                    else if (board->ships[i].type == FRAGATA) tipo = "fragata";

                    char navio_item[128];
                    snprintf(navio_item, sizeof(navio_item),
                        "%s{\"id\":%d,\"linha\":%d,\"coluna\":%d,\"tipo\":\"%s\"}",
                        navios_first ? "" : ",",
                        board->ships[i].id,
                        board->ships[i].row,
                        board->ships[i].col,
                        tipo);

                    strcat(navios_str, navio_item);
                    navios_first = 0;
                }
            }

            // Construir lista de acertos
            int acertos_first = 1;
            for (int i = 0; i < board->attack_count; i++) {
                char tipo[20];
                if (board->attacks[i].result == 0) {
                    strcpy(tipo, "agua");
                } else if (board->attacks[i].result == 1) {
                    strcpy(tipo, "acerto");
                } else {
                    strcpy(tipo, "repetido");
                }
                
                char acerto[256];
                snprintf(acerto, sizeof(acerto),
                    "%s{\"linha\":%d,\"coluna\":%d,\"tipo\":\"%s\"}",
                    acertos_first ? "" : ",",
                    board->attacks[i].row,
                    board->attacks[i].col,
                    tipo);
                strcat(acertos_str, acerto);
                acertos_first = 0;
            }
            
            snprintf(json, sizeof(json),
                "{"
                "\"tabuleiro\":{"
                "\"linhas\":%d,"
                "\"colunas\":%d"
                "},"
                "\"navios_ativos\":%d,"
                "\"score_contra\":%d,"
                "\"ataque_count\":%d,"
                "\"navios\":[%s],"
                "\"ataques\":[%s]"
                "}",
                BOARD_ROWS, BOARD_COLS, board->alive_ships, board->score_against,
                board->attack_count, navios_str, acertos_str);
            
            send_json_response(client_socket, json);
        }
        else if (strncmp(path, "/navios", 7) == 0) {
            // Endpoint: /navios (para fins educacionais - revela posição)
            char json[8192];
            char navios_str[6000] = "";
            int navios_len = 0;
            
            int first = 1;
            for (int i = 0; i < TOTAL_NAVIOS && navios_len < 5900; i++) {
                if (board->ships[i].alive) {
                    int len = snprintf(navios_str + navios_len, 6000 - navios_len,
                        "%s{\"id\":%d,\"tipo\":\"%s\",\"linha\":%d,\"coluna\":%d}",
                        first ? "" : ",",
                        board->ships[i].id,
                        get_ship_type_name(board->ships[i].type),
                        board->ships[i].row,
                        board->ships[i].col);
                    if (len > 0) {
                        navios_len += len;
                        first = 0;
                    }
                }
            }
            
            snprintf(json, sizeof(json),
                "{"
                "\"navios\":[%s],"
                "\"total\":%d"
                "}",
                navios_str, board->alive_ships);
            
            send_json_response(client_socket, json);
        }
        else if (strncmp(path, "/status", 7) == 0) {
            // Endpoint: /status
            char json[4096];
            char linhas[2048] = "";
            
            int first = 1;
            for (int i = 0; i < TOTAL_NAVIOS; i++) {
                if (board->ships[i].alive) {
                    char temp[256];
                    snprintf(temp, sizeof(temp),
                        "%s{\"linha\":%d,\"tipo\":\"%s\"}",
                        first ? "" : ",",
                        board->ships[i].row,
                        get_ship_type_name(board->ships[i].type));
                    strcat(linhas, temp);
                    first = 0;
                }
            }
            
            snprintf(json, sizeof(json),
                "{"
                "\"linhas\":[%s],"
                "\"quantidade\":{"
                "\"porta_avioes\":%d,"
                "\"submarinos\":%d,"
                "\"fragatas\":%d"
                "}"
                "}",
                linhas,
                (board->ships[0].alive ? 1 : 0),
                ((board->ships[1].alive ? 1 : 0) + (board->ships[2].alive ? 1 : 0)),
                ((board->ships[3].alive ? 1 : 0) + (board->ships[4].alive ? 1 : 0) + (board->ships[5].alive ? 1 : 0)));
            
            send_json_response(client_socket, json);
        }
        else if (strncmp(path, "/tiro?", 6) == 0) {
            // Endpoint: /tiro?linha=X&coluna=Y
            int linha = -1, coluna = -1;
            
            char* query = strdup(path + 6);
            if (query && strlen(query) > 0) {
                char* token = strtok(query, "&");
                while (token) {
                    if (strncmp(token, "linha=", 6) == 0) {
                        linha = atoi(token + 6);
                    }
                    else if (strncmp(token, "coluna=", 7) == 0) {
                        coluna = atoi(token + 7);
                    }
                    token = strtok(NULL, "&");
                }
            }
            free(query);
            
            if (linha >= 0 && coluna >= 0 && linha < BOARD_ROWS && coluna < BOARD_COLS) {
                int result = board_shoot(board, linha, coluna, "remoto");
                
                char json[512];
                if (result == 2) {
                    snprintf(json, sizeof(json), "{\"resultado\":\"repetido\"}");
                }
                else if (result == 0) {
                    snprintf(json, sizeof(json), "{\"resultado\":\"agua\"}");
                }
                else if (result == PORTA_AVIOES) {
                    snprintf(json, sizeof(json), 
                        "{\"resultado\":\"acerto\",\"tipo\":\"porta_avioes\",\"pontos\":5}");
                }
                else if (result == SUBMARINO) {
                    snprintf(json, sizeof(json), 
                        "{\"resultado\":\"acerto\",\"tipo\":\"submarino\",\"pontos\":3}");
                }
                else if (result == FRAGATA) {
                    snprintf(json, sizeof(json), 
                        "{\"resultado\":\"acerto\",\"tipo\":\"fragata\",\"pontos\":2}");
                }
                else {
                    snprintf(json, sizeof(json), "{\"erro\":\"parametros_invalidos\"}");
                }
                
                send_json_response(client_socket, json);
            } else {
                send_json_response(client_socket, "{\"erro\":\"parametros_invalidos\"}");
            }
        }
        else if (strncmp(path, "/index.html", 11) == 0 || strcmp(path, "/") == 0) {
            // Servir página HTML
            FILE* f = fopen("html/index.html", "r");
            if (f) {
                char html_buffer[65536];
                size_t size = fread(html_buffer, 1, sizeof(html_buffer) - 1, f);
                fclose(f);
                
                if (size > 0) {
                    html_buffer[size] = '\0';
                    
                    char response[70000];
                    snprintf(response, sizeof(response),
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/html\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: close\r\n"
                        "\r\n"
                        "%s",
                        strlen(html_buffer), html_buffer);
                    
                    send(client_socket, response, strlen(response), 0);
                    return;
                }
            }
            send_json_response(client_socket, "{\"erro\":\"arquivo_nao_encontrado\"}");
        }
        else {
            send_json_response(client_socket, "{\"erro\":\"comando_invalido\"}");
        }
    }
    else {
        send_json_response(client_socket, "{\"erro\":\"metodo_nao_permitido\"}");
    }
    
    free(req_copy);
}

void read_fifo_events(int fifo_fd) {
    if (fifo_fd < 0 || !board) return;

    ShipEvent event;
    ssize_t bytes;
    while ((bytes = read(fifo_fd, &event, sizeof(event))) == sizeof(event)) {
        // Localizar navio no tabuleiro
        for (int i = 0; i < TOTAL_NAVIOS; i++) {
            if (board->ships[i].id == event.ship_id && board->ships[i].alive) {
                // Limpar posição anterior
                if (board->ships[i].row >= 0 && board->ships[i].row < BOARD_ROWS &&
                    board->ships[i].col >= 0 && board->ships[i].col < BOARD_COLS) {
                    if (board->board_state[board->ships[i].row][board->ships[i].col] == SHIP) {
                        board->board_state[board->ships[i].row][board->ships[i].col] = EMPTY;
                    }
                }

                // Atualizar posição do navio
                board->ships[i].col = event.col;
                board->ships[i].row = event.row;
                board->ships[i].last_change = event.timestamp;

                // Marcar nova posição
                if (event.row >= 0 && event.row < BOARD_ROWS &&
                    event.col >= 0 && event.col < BOARD_COLS) {
                    board->board_state[event.row][event.col] = SHIP;
                }
                break;
            }
        }
    }

    // Se read retornou 0, FIFO foi fechado pelo escritor; reabrir quando necessário
    if (bytes == 0) {
        close(fifo_fd);
        fifo_fd = open(FIFO_PATH, O_RDONLY | O_NONBLOCK);
        if (fifo_fd == -1) {
            perror("open FIFO read");
        }
    }
}

void send_json_response(int client_socket, const char* json) {
    int json_len = strlen(json);
    
    // Enviar headers separadamente
    char headers[512];
    snprintf(headers, sizeof(headers),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Connection: close\r\n"
        "\r\n",
        json_len);
    
    // Enviar headers
    ssize_t sent = send(client_socket, headers, strlen(headers), 0);
    if (sent == -1) {
        perror("send headers");
        return;
    }
    
    // Enviar JSON
    sent = send(client_socket, json, json_len, 0);
    if (sent == -1) {
        perror("send json");
        return;
    }
}
