// ============================================================
// pool.cpp
// ------------------------------------------------------------
// Implementacao do pool de threads. O trecho mais importante
// (e o mais provavel de o professor pedir para explicar) e o
// loopWorker: ele mostra como mutex e condition variable
// trabalham JUNTOS.
// ============================================================

#include "pool.hpp"

using namespace std;

// ------------------------------------------------------------
// Construtor: cria as N threads. Cada uma comeca executando
// loopWorker() imediatamente — e, como a fila comeca vazia,
// todas vao DORMIR na condition variable ate chegar trabalho.
// ------------------------------------------------------------
Pool::Pool(int nThreads) {
    for (int i = 0; i < nThreads; i++) {
        workers_.emplace_back([this] { loopWorker(); });
    }
}

// ------------------------------------------------------------
// Destrutor: encerramento limpo (graceful shutdown).
// 1) Marca parar_ = true (com o mutex, pois workers leem isso)
// 2) Acorda TODAS as workers (notify_all) para que vejam a flag
// 3) join() em cada uma: espera elas terminarem a tarefa atual
// ------------------------------------------------------------
Pool::~Pool() {
    {
        lock_guard lock(mutex_);
        parar_ = true;
    }
    condicao_.notify_all();          // acorda todo mundo
    for (auto& t : workers_) {
        if (t.joinable()) t.join();  // espera cada uma terminar
    }
}

// ------------------------------------------------------------
// despachar: o PRODUTOR empilha uma tarefa.
// Passos:
//   1) trava o mutex (so ele mexe na fila agora)
//   2) empilha a tarefa
//   3) destrava (fim do escopo do lock_guard)
//   4) notify_one: toca a "campainha" — acorda UMA worker
// ------------------------------------------------------------
void Pool::despachar(Tarefa t) {
    {
        lock_guard lock(mutex_);
        fila_.push(std::move(t));
    }
    condicao_.notify_one();
}

// ------------------------------------------------------------
// loopWorker: o codigo de cada CONSUMIDOR. Roda para sempre:
//
//   esperar tarefa -> pegar tarefa -> executar -> repetir
//
// O ponto SUTIL e IMPORTANTE: condicao_.wait(lock) faz DUAS
// coisas atomicamente:
//   a) DESTRAVA o mutex (para o produtor poder empilhar!)
//   b) coloca a thread para DORMIR ate ser notificada
// Quando acorda, ela RE-TRAVA o mutex antes de continuar.
//
// O "while" (em vez de "if") protege contra SPURIOUS WAKEUP:
// a thread pode acordar sem ter sido notificada (o SO permite
// isso), entao e preciso re-conferir a condicao.
// ------------------------------------------------------------
void Pool::loopWorker() {
    while (true) {
        Tarefa tarefa;
        {
            unique_lock lock(mutex_);
            // Dorme enquanto: fila vazia E nao mandaram parar.
            condicao_.wait(lock, [&] { return parar_ || !fila_.empty(); });

            if (parar_ && fila_.empty()) return; // encerramento

            tarefa = std::move(fila_.front());
            fila_.pop();
        } // <-- mutex DESTRAVADO aqui, ANTES de executar a tarefa

        // Executa FORA da regiao critica: assim varias workers
        // processam tarefas em PARALELO de verdade. Se executassemos
        // com o mutex travado, teriamos um pool "serial" (inutil).
        tarefa();
    }
}

size_t Pool::pendentes() {
    lock_guard lock(mutex_);
    return fila_.size();
}
