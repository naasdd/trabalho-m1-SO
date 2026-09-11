#ifndef POOL_HPP
#define POOL_HPP

// ============================================================
// pool.hpp
// ------------------------------------------------------------
// Pool de threads com fila de tarefas (padrao
// PRODUTOR-CONSUMIDOR, classico de Sistemas Operacionais):
//
//   - PRODUTOR: a thread principal do servidor, que le os
//     pedidos do FIFO e os empilha na fila.
//   - CONSUMIDORES: as N worker threads, que retiram pedidos
//     da fila e os processam em paralelo.
//
// Sincronizacao da fila (PERGUNTA CLASSICA DE PROVA):
//
//   mutex_               -> protege a fila em si: so uma thread
//                           mexendo na estrutura por vez.
//   condicao_ (cond var) -> "campainha": se a fila esta vazia,
//                           a worker thread DORME (sem gastar
//                           CPU) ate o produtor notificar que
//                           chegou tarefa nova.
//
// Por que nao criar uma thread nova por pedido?
// Porque criar/destruir threads tem custo alto. No pool, as
// threads ja existem e ficam reaproveitadas — como cozinheiros
// contratados esperando pedidos, em vez de contratar um
// cozinheiro novo para cada prato.
// ============================================================

#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>
#include <atomic>

class Pool {
public:
    // Uma tarefa e simplesmente uma funcao sem parametros
    // (o pedido em si vai "capturado" dentro dela).
    using Tarefa = std::function<void()>;

    // Cria o pool com n worker threads ja rodando.
    explicit Pool(int nThreads);

    // Destrutor: sinaliza parada e espera todas as threads
    // terminarem a tarefa atual antes de encerrar (join).
    ~Pool();

    // Produtor chama isto: empilha uma tarefa e acorda uma worker.
    void despachar(Tarefa t);

    // Quantas tarefas estao esperando na fila (para logs).
    size_t pendentes();

private:
    std::vector<std::thread> workers_;  // as N threads trabalhadoras
    std::queue<Tarefa> fila_;           // fila de tarefas pendentes

    std::mutex mutex_;                  // protege fila_
    std::condition_variable condicao_;  // "campainha" de tarefa nova
    bool parar_ = false;                // flag de encerramento

    // Codigo executado por cada worker thread (loop infinito).
    void loopWorker();
};

#endif // POOL_HPP
