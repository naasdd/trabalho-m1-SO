/*
 * servidor.cpp - processo servidor (gerenciador do banco).
 *
 *      canal de requisicoes (named pipe)
 *                 |
 *                 v
 *      thread principal --enfileira--> [ fila de tarefas ]
 *                                          |     |     |
 *                                     thread 0  ...  thread N-1
 *                                          \     |     /
 *                                        tabela compartilhada
 *                                          (pthread_mutex_t)
 *                                                |
 *                                                v
 *                                       canal de respostas
 *
 * A thread principal so faz IPC: le do canal e enfileira. Quem interpreta o
 * comando, mexe na tabela e responde sao as threads do pool, em paralelo.
 * Assim uma requisicao demorada nunca bloqueia a leitura do canal.
 */
#include <pthread.h>
#include <semaphore.h>

#include <cstdio>
#include <cstdlib>
#include <queue>

#include "banco.h"

namespace {

constexpr int MAX_THREADS = 32;

/* ---- fila de tarefas: produtor (thread principal) / consumidores (pool) ----
 *
 * A fila nao tem limite de tamanho de proposito. Se ela bloqueasse quando
 * cheia, o sistema poderia travar: o cliente envia todas as requisicoes antes
 * de ler as respostas, entao as threads podem ficar presas escrevendo em um
 * canal de respostas cheio enquanto o cliente ainda esta escrevendo
 * requisicoes. Com a fila sempre aceitando, a thread principal continua
 * drenando o canal de requisicoes e o impasse nao acontece. */
std::queue<Mensagem> fila;
pthread_mutex_t      mutex_fila = PTHREAD_MUTEX_INITIALIZER;
sem_t                sem_itens;   /* conta quantas tarefas ha para consumir */

/* ---- canal de respostas: tambem e recurso compartilhado ----
 * Um canal e uma corrente de bytes; sem o mutex, duas threads escrevendo ao
 * mesmo tempo entregariam duas respostas intercaladas. */
HANDLE          canal_respostas = nullptr;
pthread_mutex_t mutex_resposta = PTHREAD_MUTEX_INITIALIZER;

unsigned long atendidas[MAX_THREADS] = {0};

/* Corpo das threads do pool. */
void *trabalhador(void *argumento)
{
    const int indice = static_cast<int>(reinterpret_cast<long long>(argumento));

    for (;;) {
        sem_wait(&sem_itens);   /* espera aparecer trabalho na fila */

        pthread_mutex_lock(&mutex_fila);
        if (fila.empty()) {
            /* Fila vazia so acontece quando o encerramento liberou o semaforo
             * de proposito, uma vez por thread, para acordar todo mundo. */
            pthread_mutex_unlock(&mutex_fila);
            break;
        }
        const Mensagem requisicao = fila.front();
        fila.pop();
        pthread_mutex_unlock(&mutex_fila);

        Mensagem resposta;
        resposta.id = requisicao.id;
        resposta.thread = indice;
        const std::string texto = executarComando(requisicao.texto);
        std::snprintf(resposta.texto, MAX_TEXTO, "%s", texto.c_str());

        /* O log vai dentro do mesmo mutex para as linhas de threads diferentes
         * nao se misturarem no terminal. */
        pthread_mutex_lock(&mutex_resposta);
        escreverMensagem(canal_respostas, resposta);
        std::printf("[thread %d] #%-3d %s -> %s\n",
                    indice, requisicao.id, requisicao.texto, resposta.texto);
        std::fflush(stdout);
        pthread_mutex_unlock(&mutex_resposta);

        ++atendidas[indice];
    }

    return nullptr;
}

}  // namespace

int main(int argc, char **argv)
{
    int n_threads = (argc > 1) ? std::atoi(argv[1]) : 4;
    if (n_threads < 1) n_threads = 1;
    if (n_threads > MAX_THREADS) n_threads = MAX_THREADS;

    /* Carrega antes de criar as threads: aqui ainda nao ha concorrencia. */
    const int carregados = carregarBanco();

    sem_init(&sem_itens, 0, 0);

    HANDLE canal_requisicoes = criarCanal(CANAL_REQUISICOES);
    if (canal_requisicoes == nullptr) {
        std::fprintf(stderr, "erro ao criar o canal: %s\n", ultimoErro().c_str());
        return 1;
    }

    std::printf("Servidor no ar com %d threads.\n", n_threads);
    std::printf("%d registro(s) carregado(s) de %s.\n", carregados, ARQUIVO_BANCO);
    std::printf("Esperando o cliente...\n");
    std::fflush(stdout);

    if (!aguardarConexao(canal_requisicoes)) {
        std::fprintf(stderr, "erro ao aceitar o cliente: %s\n", ultimoErro().c_str());
        return 1;
    }

    canal_respostas = abrirCanal(CANAL_RESPOSTAS, 5000);
    if (canal_respostas == nullptr) {
        std::fprintf(stderr, "erro ao abrir o canal de respostas: %s\n", ultimoErro().c_str());
        return 1;
    }
    std::printf("Cliente conectado.\n");
    std::fflush(stdout);

    pthread_t threads[MAX_THREADS];
    for (int i = 0; i < n_threads; ++i)
        pthread_create(&threads[i], nullptr, trabalhador,
                       reinterpret_cast<void *>(static_cast<long long>(i)));

    /* Thread principal: so IPC. Le uma requisicao e entrega ao pool.
     * O laco termina quando o cliente fecha a ponta dele do canal. */
    Mensagem requisicao;
    while (lerMensagem(canal_requisicoes, requisicao)) {
        requisicao.texto[MAX_TEXTO - 1] = '\0';

        pthread_mutex_lock(&mutex_fila);
        fila.push(requisicao);
        pthread_mutex_unlock(&mutex_fila);

        sem_post(&sem_itens);   /* avisa o pool que ha trabalho */
    }

    /* Encerramento: um post por thread acorda quem estiver esperando na fila
     * vazia. Quem ainda achar trabalho enfileirado processa antes de sair. */
    for (int i = 0; i < n_threads; ++i)
        sem_post(&sem_itens);
    for (int i = 0; i < n_threads; ++i)
        pthread_join(threads[i], nullptr);

    std::printf("\nCliente desconectado. Requisicoes atendidas por thread:\n");
    unsigned long total = 0;
    for (int i = 0; i < n_threads; ++i) {
        std::printf("  thread %d: %lu\n", i, atendidas[i]);
        total += atendidas[i];
    }
    std::printf("  total: %lu | registros em %s: %zu\n",
                total, ARQUIVO_BANCO, tabela.size());

    fecharCanal(canal_respostas);
    fecharCanal(canal_requisicoes);
    sem_destroy(&sem_itens);
    return 0;
}
