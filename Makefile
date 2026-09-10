# Makefile do trabalho M1 de Sistemas Operacionais.
#
# Funciona no Linux (make) e no Windows com MinGW-w64 (mingw32-make). A unica
# diferenca entre os dois e qual implementacao do canal IPC entra na
# compilacao: FIFO nomeado POSIX no Linux, named pipe no Windows.
#
#   make            compila cliente e servidor em bin/
#   make demo       roteiro curto de demonstracao (servidor + script de comandos)
#   make experimento  bateria de medicoes usada no relatorio
#   make limpar     apaga os binarios

# Atribuicao simples em vez de ?=: o make ja traz CC definido como "cc", que
# nao existe no MinGW. A linha de comando continua tendo prioridade
# (make CC=clang).
CC     = gcc
CFLAGS = -std=gnu11 -Wall -Wextra -O2 -Iinclude

# Modulos usados pelos dois executaveis.
COMUM = src/protocolo.c src/relogio.c

# Modulos que so o servidor usa.
SERVIDOR_EXTRA = src/banco.c src/fila.c src/registro_log.c

ifeq ($(OS),Windows_NT)
    CANAL  = src/canal_win.c
    EXE    = .exe
    LDLIBS = -lpthread
    LIMPAR = -del /Q bin\servidor.exe bin\cliente.exe 2>nul
else
    CANAL  = src/canal_posix.c
    EXE    =
    CFLAGS += -pthread
    LDLIBS = -pthread
    LIMPAR = rm -f bin/servidor bin/cliente
endif

SERVIDOR = bin/servidor$(EXE)
CLIENTE  = bin/cliente$(EXE)

.PHONY: all limpar demo experimento ajuda

all: $(SERVIDOR) $(CLIENTE)

$(SERVIDOR): src/servidor.c $(SERVIDOR_EXTRA) $(COMUM) $(CANAL) include/*.h
	$(CC) $(CFLAGS) -o $@ src/servidor.c $(SERVIDOR_EXTRA) $(COMUM) $(CANAL) $(LDLIBS)

$(CLIENTE): src/cliente.c $(COMUM) $(CANAL) include/*.h
	$(CC) $(CFLAGS) -o $@ src/cliente.c $(COMUM) $(CANAL) $(LDLIBS)

limpar:
	$(LIMPAR)

demo: all
	./scripts/demo.sh

experimento: all
	./scripts/experimento.sh

ajuda:
	@echo "make            compila cliente e servidor em bin/"
	@echo "make demo       roteiro curto de demonstracao"
	@echo "make experimento  bateria de medicoes do relatorio"
	@echo "make limpar     apaga os binarios"
