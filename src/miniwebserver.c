#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>
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
void handle_http_request(int client_socket, const char* request, struct sockaddr_in* client_addr);
void send_json_response(int client_socket, const char* json);
void read_fifo_events();
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
    signal(SIGCHLD, SIG_IGN); // Evitar zombies
    
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
    
    // Inicializar navios no board (linhas fixas)
    int ship_rows[] = {1, 3, 5, 0, 2, 4};  // PA, Sub1, Sub2, Frag1, Frag2, Frag3
    int ship_types[] = {PORTA_AVIOES, SUBMARINO, SUBMARINO, FRAGATA, FRAGATA, FRAGATA};
    int ship_ids[] = {100, 200, 201, 300, 301, 302};
    
    for (int i = 0; i < TOTAL_NAVIOS; i++) {
        Ship s = {
            .id = ship_ids[i],
            .type = ship_types[i],
            .row = ship_rows[i],
            .col = 0,
            .alive = 1,
            .last_change = time(NULL)
        };
        board_add_ship(board, s);
        ship_pids[i] = -1;
    }
    
    printf("Tabuleiro inicializado com %d navios\n", board->alive_ships);
    
    // Abrir FIFO para leitura não-bloqueante (antes de fork para que filhos possam escrever)
    // Precisamos abrir com O_RDWR para evitar que select() retorne imediatamente quando não há escritores
    fifo_fd = open(FIFO_PATH, O_RDONLY | O_NONBLOCK);
    if (fifo_fd == -1) {
        perror("open FIFO");
    }
    
    // Criar processos navio via fork()
    create_ships();
    
    // Criar servidor HTTP
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("socket");
        destroy_ships();
        return 1;
    }
    
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        destroy_ships();
        return 1;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(WEB_SERVER_PORT);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        destroy_ships();
        return 1;
    }
    
    if (listen(server_socket, 128) == -1) {
        perror("listen");
        destroy_ships();
        return 1;
    }
    
    printf("Servidor HTTP iniciado na porta %d\n", WEB_SERVER_PORT);
    
    // Loop principal com select()
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
            // Ler eventos da FIFO
            if (fifo_fd != -1 && FD_ISSET(fifo_fd, &readfds)) {
                read_fifo_events();
            }

            // Aceitar conexão HTTP
            if (FD_ISSET(server_socket, &readfds)) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                
                int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
                if (client_socket != -1) {
                    char buffer[8192];
                    ssize_t bytes = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
                    
                    if (bytes > 0) {
                        buffer[bytes] = '\0';
                        handle_http_request(client_socket, buffer, &client_addr);
                    }
                    
                    close(client_socket);
                }
            }
        }
    }
    
    // Limpeza
    close(server_socket);
    destroy_ships();
    board_free(board);
    unlink(FIFO_PATH);
    
    printf("Servidor encerrado\n");
    return 0;
}

void signal_handler(int sig) {
    printf("\nRecebido sinal %d, encerrando...\n", sig);
    shutdown_flag = 1;
}

// Criar processos navio via fork() + execl()
void create_ships() {
    printf("Criando processos navio via fork()...\n");
    
    for (int i = 0; i < TOTAL_NAVIOS; i++) {
        pid_t pid = fork();
        
        if (pid == -1) {
            perror("fork");
            continue;
        }
        
        if (pid == 0) {
            // Processo filho: executar binário navio
            char id_str[16], type_str[16], row_str[16];
            snprintf(id_str, sizeof(id_str), "%d", board->ships[i].id);
            snprintf(type_str, sizeof(type_str), "%d", board->ships[i].type);
            snprintf(row_str, sizeof(row_str), "%d", board->ships[i].row);
            
            execl("./bin/navio", "navio", id_str, type_str, row_str, NULL);
            // Se execl falhar, tentar path relativo alternativo
            execl("bin/navio", "navio", id_str, type_str, row_str, NULL);
            perror("execl navio");
            _exit(1);
        }
        
        // Processo pai: armazenar PID
        ship_pids[i] = pid;
        printf("  Navio %d (tipo=%d, linha=%d) criado com PID %d\n",
               board->ships[i].id, board->ships[i].type, board->ships[i].row, pid);
    }
    
    printf("Todos os %d navios foram criados\n", TOTAL_NAVIOS);
}

