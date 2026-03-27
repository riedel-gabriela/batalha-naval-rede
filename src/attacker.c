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
#include <stdbool.h>
#include "config.h"

volatile int shutdown_flag = 0;

typedef struct {
    char ip[20];
    int port;
    int hits;
    int shots;
    int score;
    int ships_destroyed;
    int pa_destroyed;
    int sub_destroyed;
    int frag_destroyed;
    int last_ship_count;
    int target_queue[BOARD_ROWS * BOARD_COLS][2];
    int target_queue_len;
    int tried[BOARD_ROWS][BOARD_COLS];
    int finished;
} TeamStats;

void signal_handler(int sig) {
    (void)sig;
    shutdown_flag = 1;
}

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

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        close(sock);
        return NULL;
    }

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

    char* response = (char*)malloc(16384);
    if (!response) {
        close(sock);
        return NULL;
    }

    memset(response, 0, 16384);
    ssize_t bytes = recv(sock, response, 16383, 0);
    if (bytes <= 0) {
        free(response);
        close(sock);
        return NULL;
    }

    response[bytes] = '\0';
    close(sock);

    char* json_start = strstr(response, "\r\n\r\n");
    if (json_start) {
        char* json = strdup(json_start + 4);
        free(response);
        return json;
    }

    return response;
}

static int extract_json_int_from(const char* start, const char* key) {
    if (!start || !key) return 0;

    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char* p = strstr(start, pattern);
    if (!p) return 0;

    p = strchr(p, ':');
    if (!p) return 0;
    p++;

    while (*p == ' ') p++;
    return atoi(p);
}

static int parse_status_rows(const char* json, int rows[BOARD_ROWS]) {
    int count = 0;
    const char* p = json;

    while ((p = strstr(p, "\"linha\"")) != NULL && count < BOARD_ROWS) {
        p = strchr(p, ':');
        if (!p) break;
        p++;

        int row = atoi(p);
        if (row >= 0 && row < BOARD_ROWS) {
            int found = 0;
            for (int i = 0; i < count; i++) {
                if (rows[i] == row) { found = 1; break; }
            }
            if (!found) rows[count++] = row;
        }
        p++;
    }

    return count;
}

static int pop_target_cell(TeamStats* team, int* row, int* col) {
    if (!team || team->target_queue_len <= 0) return 0;
    team->target_queue_len--;
    *row = team->target_queue[team->target_queue_len][0];
    *col = team->target_queue[team->target_queue_len][1];
    return 1;
}

static void enqueue_target_neighbor(TeamStats* team, int row, int col) {
    if (!team || team->target_queue_len >= BOARD_ROWS * BOARD_COLS) return;
    const int dr[4] = {-1, 1, 0, 0};
    const int dc[4] = {0, 0, -1, 1};

    for (int i = 0; i < 4; i++) {
        int nr = row + dr[i];
        int nc = col + dc[i];
        if (nr >= 0 && nr < BOARD_ROWS && nc >= 0 && nc < BOARD_COLS && !team->tried[nr][nc]) {
            team->target_queue[team->target_queue_len][0] = nr;
            team->target_queue[team->target_queue_len][1] = nc;
            team->target_queue_len++;
        }
    }
}

static int find_next_search_cell(TeamStats* team, int rows[BOARD_ROWS], int row_count, int* out_row, int* out_col) {
    if (row_count > 0) {
        for (int ri = 0; ri < row_count; ri++) {
            int row = rows[ri];
            if (row < 0 || row >= BOARD_ROWS) continue;
            int parity = row % 2;
            for (int col = parity; col < BOARD_COLS; col += 2) {
                if (!team->tried[row][col]) {
                    *out_row = row;
                    *out_col = col;
                    return 1;
                }
            }
        }
    }

    for (int row = 0; row < BOARD_ROWS; row++) {
        int parity = row % 2;
        for (int col = parity; col < BOARD_COLS; col += 2) {
            if (!team->tried[row][col]) {
                *out_row = row;
                *out_col = col;
                return 1;
            }
        }
    }

    for (int row = 0; row < BOARD_ROWS; row++) {
        for (int col = 0; col < BOARD_COLS; col++) {
            if (!team->tried[row][col]) {
                *out_row = row;
                *out_col = col;
                return 1;
            }
        }
    }

    return 0;
}

int get_status(const char* host, int port, int rows[BOARD_ROWS], int* row_count) {
    char* response = http_request(host, port, "/status");
    if (!response) return -1;

    /* Navegar até a seção "quantidade" para evitar
       falso-positivos com nomes de tipo nos navios */
    const char* quantidade = strstr(response, "\"quantidade\"");
    if (!quantidade) {
        free(response);
        return -1;
    }

    int porta      = extract_json_int_from(quantidade, "porta_avioes");
    int submarinos = extract_json_int_from(quantidade, "submarinos");
    int fragatas   = extract_json_int_from(quantidade, "fragatas");

    if (row_count) {
        *row_count = parse_status_rows(response, rows);
    }

    free(response);
    return porta + submarinos + fragatas;
}

