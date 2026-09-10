/*
 * cliente.cpp - processo cliente.
 *
 * Executavel separado do servidor, com o seu proprio espaco de enderecos.
 * Toda a conversa acontece por IPC:
 *
 *   1. cria o canal de respostas (fica com a ponta de leitura dele);
 *   2. abre o canal de requisicoes publicado pelo servidor;
 *   3. envia as requisicoes e le as respostas.
 *
 * Ha dois modos de fazer o passo 3, e a diferenca entre eles e justamente o
 * assunto do trabalho:
 *
 *   sequencial (padrao)  envia um comando, espera a resposta, envia o proximo.
 *                        O servidor recebe uma tarefa por vez, entao os
 *                        comandos sao executados na ordem em que foram
 *                        escritos. E o comportamento de um cliente de banco
 *                        comum, e o que torna o roteiro de demonstracao
 *                        previsivel.
 *
 *   --paralelo           envia tudo de uma vez e so depois recolhe as
 *                        respostas. O servidor acumula requisicoes na fila e as
 *                        threads do pool competem por elas, entao as respostas
 *                        voltam fora de ordem, cada uma marcada com a thread
 *                        que a atendeu. E a demonstracao do paralelismo.
 *
 * No modo paralelo nao existe garantia de ordem entre as requisicoes: um
 * UPDATE enviado depois de um INSERT pode ser executado antes dele. Por isso o
 * modo sequencial e o padrao.
 */
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "banco.h"

namespace {

/* Le comandos linha a linha, ignorando linhas vazias e comentarios. */
std::vector<std::string> lerComandos(std::istream &entrada)
{
    std::vector<std::string> comandos;
    std::string linha;

    while (std::getline(entrada, linha)) {
        if (!linha.empty() && linha.back() == '\r')   /* fim de linha do Windows */
            linha.pop_back();
        if (linha.empty() || linha[0] == '#')
            continue;
        comandos.push_back(linha);
    }
    return comandos;
}

/* Monta e envia uma requisicao. */
bool enviar(HANDLE canal, int numero, const std::string &comando)
{
    Mensagem requisicao;
    requisicao.id = numero;
    requisicao.thread = -1;
    std::snprintf(requisicao.texto, MAX_TEXTO, "%s", comando.c_str());

    std::printf("-> #%-3d %s\n", requisicao.id, requisicao.texto);
    if (escreverMensagem(canal, requisicao))
        return true;

    std::fprintf(stderr, "erro ao enviar: %s\n", ultimoErro().c_str());
    return false;
}

/* Le uma resposta e a imprime. */
bool receber(HANDLE canal)
{
    Mensagem resposta;
    if (!lerMensagem(canal, resposta)) {
        std::fprintf(stderr, "erro ao receber: %s\n", ultimoErro().c_str());
        return false;
    }
    resposta.texto[MAX_TEXTO - 1] = '\0';
    std::printf("<- #%-3d [thread %d] %s\n", resposta.id, resposta.thread, resposta.texto);
    return true;
}

}  // namespace

int main(int argc, char **argv)
{
    std::vector<std::string> comandos;
    const char *entrada = nullptr;
    bool paralelo = false;

    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--paralelo")
            paralelo = true;
        else if (entrada == nullptr)
            entrada = argv[i];
    }

    if (entrada != nullptr) {
        std::ifstream arquivo(entrada);
        if (arquivo)
            comandos = lerComandos(arquivo);
        else
            comandos.push_back(entrada);   /* o argumento e o proprio comando */
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
    HANDLE canal_respostas = criarCanal(CANAL_RESPOSTAS);
    if (canal_respostas == nullptr) {
        std::fprintf(stderr, "erro ao criar o canal de respostas: %s\n", ultimoErro().c_str());
        return 1;
    }

    HANDLE canal_requisicoes = abrirCanal(CANAL_REQUISICOES, 5000);
    if (canal_requisicoes == nullptr) {
        std::fprintf(stderr, "erro: o servidor nao esta no ar (%s)\n", ultimoErro().c_str());
        return 1;
    }

    if (!aguardarConexao(canal_respostas)) {
        std::fprintf(stderr, "erro ao conectar o canal de respostas: %s\n", ultimoErro().c_str());
        return 1;
    }

    if (paralelo) {
        /* Envia tudo antes de ler: o servidor acumula requisicoes na fila e as
         * threads do pool as processam ao mesmo tempo. */
        for (std::size_t i = 0; i < comandos.size(); ++i) {
            if (!enviar(canal_requisicoes, static_cast<int>(i) + 1, comandos[i]))
                return 1;
        }
        /* As respostas voltam na ordem em que as threads terminarem. */
        std::printf("\n");
        for (std::size_t i = 0; i < comandos.size(); ++i) {
            if (!receber(canal_respostas))
                return 1;
        }
    } else {
        /* Um de cada vez: a resposta chega antes de o proximo comando sair. */
        for (std::size_t i = 0; i < comandos.size(); ++i) {
            if (!enviar(canal_requisicoes, static_cast<int>(i) + 1, comandos[i]))
                return 1;
            if (!receber(canal_respostas))
                return 1;
        }
    }

    fecharCanal(canal_requisicoes);
    fecharCanal(canal_respostas);
    return 0;
}
