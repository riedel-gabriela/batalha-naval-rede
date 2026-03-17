# Batalha Naval em Rede - Projeto Pedagógico

Um projeto educacional implementando um jogo de Batalha Naval distribuído em C, focando em conceitos de Sistemas Operacionais como processos, IPC, concorrência e comunicação em rede.

## Estrutura do Projeto

```
batalha-naval-rede/
├── src/                    # Código fonte C
│   ├── miniwebserver.c    # Servidor web central
│   ├── navio.c            # Processo navio (fork)
│   ├── attacker.c         # Processo atacante remoto
│   └── board.c            # Lógica do tabuleiro
├── include/               # Headers
│   ├── config.h          # Configurações
│   ├── ship.h            # Estruturas de navio
│   └── board.h           # Estruturas do tabuleiro
├── html/                  # Interface web
│   └── index.html        # Página de visualização
├── config/               # Arquivos de configuração
│   └── teams.txt         # Lista de IPs das equipes
├── bin/                  # Executáveis compilados
├── Makefile              # Build system
└── README.md             # Este arquivo
```

## Componentes

### 1. Miniwebserver (miniwebserver.c)
- Servidor HTTP escutando na porta 8080
- Gerencia o tabuleiro local
- Implementa endpoints:
  - `GET /estado_local` - Estado completo do tabuleiro (para HTML)
  - `GET /status` - Informações públicas (para outras equipes)
  - `GET /tiro?linha=X&coluna=Y` - Processa tiros recebidos
  - `GET /` ou `/index.html` - Serve página de visualização

**Responsabilidades:**
- Criar FIFO para comunicação com navios
- Inicializar processos navio via fork()
- Manter estado atual do tabuleiro
- Arbitrar tiros recebidos
- Atualizar estado de navios

### 2. Processos Navio (navio.c)
- Processo independente criado via fork()
- Simula movimento de navio periodicamente
- Envia posição atual via FIFO
- **Frota padrão:**
  - 1 porta-aviões (5 pontos)
  - 2 submarinos (3 pontos cada)
  - 3 fragatas (2 pontos cada)

**Comportamento:**
- Permanece 5 segundos em cada coluna
- Move-se da esquerda para a direita
- Cicla ao atingir o final da linha
- Comunica via struct ShipEvent

### 3. Página HTML (index.html)
- Interface de visualização em tempo real
- Atualiza tabuleiro a cada 3 segundos (configurável)
- Exibe navios ativos e pontos sofridos
- Usa fetch() para consultar API

### 4. Processo Atacante (attacker.c)
- Lê lista de IPs do arquivo `config/teams.txt`
- Dispara até 100 tiros contra equipes remotas
- Utiliza endpoints HTTP das equipes
- Gera relatório final com:
  - Navios afundados por equipe
  - Pontuação total
  - Taxa de acerto

## Compilação

```bash
# Compilar tudo
make all

# Compilar apenas miniwebserver
make miniwebserver

# Compilar apenas navio
make navio

# Compilar apenas atacante
make attacker

# Limpar compilados
make clean
```

### Requisitos
- GCC (ou compatível)
- Linux/Unix
- Bibliotecas padrão C

## Execução

### Iniciar Servidor Web

```bash
make run-server
# ou
./bin/miniwebserver
```

O servidor:
1. Cria FIFO em `/tmp/batalha_naval_fifo`
2. Inicializa FIFO escuta para eventos
3. Inicia processos navio (fork)
4. Abre socket na porta 8080
5. Aguarda requisições HTTP

**Saída esperada:**
```
Tabuleiro inicializado com 6 navios
Servidor HTTP iniciado na porta 8080
```

### Iniciar Navio (Teste)

Em outro terminal:
```bash
make run-navio
# ou
./bin/navio 100 5 1
```

Formato: `navio <id> <tipo> <linha>`
- `id`: Identificador único (100-300)
- `tipo`: Tipo de navio (2=fragata, 3=submarino, 5=porta-aviões)
- `linha`: Linha no tabuleiro (0-9)

### Iniciar Atacante

```bash
make run-attacker
# ou
./bin/attacker
```

Lê `config/teams.txt` e dispara tiros.

## Testes com curl

### Testar todos os endpoints

```bash
make test-local
```

### Teste individual: Estado Local

```bash
curl http://127.0.0.1:8080/estado_local
```

Resposta esperada:
```json
{
  "tabuleiro": {
    "linhas": 10,
    "colunas": 10
  },
  "navios_ativos": 6,
  "score_contra": 0
}
```

### Teste individual: Status Público

```bash
curl http://127.0.0.1:8080/status
```

Resposta esperada:
```json
{
  "linhas": [
    {"linha": 1, "tipo": "porta_avioes"},
    {"linha": 3, "tipo": "submarino"},
    {"linha": 5, "tipo": "submarino"},
    {"linha": 2, "tipo": "fragata"},
    {"linha": 4, "tipo": "fragata"},
    {"linha": 6, "tipo": "fragata"}
  ],
  "quantidade": {
    "porta_avioes": 1,
    "submarinos": 2,
    "fragatas": 3
  }
}
```

### Teste individual: Disparar Tiro

