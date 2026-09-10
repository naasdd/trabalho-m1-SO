# Compila os dois executaveis em bin/.
#
#   mingw32-make          compila
#   mingw32-make limpar   apaga os binarios
#
# Atribuicao simples em CXX porque o make ja traz CXX definido como "g++"
# apenas em algumas versoes; a linha de comando continua tendo prioridade.

CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -Iinclude
LDLIBS   = -lpthread

.PHONY: all limpar

all: bin/servidor.exe bin/cliente.exe

bin/servidor.exe: src/servidor.cpp src/canal.cpp include/comum.hpp
	$(CXX) $(CXXFLAGS) -o $@ src/servidor.cpp src/canal.cpp $(LDLIBS)

bin/cliente.exe: src/cliente.cpp src/canal.cpp include/comum.hpp
	$(CXX) $(CXXFLAGS) -o $@ src/cliente.cpp src/canal.cpp $(LDLIBS)

limpar:
	-del /Q bin\servidor.exe bin\cliente.exe 2>nul
