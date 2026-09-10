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
 *                                        (pthread_mutex_t)
 *                                              |
 *                                              v
 *                                    canal de respostas
 *
 * A thread principal so faz IPC: le do canal e enfileira. Quem interpreta o
 * comando, mexe na tabela e responde sao as threads do pool, em paralelo.
 */
#include "comum.hpp"

#include <pthread.h>
#include <semaphore.h>

#include <cstdio>
#include <cstring>
#include <queue>
#include <string>
#include <vector>

namespace {

constexpr int MAX_THREADS = 32;

struct Registro {
    int         id;
    std::string nome;
};

/* ---- tabela compartilhada: o recurso disputado pelas threads ---- */
std::vector<Registro> tabela;
pthread_mutex_t       mutex_tabela = PTHREAD_MUTEX_INITIALIZER;

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

/* ---- canal de respostas: tambem e compartilhado ----
 * Um canal e uma corrente de bytes; sem o mutex, duas threads escrevendo ao
 * mesmo tempo entregariam duas respostas intercaladas. */
void            *canal_respostas = nullptr;
pthread_mutex_t  mutex_resposta = PTHREAD_MUTEX_INITIALIZER;

unsigned long atendidas[MAX_THREADS] = {0};

/* Procura um id na tabela. Chamada sempre com o mutex ja travado. */
int procurar(int id)
{
    for (std::size_t i = 0; i < tabela.size(); ++i) {
        if (tabela[i].id == id)
            return static_cast<int>(i);
    }
    return -1;
}

/*
 * Interpreta e executa um comando. Toda a manipulacao da tabela acontece
 * dentro do mutex - e a secao critica do trabalho.
 */
std::string executar(const char *comando)
{
    int  id = 0;
    char nome[MAX_TEXTO] = "";

    if (std::sscanf(comando, "INSERT id=%d nome='%199[^']'", &id, nome) == 2) {
        pthread_mutex_lock(&mutex_tabela);
        std::string resultado;
        if (procurar(id) >= 0) {
            resultado = "ERRO id " + std::to_string(id) + " ja existe";
        } else {
            tabela.push_back(Registro{id, nome});
            resultado = "OK inserido id=" + std::to_string(id) + " nome='" + nome + "'";
        }
        pthread_mutex_unlock(&mutex_tabela);
        return resultado;
    }

    if (std::sscanf(comando, "SELECT nome WHERE id=%d", &id) == 1 ||
        std::sscanf(comando, "SELECT id=%d", &id) == 1) {
        pthread_mutex_lock(&mutex_tabela);
        const int posicao = procurar(id);
        std::string resultado = (posicao < 0)
            ? "OK 0 registros para id=" + std::to_string(id)
            : "OK id=" + std::to_string(id) + " nome='" + tabela[posicao].nome + "'";
        pthread_mutex_unlock(&mutex_tabela);
        return resultado;
    }

    if (std::sscanf(comando, "UPDATE id=%d nome='%199[^']'", &id, nome) == 2) {
        pthread_mutex_lock(&mutex_tabela);
        const int posicao = procurar(id);
        std::string resultado;
        if (posicao < 0) {
            resultado = "ERRO id " + std::to_string(id) + " nao encontrado";
        } else {
            tabela[posicao].nome = nome;
            resultado = "OK atualizado id=" + std::to_string(id) + " nome='" + nome + "'";
        }
        pthread_mutex_unlock(&mutex_tabela);
        return resultado;
    }

    if (std::sscanf(comando, "DELETE WHERE id=%d", &id) == 1 ||
        std::sscanf(comando, "DELETE id=%d", &id) == 1) {
        pthread_mutex_lock(&mutex_tabela);
        const int posicao = procurar(id);
        std::string resultado;
        if (posicao < 0) {
            resultado = "ERRO id " + std::to_string(id) + " nao encontrado";
        } else {
            tabela.erase(tabela.begin() + posicao);
            resultado = "OK removido id=" + std::to_string(id);
        }
        pthread_mutex_unlock(&mutex_tabela);
        return resultado;
    }

    if (std::strcmp(comando, "LISTAR") == 0) {
        pthread_mutex_lock(&mutex_tabela);
        std::string resultado = "OK " + std::to_string(tabela.size()) + " registro(s):";
        for (const Registro &registro : tabela)
            resultado += " [" + std::to_string(registro.id) + "," + registro.nome + "]";
        pthread_mutex_unlock(&mutex_tabela);
        return resultado.substr(0, MAX_TEXTO - 1);
    }

    return "ERRO comando invalido";
}

/* Corpo das threads do pool. */
void *trabalhador(void *argumento)
{
    const int indice = static_cast<int>(reinterpret_cast<long long>(argumento));

    for (;;) {
        sem_wait(&sem_itens);   /* espera aparecer trabalho na fila */

        pthread_mutex_lock(&mutex_fila);
        if (fila.empty()) {
            /* Fila vazia so acontece quando o encerramento liberou o semaforo
             * de proposito, uma vez para cada thread, para acordar todo mundo. */
            pthread_mutex_unlock(&mutex_fila);
            break;
        }
        const Mensagem requisicao = fila.front();
        fila.pop();
        pthread_mutex_unlock(&mutex_fila);

        Mensagem resposta;
        resposta.id = requisicao.id;
        resposta.thread = indice;
        const std::string texto = executar(requisicao.texto);
        std::snprintf(resposta.texto, MAX_TEXTO, "%s", texto.c_str());

        pthread_mutex_lock(&mutex_resposta);
        canal::escrever(canal_respostas, resposta);
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

    sem_init(&sem_itens, 0, 0);

    void *canal_requisicoes = canal::criar(CANAL_REQUISICOES);
    if (canal_requisicoes == nullptr) {
        std::fprintf(stderr, "erro ao criar o canal: %s\n", canal::ultimoErro().c_str());
        return 1;
    }

    std::printf("Servidor no ar com %d threads. Esperando o cliente...\n", n_threads);
    std::fflush(stdout);

    if (!canal::aguardarConexao(canal_requisicoes)) {
        std::fprintf(stderr, "erro ao aceitar o cliente: %s\n", canal::ultimoErro().c_str());
        return 1;
    }

    canal_respostas = canal::abrir(CANAL_RESPOSTAS, 5000);
    if (canal_respostas == nullptr) {
        std::fprintf(stderr, "erro ao abrir o canal de respostas: %s\n",
                     canal::ultimoErro().c_str());
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
    while (canal::ler(canal_requisicoes, requisicao)) {
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
    std::printf("  total: %lu | registros na tabela: %zu\n", total, tabela.size());

    canal::fechar(canal_respostas);
    canal::fechar(canal_requisicoes);
    sem_destroy(&sem_itens);
    return 0;
}
