/*
 * banco.c - tabela compartilhada e o controle de concorrencia sobre ela.
 *
 * Solucao de leitores/escritores com catraca (sem starvation):
 *
 *   Leitor                              Escritor
 *   ------                              --------
 *   sem_wait(fila)                      sem_wait(fila)
 *   lock(mutex_leitores)                sem_wait(recurso)
 *     if (++leitores == 1)              sem_post(fila)
 *        sem_wait(recurso)              ... escreve ...
 *   unlock(mutex_leitores)              sem_post(recurso)
 *   sem_post(fila)
 *   ... le ...
 *   lock(mutex_leitores)
 *     if (--leitores == 0)
 *        sem_post(recurso)
 *   unlock(mutex_leitores)
 *
 * "recurso" e um semaforo binario que representa a posse da tabela: so o
 * primeiro leitor o toma e so o ultimo o devolve, o que permite varios SELECT
 * simultaneos. "fila" e um segundo semaforo binario onde todo mundo passa
 * antes de entrar; como um escritor que esta esperando fica segurando a
 * catraca, os leitores que chegam depois dele param na entrada e o escritor
 * nao espera indefinidamente.
 *
 * "mutex_leitores" protege o contador de leitores, que e a unica variavel que
 * os leitores alteram em conjunto.
 */
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "banco.h"
#include "relogio.h"

struct Banco {
    Registro *registros;
    int       capacidade;
    int       usados;           /* slots ja tocados no vetor */

    sem_t           fila;           /* catraca de entrada */
    sem_t           recurso;        /* posse da tabela */
    pthread_mutex_t mutex_leitores; /* protege o contador de leitores */
    int             leitores;

    pthread_mutex_t   mutex_estat;  /* protege os contadores de instrumentacao */
    EstatisticasBanco estatisticas;

    unsigned custo_us;
};

/* ------------------------------------------------------------------ */
/* Entrada e saida das secoes criticas                                 */
/* ------------------------------------------------------------------ */

static void entrar_leitura(Banco *banco)
{
    sem_wait(&banco->fila);
    pthread_mutex_lock(&banco->mutex_leitores);
    banco->leitores++;
    if (banco->leitores == 1)
        sem_wait(&banco->recurso);   /* o primeiro leitor tranca contra escritores */
    if (banco->leitores > banco->estatisticas.pico_leitores)
        banco->estatisticas.pico_leitores = banco->leitores;
    pthread_mutex_unlock(&banco->mutex_leitores);
    sem_post(&banco->fila);
}

static void sair_leitura(Banco *banco)
{
    pthread_mutex_lock(&banco->mutex_leitores);
    banco->leitores--;
    if (banco->leitores == 0)
        sem_post(&banco->recurso);   /* o ultimo leitor libera os escritores */
    pthread_mutex_unlock(&banco->mutex_leitores);

    pthread_mutex_lock(&banco->mutex_estat);
    banco->estatisticas.leituras++;
    pthread_mutex_unlock(&banco->mutex_estat);
}

static void entrar_escrita(Banco *banco)
{
    sem_wait(&banco->fila);
    if (sem_trywait(&banco->recurso) != 0) {
        /* A tabela estava ocupada: houve disputa de verdade pelo recurso. */
        pthread_mutex_lock(&banco->mutex_estat);
        banco->estatisticas.conflitos++;
        pthread_mutex_unlock(&banco->mutex_estat);
        sem_wait(&banco->recurso);
    }
    sem_post(&banco->fila);
}

static void sair_escrita(Banco *banco)
{
    sem_post(&banco->recurso);

    pthread_mutex_lock(&banco->mutex_estat);
    banco->estatisticas.escritas++;
    pthread_mutex_unlock(&banco->mutex_estat);
}

static void contabilizar(Banco *banco, TipoComando tipo)
{
    pthread_mutex_lock(&banco->mutex_estat);
    banco->estatisticas.operacoes[tipo]++;
    pthread_mutex_unlock(&banco->mutex_estat);
}

