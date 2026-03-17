# Arquitetura - Batalha Naval em Rede

## Visão Geral da Arquitetura

```
┌─────────────────────────────────────────────────────────────────┐
│                    EQUIPE LOCAL                                 │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │  Navio 1     │  │  Navio 2     │  │  ...Navio 6  │          │
│  │  (processo)  │  │  (processo)  │  │  (processo)  │          │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘          │
│         │                 │                 │                   │
│         └─────────────────┼─────────────────┘                   │
│                           │                                     │
│                    ShipEvent (FIFO)                             │
│                           │                                     │
│                           ↓                                     │
│         ┌─────────────────────────────────────┐                │
│         │                                     │                │
│         │   MINIWEBSERVER (HTTP)              │                │
│         │   Porta: 8080                       │                │
│         │                                     │                │
│         ├─────────────────────────────────────┤                │
│         │  - Thread/Process: Accept requests  │                │
│         │  - Lê eventos via FIFO              │                │
│         │  - Mantém Board state               │                │
│         │  - Arbitra tiros                    │                │
│         │  - Responde HTTP                    │                │
│         │                                     │                │
│         ├─ /estado_local  → Full Board JSON  │                │
│         ├─ /status        → Public info      │                │
│         ├─ /tiro?...      → Shoot & Result   │                │
│         └─ /index.html    → UI Page          │                │
│           │                                   │                │
│           ├──── Querido por ────────────────── Browser (HTML+JS)
│           │                                   │                │
│           └──── Atacado via ───────────────────→ Equipes Remotas
│                                               │                │
│         ┌─────────────────────────────────────┐   (HTTP)       │
│         │          Board State                │                │
│         ├─────────────────────────────────────┤                │
│         │  Ship[6]                            │                │
│         │  - id, type, row, col, alive        │                │
│         │  board[10x10]                       │                │
│         │  - estado de cada célula            │                │
│         │  AttackRecord[]                     │                │
│         │  - histórico de tiros               │                │
│         └─────────────────────────────────────┘                │
│                                                                 │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │         ATACANTE REMOTO (opcional)                       │ │
│  │  bin/attacker                                            │ │
│  │  - Lê config/teams.txt                                   │ │
│  │  - Faz até 100 requisições HTTP /tiro                   │ │
│  │  - Gera relatório final                                  │ │
│  └──────────────────────────────────────────────────────────┘ │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

## Fluxo de Dados

### 1. Inicialização (Startup)

```
main(miniwebserver)
  │
  ├─ mkfifo(/tmp/batalha_naval_fifo)
  │
  ├─ board_init()
  │   └─ Aloca Board struct
  │       └─ Cria 6 navios (Ship[6])
  │
  ├─ socket(AF_INET, SOCK_STREAM)
  │   └─ Bind port 8080
  │       └─ Listen para conexões
  │
  └─ Loop principal: select() + HTTP accept + FIFO read
```

### 2. Movimento de Navio

```
navio.c (processo fork)
  │
  ├─ Recebe: ID, tipo, linha via argv
  │
  └─ Loop:
     ├─ Aguarda 5 segundos em coluna atual
     │
     ├─ Move para próxima coluna
     │   (esquerda → direita, cicla ao final)
     │
     └─ Escreve ShipEvent na FIFO:
        {
          ship_id: 100,
          ship_type: 5,
          row: 1,
          col: 3,
          timestamp: 1710779600
        }
```

### 3. Recebimento de Evento Navio

```
miniwebserver - Loop principal
  │
  └─ select() aguarda leitura na FIFO
     │
     ├─ Lê ShipEvent da FIFO
     │
     └─ Atualiza Board:
        ├─ Encontra Ship por ID
        ├─ Atualiza col
        └─ board_state[row][col] = SHIP
```

### 4. Requisição HTTP /tiro

```
Cliente HTTP (atacante ou interface web)
  │
  └─ GET /tiro?linha=3&coluna=5
     │
     ↓ (socket TCP chegando em miniwebserver)
     │
     miniwebserver - handle_http_request()
     │
     ├─ Parse URL: linha=3, coluna=5
     │
     ├─ Validar: 0 <= linha,col < 10
     │
     ├─ board_shoot(3, 5, "IP_atacante")
     │   │
     │   ├─ board_is_hit(3, 5)?
     │   │   └─ SIM: retorna REPETIDO (resultado=2)
     │   │   └─ NÃO: continua...
     │   │
     │   └─ board_get_ship_at(3, 5)
     │       ├─ Navio encontrado (ID=200, type=3)
     │       │   │
     │       │   ├─ ship.alive = 0
     │       │   ├─ board->alive_ships--
     │       │   ├─ AttackRecord com resultado=1 (ACERTO)
     │       │   └─ return 3 (tipo)
     │       │
     │       └─ Nenhum navio
     │           ├─ AttackRecord com resultado=0 (ÁGUA)
     │           └─ return 0
     │
     └─ Enviar JSON response via socket:
        {
          "resultado": "acerto",
          "tipo": "submarino",
          "pontos": 3
        }