int shoot(const char* host, int port, int row, int col, TeamStats* stats) {
    char path[256];
    snprintf(path, sizeof(path), "/tiro?linha=%d&coluna=%d", row, col);

    char* response = http_request(host, port, path);
    if (!response) return -1;

    stats->shots++;

    int result = -1;
    if (strstr(response, "repetido")) {
        result = 2;
    } else if (strstr(response, "acerto")) {
        stats->hits++;
        if (strstr(response, "porta_avioes")) {
            stats->score += 5;
            stats->pa_destroyed++;
            result = 5;
        } else if (strstr(response, "submarino")) {
            stats->score += 3;
            stats->sub_destroyed++;
            result = 3;
        } else if (strstr(response, "fragata")) {
            stats->score += 2;
            stats->frag_destroyed++;
            result = 2;
        } else {
            result = 1;
        }
    } else if (strstr(response, "agua")) {
        result = 0;
    } else {
        result = -1;
    }

    free(response);
    return result;
}

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    FILE* f = fopen("config/teams.txt", "r");
    if (!f) {
        fprintf(stderr, "Erro ao abrir config/teams.txt\n");
        return 1;
    }

    TeamStats teams[10];
    int team_count = 0;
    char line[100];

    while (fgets(line, sizeof(line), f) && team_count < 10) {
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
        teams[team_count].pa_destroyed = 0;
        teams[team_count].sub_destroyed = 0;
        teams[team_count].frag_destroyed = 0;
        teams[team_count].last_ship_count = -1;
        teams[team_count].target_queue_len = 0;
        teams[team_count].finished = 0;
        for (int r = 0; r < BOARD_ROWS; r++) {
            for (int c = 0; c < BOARD_COLS; c++) {
                teams[team_count].tried[r][c] = 0;
            }
        }

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
    int active_teams = team_count;

    while (total_shots < MAX_SHOTS && active_teams > 0 && !shutdown_flag) {
        for (int team_idx = 0; team_idx < team_count && total_shots < MAX_SHOTS && !shutdown_flag; team_idx++) {
            TeamStats* team = &teams[team_idx];
            if (team->finished) continue;

            int rows[BOARD_ROWS] = {0};
            int row_count = 0;
            int ships_alive = get_status(team->ip, team->port, rows, &row_count);

            if (ships_alive < 0) {
                printf("[Equipe %s:%d] erro ao consultar status (re-tentando depois)\n", team->ip, team->port);
                continue;
            }

            if (team->last_ship_count < 0) team->last_ship_count = ships_alive;

            if (ships_alive == 0) {
                printf("[Equipe %s:%d] nenhum navio ativo (finalizado)\n", team->ip, team->port);
                team->finished = 1;
                active_teams--;
                continue;
            }

            if (row_count == 0) {
                for (int i = 0; i < BOARD_ROWS; i++) rows[i] = i;
                row_count = BOARD_ROWS;
            }

            int target_row=-1, target_col=-1;
            if (!pop_target_cell(team, &target_row, &target_col)) {
                if (!find_next_search_cell(team, rows, row_count, &target_row, &target_col)) {
                    // Navios se movem, resetar varredura para nova tentativa
                    printf("[Equipe %s:%d] resetando varredura (navios se movem)\n", team->ip, team->port);
                    for (int r = 0; r < BOARD_ROWS; r++)
                        for (int c = 0; c < BOARD_COLS; c++)
                            team->tried[r][c] = 0;
                    team->target_queue_len = 0;
                    continue;
                }
            }

            team->tried[target_row][target_col] = 1;
            total_shots++;

            printf("[%d/%d] Atacando %s:%d em (%d,%d)... ", total_shots, MAX_SHOTS, team->ip, team->port, target_row, target_col);
            fflush(stdout);

            int result = shoot(team->ip, team->port, target_row, target_col, team);
            if (result == -1) {
                printf("ERRO\n");
                continue;
            }

            if (result == 0) {
                printf("ÁGUA\n");
            } else if (result == 2) {
                printf("REPETIDO\n");
            } else {
                printf("ACERTO! (+%d)\n", result);
                enqueue_target_neighbor(team, target_row, target_col);

                int remaining = get_status(team->ip, team->port, NULL, NULL);
                if (remaining >= 0) {
                    if (remaining < team->last_ship_count) {
                        team->ships_destroyed += (team->last_ship_count - remaining);
                        team->last_ship_count = remaining;
                    }
                    if (remaining == 0) {
                        printf("[Equipe %s:%d] todos navios destruídos!\n", team->ip, team->port);
                        team->finished = 1;
                        active_teams--;
                    }
                }
            }

            usleep(25000);
        }
    }

    printf("\n=== RELATÓRIO FINAL ===\n\n");
    printf("Total de tiros disparados: %d\n\n", total_shots);

    int total_score = 0;
    int total_hits = 0;

    for (int i = 0; i < team_count; i++) {
        printf("Equipe: %s:%d\n", teams[i].ip, teams[i].port);
        printf("  Tiros: %d\n", teams[i].shots);
        printf("  Acertos: %d\n", teams[i].hits);
        printf("  Navios destruídos: %d\n", teams[i].ships_destroyed);
        printf("    Porta-aviões: %d\n", teams[i].pa_destroyed);
        printf("    Submarinos:   %d\n", teams[i].sub_destroyed);
        printf("    Fragatas:     %d\n", teams[i].frag_destroyed);
        printf("  Pontuação: %d\n", teams[i].score);
        printf("\n");

        total_score += teams[i].score;
        total_hits += teams[i].hits;
    }

    printf("=== RESUMO ===\n");
    printf("Total de acertos: %d\n", total_hits);
    printf("Pontuação total: %d\n", total_score);
    printf("Taxa de acerto: %.1f%%\n", total_shots > 0 ? (total_hits * 100.0 / total_shots) : 0.0);

    return 0;
}
