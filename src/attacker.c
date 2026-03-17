#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include "config.h"

volatile int shutdown_flag = 0;

typedef struct {
    char ip[20];
    int port;
    int hits;
    int shots;
    int score;
    int ships_destroyed;
} TeamStats;

void signal_handler(int sig) {
    shutdown_flag = 1;
}

// Fazer um HTTP Request e retornar a resposta
char* http_request(const char* host, int port, const char* path) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) return NULL;
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        close(sock);
        return NULL;
    }
    
    // Timeout de 5 segundos para connection
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
    
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        close(sock);
        return NULL;
    }
    
    // Enviar requisição
    char request[512];
    snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Connection: close\r\n"
        "\r\n", path, host, port);
    
    if (send(sock, request, strlen(request), 0) == -1) {
        close(sock);
        return NULL;
    }
    
    // Ler resposta
    char* response = (char*)malloc(8192);
    if (!response) {
        close(sock);
        return NULL;
    }
    
    memset(response, 0, 8192);
    ssize_t bytes = recv(sock, response, 8191, 0);
    
    if (bytes <= 0) {
        free(response);
        close(sock);
        return NULL;
    }
    
    response[bytes] = '\0';
    close(sock);
    
    // Extrair JSON da resposta (após headers)
    char* json_start = strstr(response, "\r\n\r\n");
    if (json_start) {
        char* json = (char*)malloc(2048);
        strcpy(json, json_start + 4);
        free(response);
        return json;
    }
    
    return response;
}

// Fazer um tiro
int shoot(const char* host, int port, int row, int col, TeamStats* stats) {
    char path[256];
    snprintf(path, sizeof(path), "/tiro?linha=%d&coluna=%d", row, col);
    
    char* response = http_request(host, port, path);
    if (!response) return -1;
    
    stats->shots++;
    
    int result = 0;
    if (strstr(response, "agua")) {
        result = 0;
    } else if (strstr(response, "repetido")) {
        result = -1;
    } else if (strstr(response, "acerto")) {
        stats->hits++;
        if (strstr(response, "porta_avioes")) {
            stats->score += 5;
            result = 5;
        } else if (strstr(response, "submarino")) {
            stats->score += 3;
            result = 3;
        } else if (strstr(response, "fragata")) {
            stats->score += 2;
            result = 2;
        }
    }
    
    free(response);
    return result;
}

