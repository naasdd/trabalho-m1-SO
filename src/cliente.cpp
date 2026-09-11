// ============================================================
// cliente.cpp — PROCESSO CLIENTE
// ------------------------------------------------------------
// Envia requisicoes ao servidor via FIFO (/tmp/db_req) e le as
// respostas no seu FIFO privado (/tmp/db_resp_<pid>).
//
// Dois modos de uso:
//
//   1) INTERATIVO (padrao): voce digita comandos e ve respostas.
//      ./cliente
//      > INSERT 7 Joao
//      > SELECT 7
//      > UPDATE 7 Maria
//      > DELETE 7
//      > sair
//
//   2) BENCHMARK: dispara N requisicoes iguais e mede o tempo.
//      Serve para gerar os dados do relatorio (tempo vs numero
//      de threads do servidor).
//      ./cliente -n 1000 SELECT 5
//
// Detalhe de implementacao: para esperar a resposta sem busy
// waiting (loop queimando CPU), o cliente usa poll() com
// timeout — assim a espera e "de verdade" (a thread dorme).
// ============================================================

#include <iostream>
#include <string>
#include <sstream>
#include <chrono>
#include <thread>    // thread leitora do modo burst
#include <atomic>    // contadores compartilhados com a leitora

#include <sys/stat.h>   // mkfifo
#include <fcntl.h>      // open
#include <unistd.h>     // read, write, close, unlink, getpid
#include <poll.h>       // poll (espera com timeout, sem busy wait)

#include "protocolo.hpp"

using namespace std;

// ------------------------------------------------------------
// esperarResposta: le UMA linha do FIFO de resposta, com
// timeout de 5 segundos (para o cliente nao travar para sempre
// se o servidor cair).
// ------------------------------------------------------------
bool esperarResposta(int fd, string& linhaOut) {
    string buffer;
    char bloco[proto::TAM_MAX_MSG];

    while (true) {
        pollfd pfd{fd, POLLIN, 0};
        int pronto = poll(&pfd, 1, 5000); // timeout de 5s

        if (pronto <= 0) {
            cerr << "[cliente] timeout esperando resposta do servidor\n";
            return false;
        }

        ssize_t n = read(fd, bloco, sizeof(bloco));
        if (n <= 0) return false;

        buffer.append(bloco, n);
        size_t pos = buffer.find('\n');
        if (pos != string::npos) {
            linhaOut = buffer.substr(0, pos);
            return true;
        }
    }
}

