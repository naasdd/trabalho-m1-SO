# ============================================================
# Makefile — Trabalho M1 (IPC, Threads e Paralelismo)
# ------------------------------------------------------------
# Como usar:
#   make          -> compila tudo (gera ./servidor e ./cliente)
#   make run-servidor  -> roda o servidor com 4 threads
#   make run-cliente   -> roda o cliente interativo
#   make clean    -> apaga os binarios e arquivos objeto
#
# Flags relevantes:
#   -std=c++17     -> necessario para std::shared_mutex e
#                     std::optional
#   -pthread       -> habilita suporte a threads (std::thread
#                     usa pthreads por baixo no macOS/Linux)
#   -Wall -Wextra  -> mostra todos os avisos (boa pratica)
# ============================================================

CXX      = c++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread

all: servidor cliente

servidor: src/servidor.o src/banco.o src/pool.o
	$(CXX) $(CXXFLAGS) -o $@ $^

cliente: src/cliente.o
	$(CXX) $(CXXFLAGS) -o $@ $^

src/%.o: src/%.cpp src/banco.hpp src/pool.hpp src/protocolo.hpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

run-servidor: servidor
	./servidor 4

run-cliente: cliente
	./cliente

clean:
	rm -f servidor cliente src/*.o

.PHONY: all run-servidor run-cliente clean