```

### 5. Consulta /status

```
Cliente HTTP (equipe remota)
  │
  └─ GET /status
     │
     ↓
     │
     miniwebserver - handle_http_request()
     │
     ├─ Iterar Ship[6]
     │   └─ Para cada navio vivo: adicionar ao JSON
     │
     └─ Enviar JSON:
        {
          "linhas": [
            {"linha": 1, "tipo": "porta_avioes"},
            {"linha": 3, "tipo": "submarino"},
            ...
          ],
          "quantidade": {
            "porta_avioes": 1,
            "submarinos": 2,
            "fragatas": 3
          }
        }
```

## Estruturas de Dados Chave

### Ship (include/ship.h)

```c
typedef struct {
    int id;              // 100-302
    int type;            // 2, 3, ou 5
    int row;             // Linha fixa (0-9)
    int col;             // Coluna atual (0-9)
    int alive;           // 1 ou 0
    time_t last_change;  // Para tracking de movimento
} Ship;
```

**Exemplo:**
```
Port.Aviões: {id:100, type:5, row:1, col:4, alive:1}
Submarino1:  {id:200, type:3, row:3, col:6, alive:0}
Fragata1:    {id:300, type:2, row:2, col:7, alive:1}
```

### Board (include/board.h)

```c
typedef struct {
    Ship ships[6];                        // Array de navios
    int alive_ships;                      // Contador (0-6)
    int board_state[10][10];              // Estado visual
    AttackRecord attacks[100];            // Histórico
    int attack_count;                     // Quantidade
    int score_against;                    // Pontos sofridos
} Board;
```

**board_state[][] valores:**
- `EMPTY (0)`: Sem navio
- `SHIP (1)`: Navio presente
- `HIT (2)`: Posição atingida
- `WATER (3)`: Água (tiro naquele local)

### ShipEvent (para FIFO)

```c
typedef struct {
    int ship_id;        // Qual navio
    int ship_type;      // Tipo do navio
    int row;            // Linha fixa
    int col;            // Coluna atual
    time_t timestamp;   // Quando foi descoberto
} ShipEvent;
```

**Tamanho:** 24 bytes (enviado via FIFO)

## Comunicação Intra-Processo (IPC)

### FIFO (Named Pipe)

**Path:** `/tmp/batalha_naval_fifo`

**Direção:** Navios → Miniwebserver (unidirecional)

**Protocolo:** Binary (ShipEvent struct)

**Fluxo:**
```
navio.c                         miniwebserver.c
│                                     │
├─ open(FIFO, O_WRONLY)               │
│                                     │
│                    ┌────────────────┼─ open(FIFO, O_RDONLY)
│                    │                │
├─ write(&event)     │                │
│                    ├─→ select()     │
│                    │    detected    │
│                    ├─→ read(&event) │
│                    │  process event │
│                    │  update board  │
│                    │                │
└─ ... aguarda 5s    └─ loop continua │
   escreve novamente │                │
                     └────────────────┤
```

**Vantagens:**
- Simples de implementar
- Uso de select() para não-bloqueante
- Apenas escrita de estruturas binárias

## Comunicação Inter-Processos (Rede)

### HTTP REST API

**Servidor:** miniwebserver na porta 8080

**Endpoints:**

| Método | Path | Descritivo | Resposta |
|--------|------|-----------|----------|
| GET | `/estado_local` | Estado completo (para HTML) | JSON |
| GET | `/status` | Info pública (para remotos) | JSON |
| GET | `/tiro?linha=X&coluna=Y` | Dispara tiro | JSON |
| GET | `/index.html` | Página de visualização | HTML |
| GET | `/` | Redireciona para /index.html | HTML |

**Exemplo fluxo HTTP:**

```
Cliente:              Servidor (porta 8080):
│                               │
├─ Novo socket TCP              │
│  ├─ Connect                   │
│  │  ├─→ Accept                │
│  │                            │
│  ├─ Enviar HTTP request       │
│  │  ├─→ recv()                │
│  │  ├─→ parse request         │
│  │  ├─→ process               │
│  │  ├─→ build JSON response   │
│  │                            │
│  ├─ Receber HTTP response     │
│  │  ├─← send()                │
│  │                            │
│  └─ Close socket              │
│                          ├─→ Close socket
│                          │
└───────────────────────────────┘
```

## Threading e Concorrência

### Modelo: Single-threaded select()

```c
while (!shutdown_flag) {
    FD_ZERO(&readfds);
    FD_SET(server_socket, &readfds);  // Novo cliente?
    
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    
    int activity = select(
        server_socket + 1,
        &readfds,
        NULL, NULL,
        &tv
    );
    
    if (activity > 0 && FD_ISSET(server_socket, &readfds)) {
        // Novo cliente HTTP
        int client = accept(...);
        // Processar requisição
        // Enviar resposta
        close(client);
    }
    // Verificar FIFO a cada 1 segundo
}
```

**Vantagem:** Simples, sem necessidade de mutexes

**Desvantagem:** Processa uma requisição por vez (OK para projeto pedagógico)

## Sincronização de Estado

### Board State (Crítico)

O `struct Board` é acessado por:
1. **FIFO thread** (leitura de eventos navio)
2. **HTTP threads** (requisições de ataque)

**Proteção:** Neste projeto simples, não há proteção (single-threaded)

**Para produção:** Seria necessário:
- Mutex/locks em torno do acesso a `board`
- Ou usar processo separado com IPC

### Atomicidade de Tiros

Problema: Dois tiros na mesma posição simultaneamente?

```
Tiro 1: GET /tiro?linha=5&coluna=3     Tiro 2: GET /tiro?linha=5&coluna=3
│                                       │
├─ board_is_hit(5,3) → false            │
│                                       │
│                          ┌─→ board_is_hit(5,3) → false
│                          │
├─ Criar AttackRecord      │
├─ Retornar "água"         │
│                          │
│                    ┌─────┴─ Criar AttackRecord
│                    │        Retornar "água"
│                    │
│              PROBLEMA: Ambos viram "água"
│              quando deveriam ser um único resultado
```

**Solução no projeto:** Aceitável ter pequenas race conditions em contexto educacional

**Solução real:** Usar mutex ou transações

## Estratégia de Ataque

### attacker.c

```
main():
  ├─ Ler config/teams.txt
  │
  └─ Para cada tiro (até 100):
     ├─ Escolher equipe aleatória
     ├─ Escolher posição aleatória (linha, coluna)
     ├─ http_request(equipe, "/tiro?linha=X&coluna=Y")
     ├─ Parse resposta JSON
     ├─ Atualizar estatísticas:
     │  ├─ ships_hit++
     │  ├─ ships_destroyed (aqui não faz tracking)
     │  └─ score += pontos
     │
     └─ Aguardar 1 segundo
  
  Gerar relatório final
