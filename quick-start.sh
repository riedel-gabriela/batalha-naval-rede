#!/bin/bash

# Script quick-start para Batalha Naval em Rede

set -e

PROJECT_DIR="/workspaces/batalha-naval-rede"
cd "$PROJECT_DIR"

if [ $? -ne 0 ]; then
  echo "Erro: diretório $PROJECT_DIR não encontrado. Execute em /workspaces/batalha-naval-rede ou ajuste PROJECT_DIR."
  exit 1
fi

echo "╔════════════════════════════════════════════════════════════╗"
echo "║   Batalha Naval em Rede - Quick Start                    ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

# Compilar
echo "[1] Compilando projeto..."
make clean > /dev/null 2>&1
make all > /dev/null 2>&1
echo "✓ Compilado com sucesso"
echo ""

# Menu
echo "Escolha uma opção:"
echo "  1) Iniciar servidor web"
echo "  2) Iniciar servidor + navios (em pano de fundo)"
echo "  3) Executar teste com curl"
echo "  4) Executar atacante remoto"
echo "  5) Ver ajuda"
echo "  6) Sair"
echo ""

read -p "Opção [1-6]: " OPTION

case $OPTION in
    1)
        echo ""
        echo "Iniciando servidor web na porta 8080..."
        echo "(Pressione Ctrl+C para parar)"
        echo ""
        ./bin/miniwebserver
        ;;
    2)
        echo ""
        echo "Iniciando servidor + navios..."
        echo ""
        
        # Iniciar servidor em background
        echo "[*] Servidor iniciando em background..."
        ./bin/miniwebserver &
        SERVER_PID=$!
        sleep 2
        
        echo "[*] Inicializando navios..."
        ./bin/navio 100 5 1 &
        NAVIO1_PID=$!
        ./bin/navio 200 3 3 &
        NAVIO2_PID=$!
        ./bin/navio 201 3 5 &
        NAVIO3_PID=$!
        ./bin/navio 300 2 2 &
        NAVIO4_PID=$!
        ./bin/navio 301 2 4 &
        NAVIO5_PID=$!
        ./bin/navio 302 2 6 &
        NAVIO6_PID=$!
        
        echo ""
        echo "✓ Sistema iniciado!"
        echo ""
        echo "  PIDs:"
        echo "    Servidor: $SERVER_PID"
        echo "    Navios: $NAVIO1_PID $NAVIO2_PID $NAVIO3_PID $NAVIO4_PID $NAVIO5_PID $NAVIO6_PID"
        echo ""
        echo "  Acesse: http://127.0.0.1:8080"
        echo "  Pressione Ctrl+C para parar todos os processos"
        echo ""
        
        # Aguardar Ctrl+C
        trap "kill $SERVER_PID $NAVIO1_PID $NAVIO2_PID $NAVIO3_PID $NAVIO4_PID $NAVIO5_PID $NAVIO6_PID 2>/dev/null; echo; echo 'Sistema encerrado.'; exit 0" INT
        wait
        ;;
    3)
        echo ""
        echo "Executando testes com curl..."
        bash test.sh
        ;;
    4)
        echo ""
        echo "Executando processo atacante..."
        echo "(Máximo 100 tiros)"
        echo ""
        ./bin/attacker
        ;;
    5)
        echo ""
        echo "Ajuda - Comandos disponíveis:"
        echo ""
        echo "  make all                 - Compilar projeto"
        echo "  make clean               - Limpar compilados"
        echo "  make run-server          - Executar servidor"
        echo "  make run-navio           - Executar navio (teste)"
        echo "  make run-attacker        - Executar atacante"
        echo "  make test-local          - Testes com curl"
        echo ""
        echo "  ./test.sh                - Script de teste"
        echo "  ./quick-start.sh         - Este script"
        echo ""
        echo "Arquivo de configuração:"
        echo "  config/teams.txt         - Lista de IPs das equipes"
        echo ""
        echo "Documentação:"
        echo "  README.md                - Documentação completa"
        echo ""
        ;;
    6)
        echo "Até logo!"
        exit 0
        ;;
    *)
        echo "Opção inválida!"
        exit 1
        ;;
esac
