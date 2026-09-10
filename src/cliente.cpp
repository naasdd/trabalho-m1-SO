/*
 * cliente.cpp - processo cliente.
 *
 * Executavel separado do servidor, com o seu proprio espaco de enderecos.
 * Toda a conversa acontece por IPC:
 *
 *   1. cria o canal de respostas (fica com a ponta de leitura dele);
 *   2. abre o canal de requisicoes publicado pelo servidor;
 *   3. envia todas as requisicoes;
 *   4. le todas as respostas.
 *
 * Enviar tudo antes de ler e o que da trabalho simultaneo ao pool: o servidor
 * fica com varias requisicoes na fila e as threads as processam em paralelo.
 * Por isso as respostas voltam fora de ordem - cada uma termina quando a sua
 * thread termina -, e cada resposta traz o numero da requisicao e o indice da
 * thread que a atendeu.
 */
#include "comum.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

/* Le os comandos de um arquivo, ignorando linhas vazias e comentarios. */
std::vector<std::string> lerComandos(std::istream &entrada)
{
    std::vector<std::string> comandos;
    std::string linha;

    while (std::getline(entrada, linha)) {
        if (!linha.empty() && linha.back() == '\r')   /* arquivos com fim de linha do Windows */
            linha.pop_back();
        if (linha.empty() || linha[0] == '#')
            continue;
        comandos.push_back(linha);
    }
    return comandos;
}

}  // namespace

int main(int argc, char **argv)
{
    std::vector<std::string> comandos;

    if (argc > 1) {
        std::ifstream arquivo(argv[1]);
        if (arquivo) {
            comandos = lerComandos(arquivo);
        } else {
            comandos.push_back(argv[1]);   /* o argumento e o proprio comando */
        }
    } else {
        std::printf("Digite os comandos (uma linha cada) e termine com Ctrl+Z e Enter:\n");
        comandos = lerComandos(std::cin);
    }

    if (comandos.empty()) {
        std::fprintf(stderr, "nenhum comando para enviar\n");
        return 1;
    }

    /* O canal de respostas precisa existir antes de o servidor tentar abri-lo,
     * e ele faz isso assim que aceita a conexao das requisicoes. */
    void *canal_respostas = canal::criar(CANAL_RESPOSTAS);
    if (canal_respostas == nullptr) {
        std::fprintf(stderr, "erro ao criar o canal de respostas: %s\n",
                     canal::ultimoErro().c_str());
        return 1;
    }

    void *canal_requisicoes = canal::abrir(CANAL_REQUISICOES, 5000);
    if (canal_requisicoes == nullptr) {
        std::fprintf(stderr, "erro: o servidor nao esta no ar (%s)\n",
                     canal::ultimoErro().c_str());
        return 1;
    }

    if (!canal::aguardarConexao(canal_respostas)) {
        std::fprintf(stderr, "erro ao conectar o canal de respostas: %s\n",
                     canal::ultimoErro().c_str());
        return 1;
    }

    /* Envia tudo primeiro. */
    for (std::size_t i = 0; i < comandos.size(); ++i) {
        Mensagem requisicao;
        requisicao.id = static_cast<int>(i) + 1;
        requisicao.thread = -1;
        std::snprintf(requisicao.texto, MAX_TEXTO, "%s", comandos[i].c_str());

        std::printf("-> #%d %s\n", requisicao.id, requisicao.texto);
        if (!canal::escrever(canal_requisicoes, requisicao)) {
            std::fprintf(stderr, "erro ao enviar: %s\n", canal::ultimoErro().c_str());
            return 1;
        }
    }

    /* Depois recolhe as respostas, na ordem em que as threads terminarem. */
    std::printf("\n");
    for (std::size_t i = 0; i < comandos.size(); ++i) {
        Mensagem resposta;
        if (!canal::ler(canal_respostas, resposta)) {
            std::fprintf(stderr, "erro ao receber: %s\n", canal::ultimoErro().c_str());
            return 1;
        }
        resposta.texto[MAX_TEXTO - 1] = '\0';
        std::printf("<- #%-3d [thread %d] %s\n", resposta.id, resposta.thread, resposta.texto);
    }

    canal::fechar(canal_requisicoes);
    canal::fechar(canal_respostas);
    return 0;
}
