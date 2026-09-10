# Compila os dois executaveis do trabalho.
#
#   mingw32-make          compila servidor.exe e cliente.exe
#   mingw32-make limpar   apaga os executaveis
#
# Atribuicao simples em CXX porque o make traz variaveis padrao proprias; a
# linha de comando continua tendo prioridade (mingw32-make CXX=clang++).

CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
LDLIBS   = -lpthread

.PHONY: all limpar

all: servidor.exe cliente.exe

servidor.exe: servidor.cpp banco.h
	$(CXX) $(CXXFLAGS) -o $@ servidor.cpp $(LDLIBS)

cliente.exe: cliente.cpp banco.h
	$(CXX) $(CXXFLAGS) -o $@ cliente.cpp $(LDLIBS)

limpar:
	-del /Q servidor.exe cliente.exe 2>nul