```bash
# Tiro na linha 3, coluna 5
curl "http://127.0.0.1:8080/tiro?linha=3&coluna=5"
```

Respostas possíveis:

```json
{"resultado":"agua"}
{"resultado":"repetido"}
{"resultado":"acerto","tipo":"fragata","pontos":2}
{"resultado":"acerto","tipo":"submarino","pontos":3}
{"resultado":"acerto","tipo":"porta_avioes","pontos":5}
```

### Teste remoto (entre equipes)

Substituir 127.0.0.1 pelo IP da equipe alvo:

```bash
curl "http://10.0.0.12:8080/tiro?linha=3&coluna=5"
curl http://10.0.0.12:8080/status
```

## Acessar Interface Web

Abrir navegador em:
```
http://127.0.0.1:8080
```

## Configuração de Equipes Remotas

Editar `config/teams.txt`:

```
# Lista de IPs das equipes participantes
127.0.0.1:8080
192.168.1.10:8080
192.168.1.11:8080
10.0.0.5:8080
```

O processo atacante usará essa lista.

## Conceitos de SO Abordados

- ✅ **Processos**: Criação via fork(), gerenciamento de PIDs
- ✅ **IPC**: Comunicação via FIFO (named pipes)
- ✅ **Concorrência**: Múltiplos navios e requisições simultâneas
- ✅ **Sincronização**: Proteção de estado compartilhado
- ✅ **Rede**: HTTP, sockets, cliente-servidor
- ✅ **Persistência**: Manutenção de estado em estruturas de dados
- ✅ **Sinalização**: Tratamento de SIGINT, SIGTERM

## Arquitetura de Comunicação

```
┌──────────────────┐
│  Processos Navio │
│   (fork)         │
└────────┬─────────┘
         │ ShipEvent (via FIFO)
         │
         ↓
    ┌─────────────────────────────┐
    │  Miniwebserver Central      │
    │  - Mantém estado do board   │
    │  - Arbitra tiros            │
    │  - Gerencia navios          │
    └──────────────┬──────────────┘
                   │
        ┌──────────┼──────────┐
        ↓          ↓          ↓
    ┌────────┐ ┌─────────┐ ┌──────────────┐
    │ HTML   │ │Equipes  │ │Processo      │
    │Browser │ │Remotas  │ │Atacante      │
    │        │ │(HTTP)   │ │(HTTP Client) │
    └────────┘ └─────────┘ └──────────────┘
```

## Diagrama de Fluxo: Tiro Recebido

```
HTTP GET /tiro?linha=X&coluna=Y
        │
        ↓
   miniwebserver recebe
        │
        ├─ Posição fora do tabuleiro? → ERRO
        │
        ├─ Posição já foi atacada? → REPETIDO
        │
        └─ Verificar se há navio
                │
                ├─ Sim → ACERTO
                │        ├─ Enviar tipo + pontos
                │        ├─ Destruir navio (kill)
                │        └─ Registrar ataque
                │
                └─ Não → ÁGUA
                         └─ Registrar ataque
```

## Cronograma de Desenvolvimento

Recomendado:

1. **Fase 1**: Estrutura base + Makefile ✅
2. **Fase 2**: Miniwebserver + endpoints básicos ✅
3. **Fase 3**: Processos navio + FIFO ✅
4. **Fase 4**: Página HTML + JavaScript ✅
5. **Fase 5**: Processo atacante ✅
6. **Fase 6**: Testes + Integração
7. **Fase 7**: Documentação + Relatório

## Troubleshooting

### Erro: "Address already in use"
A porta 8080 já está em uso. Aguarde alguns segundos ou:
```bash
# Encontrar processo na porta 8080
lsof -i :8080

# Matar processo
kill -9 <PID>
```

### Erro: "mkfifo: File exists"
FIFO já existe. Remover manualmente:
```bash
rm /tmp/batalha_naval_fifo
```

### HTML não carrega
Certificar que `html/index.html` existe e que o servidor está rodando.

### Navios não aparecem no status
Verificar se os processos navio estão rodando:
```bash
ps aux | grep navio
```

## Melhorias Futuras

- [ ] Validação de entrada robusta
- [ ] Logging em arquivo
- [ ] WebSocket para atualizações em tempo real
- [ ] Persistência de estado em banco de dados
- [ ] Interface web mais completa
- [ ] Modo multiplayer com sincronização
- [ ] Estratégias de IA para ataque

## Documentação Obrigatória

Para o relatório final, incluir:

1. **Diagrama da arquitetura** adotada
2. **Mecanismo de IPC** (FIFO: formato, protocolo)
3. **Estruturas de dados** (navios, ataques, tabuleiro)
4. **Formato de dados interno** (ShipEvent via FIFO)
5. **Endpoints HTTP implementados** e exemplos
6. **Estratégia de ataque** (algoritmo + justificativa)
7. **Dificuldades encontradas** e soluções
8. **Decisões de projeto** e alternativas consideradas

## Licença

Projeto educacional - Uso livre para fins pedagógicos.

## Contato / Dúvidas

Documentar no relatório as dificuldades e soluções encontradas.

---

**Versão**: 1.0  
**Última atualização**: 17 de março de 2026  
**Status**: Pronto para implementação