int main(int argc, char* argv[]) {
    int pid = getpid();

    // --- 1) Cria o FIFO privado de resposta deste cliente ---
    string meuFifo = proto::fifoResposta(pid);
    unlink(meuFifo.c_str());
    if (mkfifo(meuFifo.c_str(), 0666) != 0) {
        perror("[cliente] mkfifo");
        return 1;
    }

    // --- 2) Abre o FIFO de pedidos do servidor para escrita ---
    int fdReq = open(proto::FIFO_PEDIDOS, O_WRONLY);
    if (fdReq < 0) {
        perror("[cliente] open fifo de pedidos (o servidor esta rodando?)");
        unlink(meuFifo.c_str());
        return 1;
    }

    // --- 3) Abre nosso FIFO de resposta para leitura ---
    // O_RDONLY | O_NONBLOCK na abertura evita travar aqui caso o
    // servidor ainda nao tenha aberto o outro lado; depois a
    // leitura em si e controlada pelo poll().
    int fdResp = open(meuFifo.c_str(), O_RDONLY | O_NONBLOCK);
    if (fdResp < 0) {
        perror("[cliente] open fifo de resposta");
        close(fdReq);
        unlink(meuFifo.c_str());
        return 1;
    }

    // ================= MODO BENCHMARK "BURST" =================
    // Dispara TODAS as requisicoes sem esperar resposta (enchendo
    // a fila do servidor com trabalho de verdade) e so depois
    // confere as respostas. E o modo que revela o ganho do pool
    // de threads — ideal para o grafico do relatorio.
    //
    // Detalhe de concorrencia: se o cliente escrevesse tudo sem
    // ler, o FIFO de resposta encheria e o servidor travaria no
    // write (DEADLOCK classico de pipe cheio). Para evitar, uma
    // THREAD LEITORA separada vai drenando as respostas enquanto
    // a thread principal escreve. Ou seja: o cliente tambem usa
    // threads — mais um ponto de paralelismo no trabalho.
    //
    //   ./cliente -b 2000 SELECT 5
    if (argc >= 3 && string(argv[1]) == "-b") {
        int total = stoi(argv[2]);

        ostringstream cmd;
        for (int i = 3; i < argc; i++) cmd << (i > 3 ? " " : "") << argv[i];

        istringstream parser(cmd.str());
        string op; int id = 0; string nome;
        parser >> op >> id;
        getline(parser >> ws, nome);

        string pedido = to_string(pid) + "|" + op + "|" +
                        to_string(id) + "|" + nome + "\n";

        // Contadores compartilhados entre a thread leitora e a main.
        // atomic: cada thread atualiza sem precisar de mutex (inteiros
        // sao operacoes atomicas simples; o atomic garante visibilidade).
        atomic<int> ok{0}, falhas{0}, recebidas{0};

        // THREAD LEITORA: fica drenando o FIFO de resposta ate
        // receber 'total' linhas. Sem ela, o FIFO encheria e o
        // servidor travaria (pipe cheio = escritor bloqueado).
        thread leitora([&] {
            string buffer;
            char bloco[proto::TAM_MAX_MSG];
            while (recebidas < total) {
                pollfd pfd{fdResp, POLLIN, 0};
                if (poll(&pfd, 1, 10000) <= 0) break; // timeout 10s
                ssize_t n = read(fdResp, bloco, sizeof(bloco));
                if (n <= 0) continue;
                buffer.append(bloco, n);
                size_t pos;
                while ((pos = buffer.find('\n')) != string::npos) {
                    string linha = buffer.substr(0, pos);
                    buffer.erase(0, pos + 1);
                    if (linha.rfind("OK", 0) == 0) ok++; else falhas++;
                    recebidas++;
                }
            }
        });

        cout << "[cliente] burst: " << total << " x " << cmd.str() << '\n';
        auto inicio = chrono::steady_clock::now();

        // THREAD PRINCIPAL: dispara todas as requisicoes.
        for (int i = 0; i < total; i++) {
            ssize_t ignorado = write(fdReq, pedido.data(), pedido.size());
            (void)ignorado;
        }

        leitora.join(); // espera todas as respostas chegarem
        auto fim = chrono::steady_clock::now();
        double ms = chrono::duration<double, milli>(fim - inicio).count();

        cout << "[cliente] concluido: " << ok << " OK, " << falhas
             << " falhas\n";
        cout << "[cliente] tempo total: " << ms << " ms  ("
             << (total > 0 ? ms / total : 0) << " ms/requisicao)\n";

        close(fdReq);
        close(fdResp);
        unlink(meuFifo.c_str());
        return 0;
    }

    // ================= MODO BENCHMARK SEQUENCIAL =================
    // Ping-pong: envia, espera resposta, envia a proxima. Mede a
    // LATENCIA do IPC (ida+volta). Util para comparar com o burst.
    if (argc >= 3 && string(argv[1]) == "-n") {
        int total = stoi(argv[2]);

        // Monta o pedido a partir dos argumentos restantes
        // ex: ./cliente -n 1000 SELECT 5   ->   "1234|SELECT|5|"
        ostringstream cmd;
        for (int i = 3; i < argc; i++) cmd << (i > 3 ? " " : "") << argv[i];

        istringstream parser(cmd.str());
        string op; int id = 0; string nome;
        parser >> op >> id;
        getline(parser >> ws, nome);

        string pedido = to_string(pid) + "|" + op + "|" +
                        to_string(id) + "|" + nome + "\n";

        cout << "[cliente] benchmark: " << total << " x " << cmd.str() << '\n';
        auto inicio = chrono::steady_clock::now();

        int ok = 0, falhas = 0;
        for (int i = 0; i < total; i++) {
            ssize_t ignorado = write(fdReq, pedido.data(), pedido.size());
            (void)ignorado;

            string resp;
            if (esperarResposta(fdResp, resp)) {
                if (resp.rfind("OK", 0) == 0) ok++; else falhas++;
            } else {
                falhas++;
                break;
            }
        }

        auto fim = chrono::steady_clock::now();
        double ms = chrono::duration<double, milli>(fim - inicio).count();

        cout << "[cliente] concluido: " << ok << " OK, " << falhas
             << " falhas\n";
        cout << "[cliente] tempo total: " << ms << " ms  ("
             << (total > 0 ? ms / total : 0) << " ms/requisicao)\n";

        close(fdReq);
        close(fdResp);
        unlink(meuFifo.c_str());
        return 0;
    }

    // ================= MODO INTERATIVO =================
    cout << "===============================================\n";
    cout << " Cliente do banco paralelo (PID " << pid << ")\n";
    cout << " Comandos:\n";
    cout << "   INSERT <id> <nome>\n";
    cout << "   SELECT <id>\n";
    cout << "   UPDATE <id> <novo nome>\n";
    cout << "   DELETE <id>\n";
    cout << "   sair\n";
    cout << "===============================================\n";

    string linha;
    while (true) {
        cout << "> ";
        if (!getline(cin, linha)) break;      // Ctrl+D
        if (linha == "sair" || linha == "exit" || linha == "quit") break;
        if (linha.empty()) continue;

        // Converte "INSERT 7 Joao" -> "<pid>|INSERT|7|Joao\n"
        istringstream parser(linha);
        string op; int id = 0; string nome;
        parser >> op;
        if (!(parser >> id)) {
            cout << "[cliente] formato invalido. Ex: INSERT 7 Joao\n";
            continue;
        }
        getline(parser >> ws, nome);

        string pedido = to_string(pid) + "|" + op + "|" +
                        to_string(id) + "|" + nome + "\n";

        ssize_t ignorado = write(fdReq, pedido.data(), pedido.size());
        (void)ignorado;

        string resp;
        if (esperarResposta(fdResp, resp)) {
            // resp = "OK|7|Joao" ou "ERR|7|mensagem"
            istringstream rs(resp);
            string status, sid, info;
            getline(rs, status, '|');
            getline(rs, sid, '|');
            getline(rs, info);
            if (status == "OK") {
                cout << "  OK  -> id=" << sid << " " << info << '\n';
            } else {
                cout << "  ERRO-> " << info << '\n';
            }
        } else {
            break;
        }
    }

    // --- Limpeza ---
    close(fdReq);
    close(fdResp);
    unlink(meuFifo.c_str());
    cout << "[cliente] encerrado.\n";
    return 0;
}
