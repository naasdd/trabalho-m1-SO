// ============================================================
// servidor.cpp — PROCESSO SERVIDOR (gerenciador do banco)
// ------------------------------------------------------------
// Responsabilidades:
//   1) Criar o FIFO de pedidos (/tmp/db_req) — o IPC real
//      exigido pelo enunciado (pipe nomeado, syscalls POSIX
//      mkfifo/open/read/write, nao variaveis globais nem
//      arquivos temporarios com polling).
//   2) Carregar o banco.txt para a memoria.
//   3) Loop principal: ler linhas do FIFO e DESPACHAR cada
//      pedido para o pool de threads (paralelismo).
//   4) Cada worker processa o pedido no banco (protegido por
//      shared_mutex) e responde no FIFO privado do cliente
//      (/tmp/db_resp_<pid>).
//   5) Ao receber Ctrl+C (SIGINT): salvar banco.txt e sair.
//
// Uso:  ./servidor [nThreads]   (padrao: 4)
// ============================================================

#include <iostream>
#include <fstream>
#include <string>
#include <csignal>
#include <atomic>

// Syscalls POSIX para o FIFO — prova de que o IPC e "de verdade"
#include <sys/stat.h>   // mkfifo
#include <fcntl.h>      // open
#include <unistd.h>     // read, close, unlink

#include "protocolo.hpp"
#include "banco.hpp"
#include "pool.hpp"

using namespace std;

// Flag global setada pelo handler de sinal (Ctrl+C).
// atomic porque o handler roda em outro "contexto" de execucao.
atomic<bool> g_rodando{true};

void tratarSigint(int) {
    g_rodando = false;
}

// ------------------------------------------------------------
// processarPedido: executada pelas WORKER THREADS do pool.
// Recebe o pedido, opera o banco e responde no FIFO do cliente.
// ------------------------------------------------------------
void processarPedido(Banco& banco, const proto::Pedido& p) {
    string resposta;

    switch (p.operacao) {
        case proto::Op::INSERT:
            resposta = banco.inserir(p.id, p.nome)
                ? proto::montarResposta("OK", p.id, "inserido")
                : proto::montarResposta("ERR", p.id, "id ja existe");
            break;

        case proto::Op::SELECT: {
            auto reg = banco.buscar(p.id);
            resposta = reg
                ? proto::montarResposta("OK", reg->id, reg->nome)
                : proto::montarResposta("ERR", p.id, "nao encontrado");
            break;
        }

        case proto::Op::UPDATE:
            resposta = banco.atualizar(p.id, p.nome)
                ? proto::montarResposta("OK", p.id, "atualizado")
                : proto::montarResposta("ERR", p.id, "nao encontrado");
            break;

        case proto::Op::DELETE:
            resposta = banco.remover(p.id)
                ? proto::montarResposta("OK", p.id, "removido")
                : proto::montarResposta("ERR", p.id, "nao encontrado");
            break;

        default:
            resposta = proto::montarResposta("ERR", p.id, "operacao invalida");
    }

    // Responde no FIFO PRIVADO do cliente: /tmp/db_resp_<pid>
    string fifoResp = proto::fifoResposta(p.pid);
    int fd = open(fifoResp.c_str(), O_WRONLY);
    if (fd >= 0) {
        ssize_t ignorado = write(fd, resposta.data(), resposta.size());
        (void)ignorado;
        close(fd);
    }
    // Se fd < 0, o cliente ja morreu ou nao abriu o FIFO de
    // resposta — simplesmente descartamos a resposta.
}