```

**Estratégia:** Aleatória (simples para projeto pedagógico)

**Melhorias possíveis:**
- Inteligente: atacar linhas conhecidas (via /status)
- Adaptativa: se acerto, atacar vizinhos
- Distribuída: múltiplos processos fork() atacando

## Performance e Escalabilidade

### Limitações Atuais

| Aspecto | Limite | Motivo |
|---------|--------|--------|
| Navios | 6 | Hardcoded |
| Tabuleiro | 10x10 | Hardcoded |
| Clientes HTTP simultâneos | 1 | select() sequencial |
| Tiros simultâneos | 100 | Maxshots |
| Equipes remotas | Ilimitado | HTTP (mas uma por vez) |

### Gargalos

1. **Single-threaded:** Processa requisição por vez
2. **Sem cache:** Recalcula JSON a cada requisição
3. **FIFO bloqueante:** Se navio não escreve, servidor falha?

### Otimizações Possíveis

1. **Multi-threaded:** pthread_create() para cada cliente
2. **Buffer circular:** Manter última resposta em cache
3. **Epoll/Kqueue:** Suporte a mais I/O multiplexing
4. **Message queue:** Substituir FIFO por fila mais robusta

## Fluxo de Erro

### Erro: Navio offline

```
Servidor aguardando evento de navio_id=100
│
├─ Navio morre (kill -9)
│
└─ Servidor continua respondendo HTTP!
   (Navio 100 fica com última posição conhecida)
```

**Solução:** Implementar heartbeat + timeout

### Erro: Equipe remota offline

```
Client: Http GET /tiro?... (contra 10.0.0.5:8080)
│
└─ 10.0.0.5 offline
   │
   └─ timeout 5 segundos
      └─ Retorna erro: "servidor offline"
```

**Implementado:** timeout em http_request()

## Sequência Temporal Exemplo

```
T=0:     Servidor inicia
         ├─ Board: 6 navios em posições iniciais
         └─ Escutando na porta 8080

T=5:     Navio 100 se move: (1,0) → (1,1)
         └─ Escreve ShipEvent na FIFO

T=5:     Servidor lê evento:
         └─ board.ships[0].col = 1

T=10:    Cliente 1 ataca (3,5):
         ├─ GET /tiro?linha=3&coluna=5
         ├─ Sem navio lá
         └─ Responde: {"resultado":"agua"}

T=10:    Navio 200 se move: (3,0) → (3,1)
         └─ Escreve ShipEvent

T=15:    Cliente 2 ataca (3,1):
         ├─ GET /tiro?linha=3&coluna=1
         ├─ Navio 200 presente!
         ├─ Mata navio: ships.alive = 0
         └─ Responde: {"resultado":"acerto", "tipo":"submarino", "pontos":3}

T=20:    Cliente 1 ataca (3,5) novamente:
         ├─ GET /tiro?linha=3&coluna=5
         ├─ Já foi atacado!
         └─ Responde: {"resultado":"repetido"}

T=25:    GET /status (atacante remoto):
         ├─ Verifica navios vivos
         ├─ Submarino está morto
         └─ Responde sem submarino na lista
```

## Conclusão

A arquitetura é **simples, educacional e funcional**. Demonstra:

✅ Processos e fork()  
✅ IPC via FIFO  
✅ Servidor HTTP  
✅ Sincronização básica  
✅ Estado distribuído  
✅ JSON para APIs  

Adequada para aprender SO sem complexidade desnecessária.

---

**Versão:** 1.0  
**Data:** 17 de março de 2026
