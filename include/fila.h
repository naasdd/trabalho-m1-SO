/*
 * fila.h - fila de tarefas entre a thread leitora e o pool de threads.
 *
 * E o problema classico do produtor/consumidor:
 *
 *   - produtor: a thread principal do servidor, que le as requisicoes do
 *     canal IPC e as coloca na fila;
 *   - consumidores: as threads do pool, que retiram uma requisicao por vez e
 *     a executam sobre o banco.
 *
 * A fila e um buffer circular de tamanho fixo, sincronizado com dois semaforos
 * de contagem e um mutex:
 *
 *   sem_t vagas  -> quantos espacos livres restam (o produtor espera nele)
 *   sem_t itens  -> quantas requisicoes ha para consumir (os consumidores
 *                   esperam nele)
 *   mutex        -> protege os indices do buffer circular
 *
 * O limite de tamanho tambem serve de controle de fluxo: se os clientes
 * mandarem requisicoes mais rapido do que o pool consegue processar, o
 * produtor bloqueia em vez de consumir memoria sem limite.
 */
#ifndef FILA_H
#define FILA_H

#include "protocolo.h"

typedef struct Fila Fila;

Fila *fila_criar(int capacidade);
void  fila_destruir(Fila *fila);

/* Coloca uma requisicao na fila, esperando se ela estiver cheia.
 * Devolve 0 se a fila ja foi encerrada. */
int fila_enfileirar(Fila *fila, const Requisicao *requisicao);

/* Retira uma requisicao, esperando se ela estiver vazia.
 * Devolve 0 quando a fila foi encerrada e esvaziada: e o sinal para a thread
 * do pool terminar. */
int fila_desenfileirar(Fila *fila, Requisicao *destino);

/*
 * Marca a fila como encerrada e acorda os "n_consumidores" que possam estar
 * bloqueados esperando trabalho. O numero vem de quem criou o pool - com
 * semaforo nao da para "acordar todo mundo" como em pthread_cond_broadcast,
 * entao e preciso liberar uma vez para cada thread.
 */
void fila_encerrar(Fila *fila, int n_consumidores);

/* Maior ocupacao observada, indicador de quanto o pool ficou para tras do
 * ritmo de chegada das requisicoes. */
int fila_pico(Fila *fila);

#endif /* FILA_H */
