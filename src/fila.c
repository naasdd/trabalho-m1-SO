/* fila.c - buffer circular sincronizado com semaforos de contagem. */
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <string.h>

#include "fila.h"

struct Fila {
    Requisicao *itens;
    int         capacidade;
    int         inicio;      /* proxima posicao a ser consumida */
    int         fim;         /* proxima posicao a ser produzida */
    int         quantidade;
    int         pico;
    int         encerrada;

    sem_t           sem_vagas;
    sem_t           sem_itens;
    pthread_mutex_t mutex;
};

Fila *fila_criar(int capacidade)
{
    Fila *fila;

    if (capacidade <= 0)
        return NULL;

    fila = calloc(1, sizeof(Fila));
    if (fila == NULL)
        return NULL;

    fila->itens = calloc((size_t)capacidade, sizeof(Requisicao));
    if (fila->itens == NULL) {
        free(fila);
        return NULL;
    }
    fila->capacidade = capacidade;

    if (sem_init(&fila->sem_vagas, 0, (unsigned)capacidade) != 0 ||
        sem_init(&fila->sem_itens, 0, 0) != 0) {
        free(fila->itens);
        free(fila);
        return NULL;
    }
    pthread_mutex_init(&fila->mutex, NULL);

    return fila;
}

void fila_destruir(Fila *fila)
{
    if (fila == NULL)
        return;
    sem_destroy(&fila->sem_vagas);
    sem_destroy(&fila->sem_itens);
    pthread_mutex_destroy(&fila->mutex);
    free(fila->itens);
    free(fila);
}

int fila_enfileirar(Fila *fila, const Requisicao *requisicao)
{
    sem_wait(&fila->sem_vagas);          /* espera abrir espaco */

    pthread_mutex_lock(&fila->mutex);
    if (fila->encerrada) {
        pthread_mutex_unlock(&fila->mutex);
        sem_post(&fila->sem_vagas);
        return 0;
    }
    fila->itens[fila->fim] = *requisicao;
    fila->fim = (fila->fim + 1) % fila->capacidade;
    fila->quantidade++;
    if (fila->quantidade > fila->pico)
        fila->pico = fila->quantidade;
    pthread_mutex_unlock(&fila->mutex);

    sem_post(&fila->sem_itens);          /* avisa que ha trabalho */
    return 1;
}

int fila_desenfileirar(Fila *fila, Requisicao *destino)
{
    sem_wait(&fila->sem_itens);          /* espera aparecer trabalho */

    pthread_mutex_lock(&fila->mutex);
    if (fila->quantidade == 0) {
        /* Só acontece quando fila_encerrar liberou o semaforo de proposito
         * para acordar as threads que estavam bloqueadas. */
        pthread_mutex_unlock(&fila->mutex);
        return 0;
    }
    *destino = fila->itens[fila->inicio];
    fila->inicio = (fila->inicio + 1) % fila->capacidade;
    fila->quantidade--;
    pthread_mutex_unlock(&fila->mutex);

    sem_post(&fila->sem_vagas);          /* devolve o espaco ao produtor */
    return 1;
}

void fila_encerrar(Fila *fila, int n_consumidores)
{
    int i;

    pthread_mutex_lock(&fila->mutex);
    fila->encerrada = 1;
    pthread_mutex_unlock(&fila->mutex);

    /* Um post por consumidor: quem ainda encontrar trabalho na fila processa
     * normalmente, e quem encontrar a fila vazia recebe o sinal de parada. */
    for (i = 0; i < n_consumidores; i++)
        sem_post(&fila->sem_itens);
}

int fila_pico(Fila *fila)
{
    int pico;

    pthread_mutex_lock(&fila->mutex);
    pico = fila->pico;
    pthread_mutex_unlock(&fila->mutex);
    return pico;
}