/* ------------------------------------------------------------------ */
/* Ciclo de vida                                                       */
/* ------------------------------------------------------------------ */

Banco *banco_criar(int capacidade)
{
    Banco *banco;

    if (capacidade <= 0)
        return NULL;

    banco = calloc(1, sizeof(Banco));
    if (banco == NULL)
        return NULL;

    banco->registros = calloc((size_t)capacidade, sizeof(Registro));
    if (banco->registros == NULL) {
        free(banco);
        return NULL;
    }
    banco->capacidade = capacidade;

    if (sem_init(&banco->fila, 0, 1) != 0 ||
        sem_init(&banco->recurso, 0, 1) != 0) {
        free(banco->registros);
        free(banco);
        return NULL;
    }
    pthread_mutex_init(&banco->mutex_leitores, NULL);
    pthread_mutex_init(&banco->mutex_estat, NULL);

    return banco;
}

void banco_destruir(Banco *banco)
{
    if (banco == NULL)
        return;
    sem_destroy(&banco->fila);
    sem_destroy(&banco->recurso);
    pthread_mutex_destroy(&banco->mutex_leitores);
    pthread_mutex_destroy(&banco->mutex_estat);
    free(banco->registros);
    free(banco);
}

void banco_definir_custo(Banco *banco, unsigned custo_us)
{
    banco->custo_us = custo_us;
}

/* ------------------------------------------------------------------ */
/* Busca (chamada sempre com o recurso ja tomado)                      */
/* ------------------------------------------------------------------ */

static int procurar(const Banco *banco, int id)
{
    int i;
    for (i = 0; i < banco->usados; i++) {
        if (banco->registros[i].ativo && banco->registros[i].id == id)
            return i;
    }
    return -1;
}