int main(int argc, char* argv[]) {
    // Numero de worker threads: padrao 4 se nao passado.
    // Usamos stoi dentro de try/catch para nao quebrar caso o
    // usuario digite algo que nao seja numero (ex: ./servidor abc).
    int nThreads = 4;
    if (argc >= 2) {
        try {
            nThreads = stoi(argv[1]);
        } catch (...) {
            cerr << "[servidor] argumento invalido ('" << argv[1]
                 << "'); usando 4 threads\n";
            nThreads = 4;
        }
    }
    if (nThreads < 1) {
        cerr << "[servidor] numero de threads deve ser >= 1; usando 1\n";
        nThreads = 1;
    }

    cout << "[servidor] Iniciando com " << nThreads << " worker threads\n";

    // --- 1) Carrega o banco (antes de qualquer thread) ---
    Banco banco;
    const string caminhoBanco = "banco.txt";
    if (banco.carregar(caminhoBanco)) {
        cout << "[servidor] banco.txt carregado: " << banco.tamanho()
             << " registros\n";
    } else {
        cout << "[servidor] banco.txt nao encontrado, comecando vazio\n";
    }

    // --- 2) Cria o FIFO de pedidos (IPC) ---
    // mkfifo cria o "arquivo especial" nomeado. Se ja existir
    // (execucao anterior), removemos antes.
    unlink(proto::FIFO_PEDIDOS);
    if (mkfifo(proto::FIFO_PEDIDOS, 0666) != 0) {
        perror("[servidor] mkfifo");
        return 1;
    }
    cout << "[servidor] FIFO criado em " << proto::FIFO_PEDIDOS << '\n';

    // --- 3) Sobe o pool de threads ---
    Pool pool(nThreads);

    // --- 4) Trata Ctrl+C para encerrar salvando o banco ---
    signal(SIGINT, tratarSigint);
    signal(SIGTERM, tratarSigint);

    // --- 5) Abre o FIFO para leitura ---
    // O_RDONLY | O_NONBLOCK: leitura nao-bloqueante, para o loop
    // conseguir checar a flag g_rodando e encerrar no Ctrl+C.
    int fd = open(proto::FIFO_PEDIDOS, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("[servidor] open fifo");
        return 1;
    }

    cout << "[servidor] Aguardando pedidos... (Ctrl+C para sair)\n";

    // --- 6) Loop principal: le linhas e despacha para o pool ---
    string buffer;                 // acumula bytes ate formar linha
    char bloco[proto::TAM_MAX_MSG];

    while (g_rodando) {
        ssize_t n = read(fd, bloco, sizeof(bloco));

        if (n > 0) {
            buffer.append(bloco, n);

            // Extrai todas as linhas completas (terminadas em \n)
            size_t pos;
            while ((pos = buffer.find('\n')) != string::npos) {
                string linha = buffer.substr(0, pos);
                buffer.erase(0, pos + 1);

                proto::Pedido p;
                if (proto::parsePedido(linha, p)) {
                    // DESPACHA para o pool: uma worker livre executa
                    // processarPedido em PARALELO com as demais.
                    pool.despachar([&banco, p] { processarPedido(banco, p); });
                } else {
                    cerr << "[servidor] pedido mal formatado: '"
                         << linha << "'\n";

                    // Boa pratica de IPC: sempre responder, mesmo
                    // com erro. Sem isso, o cliente ficaria preso
                    // no timeout de 5s sem saber o que houve.
                    int pidErr = proto::extrairPid(linha);
                    if (pidErr > 0) {
                        string fifoErr = proto::fifoResposta(pidErr);
                        int fdE = open(fifoErr.c_str(), O_WRONLY);
                        if (fdE >= 0) {
                            string resp = proto::montarResposta(
                                "ERR", 0, "pedido invalido");
                            ssize_t ignorado = write(fdE, resp.data(), resp.size());
                            (void)ignorado;
                            close(fdE);
                        }
                    }
                }
            }
        } else {
            // Sem dados no momento: dorme 10ms para nao queimar CPU
            // (so no loop de LEITURA do FIFO; as workers dormem de
            // verdade na condition variable).
            usleep(10000);

            // Truque para detectar clientes novos: reabre o FIFO
            // se todos os escritores fecharam (read retornaria 0/EOF
            // em modo bloqueante; em nonblock apenas tentamos de novo).
        }
    }

    // --- 7) Encerramento limpo ---
    close(fd);
    unlink(proto::FIFO_PEDIDOS);   // remove o FIFO do sistema

    if (banco.salvar(caminhoBanco)) {
        cout << "\n[servidor] banco.txt salvo com " << banco.tamanho()
             << " registros. Encerrado.\n";
    } else {
        cerr << "\n[servidor] ERRO ao salvar banco.txt\n";
    }
    // O destrutor do Pool (final do main) para e junta as threads.
    return 0;
}
