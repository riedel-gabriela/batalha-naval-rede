CC = gcc
CFLAGS = -Wall -Wextra -g -I./include
LDFLAGS = -lm

SRCDIR = src
BINDIR = bin
INCDIR = include

# Objetos
OBJS = $(BINDIR)/board.o $(BINDIR)/miniwebserver.o
NAVIO_OBJ = $(BINDIR)/navio.o
ATTACKER_OBJ = $(BINDIR)/attacker.o

TARGETS = $(BINDIR)/miniwebserver $(BINDIR)/navio $(BINDIR)/attacker

all: $(TARGETS)

$(BINDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BINDIR)/miniwebserver: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

$(BINDIR)/navio: $(NAVIO_OBJ) $(BINDIR)/board.o
	$(CC) $(CFLAGS) $(NAVIO_OBJ) $(BINDIR)/board.o -o $@ $(LDFLAGS)

$(BINDIR)/attacker: $(ATTACKER_OBJ)
	$(CC) $(CFLAGS) $(ATTACKER_OBJ) -o $@ $(LDFLAGS)

clean:
	rm -rf $(BINDIR)

run-server: all
	./$(BINDIR)/miniwebserver

run-navio: all
	./$(BINDIR)/navio 100 5 1

run-attacker: all
	./$(BINDIR)/attacker

test-local:
	@echo "Testando /estado_local..."
	curl http://127.0.0.1:8080/estado_local
	@echo "\n\nTestando /status..."
	curl http://127.0.0.1:8080/status
	@echo "\n\nTestando /tiro..."
	curl "http://127.0.0.1:8080/tiro?linha=3&coluna=5"

help:
	@echo "Alvos disponíveis:"
	@echo "  make all         - Compilar projeto"
	@echo "  make clean       - Limpar arquivos compilados"
	@echo "  make run-server  - Executar servidor web"
	@echo "  make run-navio   - Executar processo navio (teste)"
	@echo "  make test-local  - Testar endpoints com curl"

.PHONY: all clean run-server run-navio test-local help