static int slot_livre(const Banco *banco)
{
    int i;
    for (i = 0; i < banco->usados; i++) {
        if (!banco->registros[i].ativo)
            return i;          /* reaproveita o espaco de um registro removido */
    }
    if (banco->usados < banco->capacidade)
        return banco->usados;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Operacoes                                                           */
/* ------------------------------------------------------------------ */

int banco_inserir(Banco *banco, int id, const char *nome, char *saida, size_t n)
{
    int posicao;
    int ok = 0;

    contabilizar(banco, CMD_INSERT);
    entrar_escrita(banco);

    relogio_gastar_us(banco->custo_us);

    if (procurar(banco, id) >= 0) {
        snprintf(saida, n, "ERRO id %d ja existe", id);
    } else {
        posicao = slot_livre(banco);
        if (posicao < 0) {
            snprintf(saida, n, "ERRO banco cheio (%d registros)", banco->capacidade);
        } else {
            banco->registros[posicao].id = id;
            snprintf(banco->registros[posicao].nome, MAX_NOME, "%s", nome);
            banco->registros[posicao].ativo = 1;
            if (posicao == banco->usados)
                banco->usados++;
            snprintf(saida, n, "OK 1 registro inserido (id=%d nome='%s')", id, nome);
            ok = 1;
        }
    }

    sair_escrita(banco);
    return ok;
}

int banco_selecionar(Banco *banco, int id, char *saida, size_t n)
{
    int posicao;
    int ok = 0;

    contabilizar(banco, CMD_SELECT);
    entrar_leitura(banco);

    relogio_gastar_us(banco->custo_us);

    posicao = procurar(banco, id);
    if (posicao < 0)
        snprintf(saida, n, "OK 0 registros para id=%d", id);
    else {
        snprintf(saida, n, "OK id=%d nome='%s'", id, banco->registros[posicao].nome);
        ok = 1;
    }

    sair_leitura(banco);
    return ok;
}

int banco_atualizar(Banco *banco, int id, const char *nome, char *saida, size_t n)
{
    int posicao;
    int ok = 0;

    contabilizar(banco, CMD_UPDATE);
    entrar_escrita(banco);

    relogio_gastar_us(banco->custo_us);

    posicao = procurar(banco, id);
    if (posicao < 0)
        snprintf(saida, n, "ERRO id %d nao encontrado", id);
    else {
        snprintf(banco->registros[posicao].nome, MAX_NOME, "%s", nome);
        snprintf(saida, n, "OK 1 registro atualizado (id=%d nome='%s')", id, nome);
        ok = 1;
    }

    sair_escrita(banco);
    return ok;
}

int banco_remover(Banco *banco, int id, char *saida, size_t n)
{
    int posicao;
    int ok = 0;

    contabilizar(banco, CMD_DELETE);
    entrar_escrita(banco);

    relogio_gastar_us(banco->custo_us);

    posicao = procurar(banco, id);
    if (posicao < 0)
        snprintf(saida, n, "ERRO id %d nao encontrado", id);
    else {
        banco->registros[posicao].ativo = 0;
        snprintf(saida, n, "OK 1 registro removido (id=%d)", id);
        ok = 1;
    }

    sair_escrita(banco);
    return ok;
}

int banco_listar(Banco *banco, char *saida, size_t n)
{
    size_t usado;
    int i, mostrados = 0, ativos = 0;

    contabilizar(banco, CMD_LISTAR);
    entrar_leitura(banco);

    for (i = 0; i < banco->usados; i++)
        if (banco->registros[i].ativo)
            ativos++;

    usado = (size_t)snprintf(saida, n, "OK %d registro(s):", ativos);
    for (i = 0; i < banco->usados && usado < n; i++) {
        int escrito;
        if (!banco->registros[i].ativo)
            continue;
        escrito = snprintf(saida + usado, n - usado, " [%d,%s]",
                           banco->registros[i].id, banco->registros[i].nome);
        if (escrito < 0 || (size_t)escrito >= n - usado) {
            /* A resposta tem tamanho fixo; avisa que a lista foi cortada. */
            snprintf(saida + n - 5, 5, " ...");
            break;
        }
        usado += (size_t)escrito;
        mostrados++;
    }
    (void)mostrados;

    sair_leitura(banco);
    return 1;
}

void banco_estatisticas(Banco *banco, EstatisticasBanco *destino)
{
    int i, ativos = 0;

    entrar_leitura(banco);
    for (i = 0; i < banco->usados; i++)
        if (banco->registros[i].ativo)
            ativos++;
    sair_leitura(banco);

    pthread_mutex_lock(&banco->mutex_estat);
    *destino = banco->estatisticas;
    destino->registros_ativos = ativos;
    pthread_mutex_unlock(&banco->mutex_estat);
}

/* ------------------------------------------------------------------ */
/* Persistencia                                                        */
/* ------------------------------------------------------------------ */

int banco_carregar(Banco *banco, const char *caminho)
{
    FILE *arquivo = fopen(caminho, "r");
    char linha[MAX_NOME + 64];
    int carregados = 0;

    if (arquivo == NULL)
        return 0;

    entrar_escrita(banco);
    while (fgets(linha, sizeof(linha), arquivo) != NULL && banco->usados < banco->capacidade) {
        char *separador = strchr(linha, ';');
        int id;

        if (separador == NULL)
            continue;
        *separador = '\0';
        id = atoi(linha);
        separador++;
        separador[strcspn(separador, "\r\n")] = '\0';

        banco->registros[banco->usados].id = id;
        snprintf(banco->registros[banco->usados].nome, MAX_NOME, "%s", separador);
        banco->registros[banco->usados].ativo = 1;
        banco->usados++;
        carregados++;
    }
    sair_escrita(banco);

    fclose(arquivo);
    return carregados;
}

int banco_salvar(const Banco *banco, const char *caminho)
{
    FILE *arquivo = fopen(caminho, "w");
    int i, salvos = 0;

    if (arquivo == NULL)
        return -1;

    /* Chamado no encerramento, quando as threads do pool ja terminaram. */
    for (i = 0; i < banco->usados; i++) {
        if (!banco->registros[i].ativo)
            continue;
        fprintf(arquivo, "%d;%s\n", banco->registros[i].id, banco->registros[i].nome);
        salvos++;
    }

    fclose(arquivo);
    return salvos;
}