// Destruir todos os processos navio
void destroy_ships() {
    printf("Encerrando processos navio...\n");
    for (int i = 0; i < TOTAL_NAVIOS; i++) {
        if (ship_pids[i] > 0) {
            kill(ship_pids[i], SIGTERM);
            waitpid(ship_pids[i], NULL, WNOHANG);
            printf("  Navio PID %d encerrado\n", ship_pids[i]);
            ship_pids[i] = -1;
        }
    }
}

void handle_http_request(int client_socket, const char* request, struct sockaddr_in* client_addr) {
    char method[16], path[256], version[16];
    char* saveptr;
    char* req_copy = strdup(request);
    
    memset(method, 0, sizeof(method));
    memset(path, 0, sizeof(path));
    memset(version, 0, sizeof(version));
    
    // Extrair IP do atacante
    char attacker_ip[20];
    if (client_addr) {
        strncpy(attacker_ip, inet_ntoa(client_addr->sin_addr), sizeof(attacker_ip) - 1);
        attacker_ip[sizeof(attacker_ip) - 1] = '\0';
    } else {
        strcpy(attacker_ip, "desconhecido");
    }
    
    // Parse da primeira linha
    char* line = strtok_r(req_copy, "\n", &saveptr);
    if (line) {
        sscanf(line, "%15s %255s %15s", method, path, version);
    }
    
    // Processar requisição
    if (strncmp(method, "GET", 3) == 0) {
        if (strncmp(path, "/estado_local", 13) == 0) {
            // Endpoint: /estado_local – estado completo para a página HTML
            char json[24576];
            char acertos_str[12000] = "";
            char navios_str[10000] = "";
            int navios_first = 1;
            
            for (int i = 0; i < TOTAL_NAVIOS; i++) {
                if (board->ships[i].alive) {
                    const char* tipo = get_ship_type_name(board->ships[i].type);
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

            // Construir lista de ataques com IP e horário
            int acertos_first = 1;
            for (int i = 0; i < board->attack_count; i++) {
                char resultado[20];
                if (board->attacks[i].result == 0) {
                    strcpy(resultado, "agua");
                } else if (board->attacks[i].result == 1) {
                    strcpy(resultado, "acerto");
                } else {
                    strcpy(resultado, "repetido");
                }
                
                // Formatar horário
                char hora_str[32] = "";
                time_t ts = (time_t)board->attacks[i].timestamp;
                struct tm* tm_info = localtime(&ts);
                if (tm_info) {
                    strftime(hora_str, sizeof(hora_str), "%H:%M:%S", tm_info);
                }
                
                char acerto[512];
                snprintf(acerto, sizeof(acerto),
                    "%s{\"linha\":%d,\"coluna\":%d,\"tipo\":\"%s\",\"ip\":\"%s\",\"horario\":\"%s\"}",
                    acertos_first ? "" : ",",
                    board->attacks[i].row,
                    board->attacks[i].col,
                    resultado,
                    board->attacks[i].attacker_ip,
                    hora_str);
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
        else if (strncmp(path, "/status", 7) == 0) {
            // Endpoint: /status – informações públicas (sem revelar coluna)
            char json[4096];
            char linhas[2048] = "";
            
            int first = 1;
            int pa_count = 0, sub_count = 0, frag_count = 0;
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
                    
                    if (board->ships[i].type == PORTA_AVIOES) pa_count++;
                    else if (board->ships[i].type == SUBMARINO) sub_count++;
                    else if (board->ships[i].type == FRAGATA) frag_count++;
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
                linhas, pa_count, sub_count, frag_count);
            
            send_json_response(client_socket, json);
        }
        else if (strncmp(path, "/tiro", 5) == 0) {
            // Endpoint: /tiro?linha=L&coluna=C
            
            // Verificar se tem query string
            char* query_start = strchr(path + 5, '?');
            if (!query_start) {
                send_json_response(client_socket, "{\"erro\":\"parametros_ausentes\"}");
                free(req_copy);
                return;
            }
            
            int linha = -1, coluna = -1;
            int has_linha = 0, has_coluna = 0;
            
            char* query = strdup(query_start + 1);
            if (query && strlen(query) > 0) {
                char* token = strtok(query, "&");
                while (token) {
                    if (strncmp(token, "linha=", 6) == 0) {
                        linha = atoi(token + 6);
                        has_linha = 1;
                    }
                    else if (strncmp(token, "coluna=", 7) == 0) {
                        coluna = atoi(token + 7);
                        has_coluna = 1;
                    }
                    token = strtok(NULL, "&");
                }
            }
            free(query);
            
            if (!has_linha || !has_coluna) {
                send_json_response(client_socket, "{\"erro\":\"parametros_ausentes\"}");
            }
            else if (linha < 0 || coluna < 0 || linha >= BOARD_ROWS || coluna >= BOARD_COLS) {
                send_json_response(client_socket, "{\"erro\":\"fora_do_tabuleiro\"}");
            }
            else {
                int result = board_shoot(board, linha, coluna, attacker_ip);
                
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
                    // Matar o processo navio correspondente
                    for (int i = 0; i < TOTAL_NAVIOS; i++) {
                        if (!board->ships[i].alive && board->ships[i].type == PORTA_AVIOES && ship_pids[i] > 0) {
                            printf("Matando navio %d (PID %d) - atingido por %s\n", 
                                   board->ships[i].id, ship_pids[i], attacker_ip);
                            kill(ship_pids[i], SIGTERM);
                            ship_pids[i] = -1;
                            break;
                        }
                    }
                }
                else if (result == SUBMARINO) {
                    snprintf(json, sizeof(json), 
                        "{\"resultado\":\"acerto\",\"tipo\":\"submarino\",\"pontos\":3}");
                    for (int i = 0; i < TOTAL_NAVIOS; i++) {
                        if (!board->ships[i].alive && board->ships[i].type == SUBMARINO && ship_pids[i] > 0) {
                            printf("Matando navio %d (PID %d) - atingido por %s\n",
                                   board->ships[i].id, ship_pids[i], attacker_ip);
                            kill(ship_pids[i], SIGTERM);
                            ship_pids[i] = -1;
                            break;
                        }
                    }
                }
                else if (result == FRAGATA) {
                    snprintf(json, sizeof(json), 
                        "{\"resultado\":\"acerto\",\"tipo\":\"fragata\",\"pontos\":2}");
                    for (int i = 0; i < TOTAL_NAVIOS; i++) {
                        if (!board->ships[i].alive && board->ships[i].type == FRAGATA && ship_pids[i] > 0) {
                            printf("Matando navio %d (PID %d) - atingido por %s\n",
                                   board->ships[i].id, ship_pids[i], attacker_ip);
                            kill(ship_pids[i], SIGTERM);
                            ship_pids[i] = -1;
                            break;
                        }
                    }
                }
                else {
                    snprintf(json, sizeof(json), "{\"erro\":\"erro_interno\"}");
                }
                
                send_json_response(client_socket, json);
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
                    free(req_copy);
                    return;
                }
            }
            send_json_response(client_socket, "{\"erro\":\"arquivo_nao_encontrado\"}");
        }
        else {
            // Endpoint inexistente
            send_json_response(client_socket, "{\"erro\":\"comando_invalido\"}");
        }
    }
    else {
        send_json_response(client_socket, "{\"erro\":\"metodo_nao_permitido\"}");
    }
    
    free(req_copy);
}

void read_fifo_events() {
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

    // Se read retornou 0, FIFO foi fechada pelo escritor; reabrir
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
    
    ssize_t sent = send(client_socket, headers, strlen(headers), 0);
    if (sent == -1) {
        perror("send headers");
        return;
    }
    
    sent = send(client_socket, json, json_len, 0);
    if (sent == -1) {
        perror("send json");
        return;
    }
}