// Obter status de uma equipe
int get_status(const char* host, int port, TeamStats* stats) {
    char* response = http_request(host, port, "/status");
    if (!response) return -1;
    
    // Contar quantos navios ainda estão vivos
    int ships = 0;
    if (strstr(response, "porta_avioes")) ships++;
    if (strstr(response, "submarino")) ships += 2;
    if (strstr(response, "fragata")) ships += 3;
    
    free(response);
    return ships;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Ler arquivo de equipes
    FILE* f = fopen("config/teams.txt", "r");
    if (!f) {
        fprintf(stderr, "Erro ao abrir config/teams.txt\n");
        return 1;
    }
    
    TeamStats teams[10];
    int team_count = 0;
    char line[100];
    
    while (fgets(line, sizeof(line), f) && team_count < 10) {
        // Remover comentários e linhas em branco
        if (line[0] == '#' || line[0] == '\n') continue;
        
        int port = WEB_SERVER_PORT;
        if (sscanf(line, "%19[^:]:%d", teams[team_count].ip, &port) == 2) {
            teams[team_count].port = port;
        } else if (sscanf(line, "%19s", teams[team_count].ip) == 1) {
            teams[team_count].port = WEB_SERVER_PORT;
        } else {
            continue;
        }
        
        teams[team_count].hits = 0;
        teams[team_count].shots = 0;
        teams[team_count].score = 0;
        teams[team_count].ships_destroyed = 0;
        
        printf("Equipe carregada: %s:%d\n", teams[team_count].ip, teams[team_count].port);
        team_count++;
    }
    fclose(f);
    
    if (team_count == 0) {
        fprintf(stderr, "Nenhuma equipe carregada\n");
        return 1;
    }
    
    printf("\n=== INICIANDO ATAQUE (máximo %d tiros) ===\n\n", MAX_SHOTS);
    
    int total_shots = 0;
    int team_idx = 0;
    
    // Estratégia SIMPLES e EFICIENTE: Varrer linha por linha
    // Quando acertar, destruir o navio expandindo na mesma linha
    printf("[ ESTRATÉGIA ] Varredura sistemática linha-por-linha com destruição\n\n");
    
    // Varrer sequencialmente linha por linha
    for (int row = 0; row < 10 && total_shots < MAX_SHOTS; row++) {
        for (int col = 0; col < 10 && total_shots < MAX_SHOTS; col++) {
            if (shutdown_flag) break;
            
            total_shots++;
            
            printf("[%d/%d] Atacando %s:%d em (%d,%d)... ",
                total_shots, MAX_SHOTS,
                teams[team_idx].ip, teams[team_idx].port,
                row, col);
            fflush(stdout);
            
            int result = shoot(teams[team_idx].ip, teams[team_idx].port, row, col, &teams[team_idx]);
            
            if (result == -1) {
                printf("ERRO\n");
            } else if (result == 0) {
                printf("ÁGUA\n");
            } else if (result == 2) {
                printf("REPETIDO\n");
            } else if (result > 0) {
                printf("ACERTO! (+%d) - Destruindo...\n", result);
                
                // Navio encontrado! Expandir DIREITA e ESQUERDA na mesma linha
                int destroyed = 0;
                
                // Expandir DIREITA
                for (int c = col + 1; c < 10 && total_shots < MAX_SHOTS; c++) {
                    total_shots++;
                    printf("[%d/%d] %s:%d em (%d,%d)... ",
                        total_shots, MAX_SHOTS, teams[team_idx].ip, 
                        teams[team_idx].port, row, c);
                    fflush(stdout);
                    
                    int r = shoot(teams[team_idx].ip, teams[team_idx].port, row, c, &teams[team_idx]);
                    if (r > 0) {
                        printf("ACERTO! (+%d)\n", r);
                        destroyed = 1;
                        usleep(25000);
                    } else {
                        printf("água\n");
                        break;
                    }
                }
                
                // Expandir ESQUERDA
                for (int c = col - 1; c >= 0 && total_shots < MAX_SHOTS; c--) {
                    total_shots++;
                    printf("[%d/%d] %s:%d em (%d,%d)... ",
                        total_shots, MAX_SHOTS, teams[team_idx].ip, 
                        teams[team_idx].port, row, c);
                    fflush(stdout);
                    
                    int r = shoot(teams[team_idx].ip, teams[team_idx].port, row, c, &teams[team_idx]);
                    if (r > 0) {
                        printf("ACERTO! (+%d)\n", r);
                        destroyed = 1;
                        usleep(25000);
                    } else {
                        printf("água\n");
                        break;
                    }
                }
                
                // Se ainda não destruído, procura na coluna
                if (!destroyed && total_shots < MAX_SHOTS) {
                    // Expandir ABAIXO
                    for (int r = row + 1; r < 10 && total_shots < MAX_SHOTS; r++) {
                        total_shots++;
                        printf("[%d/%d] %s:%d em (%d,%d)... ",
                            total_shots, MAX_SHOTS, teams[team_idx].ip, 
                            teams[team_idx].port, r, col);
                        fflush(stdout);
                        
                        int res = shoot(teams[team_idx].ip, teams[team_idx].port, r, col, &teams[team_idx]);
                        if (res > 0) {
                            printf("ACERTO! (+%d)\n", res);
                            usleep(25000);
                        } else {
                            printf("água\n");
                            break;
                        }
                    }
                    
                    // Expandir ACIMA
                    for (int r = row - 1; r >= 0 && total_shots < MAX_SHOTS; r--) {
                        total_shots++;
                        printf("[%d/%d] %s:%d em (%d,%d)... ",
                            total_shots, MAX_SHOTS, teams[team_idx].ip, 
                            teams[team_idx].port, r, col);
                        fflush(stdout);
                        
                        int res = shoot(teams[team_idx].ip, teams[team_idx].port, r, col, &teams[team_idx]);
                        if (res > 0) {
                            printf("ACERTO! (+%d)\n", res);
                            usleep(25000);
                        } else {
                            printf("água\n");
                            break;
                        }
                    }
                }
            }
            
            usleep(25000);
        }
    }
    
    // Exibir relatório final
    printf("\n=== RELATÓRIO FINAL ===\n\n");
    printf("Total de tiros disparados: %d\n\n", total_shots);
    
    int total_score = 0;
    int total_hits = 0;
    
    for (int i = 0; i < team_count; i++) {
        printf("Equipe: %s:%d\n", teams[i].ip, teams[i].port);
        printf("  Tiros: %d\n", teams[i].shots);
        printf("  Acertos: %d\n", teams[i].hits);
        printf("  Pontuação: %d\n", teams[i].score);
        printf("\n");
        
        total_score += teams[i].score;
        total_hits += teams[i].hits;
    }
    
    printf("=== RESUMO ===\n");
    printf("Total de acertos: %d\n", total_hits);
    printf("Pontuação total: %d\n", total_score);
    printf("Taxa de acerto: %.1f%%\n", 
        total_shots > 0 ? (total_hits * 100.0 / total_shots) : 0);
    
    return 0;
}
