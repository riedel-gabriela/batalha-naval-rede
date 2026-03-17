#!/bin/bash

# Script de teste do projeto Batalha Naval em Rede

echo "╔════════════════════════════════════════════════════════════╗"
echo "║   TESTE - Batalha Naval em Rede                          ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

# Cores
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Verificar se servidor está rodando
echo -e "${BLUE}[1/5] Verificando se servidor está rodando...${NC}"
if ! nc -z 127.0.0.1 8080 2>/dev/null; then
    echo -e "${YELLOW}⚠ Servidor não está respondendo na porta 8080${NC}"
    echo -e "${YELLOW}Inicie com: make run-server${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Servidor respondendo${NC}"

# Teste 1: /estado_local
echo ""
echo -e "${BLUE}[2/5] Testando /estado_local...${NC}"
RESPONSE=$(curl -s http://127.0.0.1:8080/estado_local)
if echo "$RESPONSE" | grep -q "navios_ativos"; then
    echo -e "${GREEN}✓ Endpoint /estado_local respondendo corretamente${NC}"
    echo -e "${YELLOW}Resposta:${NC}"
    echo "$RESPONSE" | python3 -m json.tool 2>/dev/null || echo "$RESPONSE"
else
    echo -e "${RED}✗ Erro no endpoint /estado_local${NC}"
    echo "Resposta: $RESPONSE"
fi

# Teste 2: /status
echo ""
echo -e "${BLUE}[3/5] Testando /status...${NC}"
RESPONSE=$(curl -s http://127.0.0.1:8080/status)
if echo "$RESPONSE" | grep -q "linhas"; then
    echo -e "${GREEN}✓ Endpoint /status respondendo corretamente${NC}"
    echo -e "${YELLOW}Resposta:${NC}"
    echo "$RESPONSE" | python3 -m json.tool 2>/dev/null || echo "$RESPONSE"
else
    echo -e "${RED}✗ Erro no endpoint /status${NC}"
    echo "Resposta: $RESPONSE"
fi

# Teste 3: /tiro
echo ""
echo -e "${BLUE}[4/5] Testando /tiro?linha=3&coluna=5...${NC}"
RESPONSE=$(curl -s "http://127.0.0.1:8080/tiro?linha=3&coluna=5")
if echo "$RESPONSE" | grep -q "resultado"; then
    echo -e "${GREEN}✓ Endpoint /tiro respondendo corretamente${NC}"
    echo -e "${YELLOW}Resposta:${NC}"
    echo "$RESPONSE" | python3 -m json.tool 2>/dev/null || echo "$RESPONSE"
else
    echo -e "${RED}✗ Erro no endpoint /tiro${NC}"
    echo "Resposta: $RESPONSE"
fi

# Teste 4: Página HTML
echo ""
echo -e "${BLUE}[5/5] Testando /index.html...${NC}"
STATUS=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8080/index.html)
if [ "$STATUS" = "200" ]; then
    echo -e "${GREEN}✓ Página HTML acessível${NC}"
    echo -e "${YELLOW}Abra em seu navegador: http://127.0.0.1:8080${NC}"
else
    echo -e "${YELLOW}⚠ Página HTML retornou código: $STATUS${NC}"
fi

echo ""
echo -e "${GREEN}╔════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║   Testes Concluídos                                      ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo "Próximos passos:"
echo "  1. Abra http://127.0.0.1:8080 no navegador"
echo "  2. Em outro terminal, execute: make run-attacker"
echo "  3. Monitore os ataques e o comportamento do servidor"
echo ""
