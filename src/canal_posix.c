/*
 * canal_posix.c - implementacao do canal IPC sobre FIFO nomeado POSIX.
 *
 * Um FIFO nomeado (criado com mkfifo) aparece no sistema de arquivos, mas nao
 * e um arquivo comum: o dado nao vai para o disco, fica em um buffer do
 * kernel. Escrever nele com write() e ler com read() e uma chamada de sistema
 * de IPC de verdade, com bloqueio quando nao ha dado disponivel - nada de
 * polling em arquivo temporario.
 *
 * Detalhe importante: um FIFO aberto so para leitura devolve EOF assim que o
 * ultimo escritor fecha a ponta dele. Como o servidor precisa continuar vivo
 * entre um cliente e outro, o lado dono abre o FIFO com O_RDWR. Assim sempre
 * existe pelo menos um escritor (ele mesmo), read() volta a bloquear em vez de
 * retornar 0, e o servidor sobrevive a clientes que entram e saem.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "canal.h"

#define DIRETORIO_FIFO "/tmp"

struct Canal {
    int  fd;
    int  dono;                 /* 1 = criou o FIFO, logo deve remove-lo */
    char caminho[MAX_CAMINHO];
};

static _Thread_local char ultimo_erro[256];

static void guardar_erro(const char *contexto)
{
    snprintf(ultimo_erro, sizeof(ultimo_erro), "%s: %s", contexto, strerror(errno));
}

static void montar_caminho(char *destino, size_t n, const char *nome)
{
    snprintf(destino, n, "%s/%s", DIRETORIO_FIFO, nome);
}

static void dormir_ms(int ms)
{
    struct timespec t;
    t.tv_sec  = ms / 1000;
    t.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&t, NULL);
}

Canal *canal_criar(const char *nome)
{
    Canal *canal = calloc(1, sizeof(Canal));
    if (canal == NULL) {
        snprintf(ultimo_erro, sizeof(ultimo_erro), "sem memoria para o canal");
        return NULL;
    }

    montar_caminho(canal->caminho, sizeof(canal->caminho), nome);

    /* Um FIFO orfao de uma execucao anterior seria reaproveitado com dados
     * antigos dentro; e mais seguro recriar. */
    unlink(canal->caminho);

    if (mkfifo(canal->caminho, 0666) < 0 && errno != EEXIST) {
        guardar_erro("mkfifo");
        free(canal);
        return NULL;
    }

    canal->fd = open(canal->caminho, O_RDWR);
    if (canal->fd < 0) {
        guardar_erro("open do FIFO");
        unlink(canal->caminho);
        free(canal);
        return NULL;
    }

    canal->dono = 1;
    return canal;
}

Canal *canal_abrir(const char *nome, int timeout_ms)
{
    Canal *canal;
    char caminho[MAX_CAMINHO];
    int esperado = 0;
    int fd = -1;

    montar_caminho(caminho, sizeof(caminho), nome);

    /* O canal pode ainda nao existir se o outro processo acabou de subir. */
    for (;;) {
        fd = open(caminho, O_WRONLY);
        if (fd >= 0)
            break;
        if (errno != ENOENT || esperado >= timeout_ms) {
            guardar_erro("open do FIFO para escrita");
            return NULL;
        }
        dormir_ms(50);
        esperado += 50;
    }

    canal = calloc(1, sizeof(Canal));
    if (canal == NULL) {
        close(fd);
        snprintf(ultimo_erro, sizeof(ultimo_erro), "sem memoria para o canal");
        return NULL;
    }

    canal->fd = fd;
    canal->dono = 0;
    snprintf(canal->caminho, sizeof(canal->caminho), "%s", caminho);
    return canal;
}

int canal_ler(Canal *canal, void *destino, size_t n)
{
    char *p = destino;
    size_t lidos = 0;

    while (lidos < n) {
        ssize_t r = read(canal->fd, p + lidos, n - lidos);
        if (r > 0) {
            lidos += (size_t)r;
        } else if (r == 0) {
            return 0;                 /* canal encerrado */
        } else if (errno == EINTR) {
            continue;                 /* sinal no meio da chamada, tenta de novo */
        } else {
            guardar_erro("read do FIFO");
            return 0;
        }
    }
    return 1;
}

int canal_escrever(Canal *canal, const void *origem, size_t n)
{
    const char *p = origem;
    size_t escritos = 0;

    while (escritos < n) {
        ssize_t w = write(canal->fd, p + escritos, n - escritos);
        if (w > 0) {
            escritos += (size_t)w;
        } else if (w < 0 && errno == EINTR) {
            continue;
        } else {
            guardar_erro("write no FIFO");
            return 0;
        }
    }
    return 1;
}

void canal_fechar(Canal *canal)
{
    if (canal == NULL)
        return;
    if (canal->fd >= 0)
        close(canal->fd);
    if (canal->dono)
        unlink(canal->caminho);
    free(canal);
}

void canal_remover(const char *nome)
{
    char caminho[MAX_CAMINHO];
    montar_caminho(caminho, sizeof(caminho), nome);
    unlink(caminho);
}

const char *canal_erro(void)
{
    return ultimo_erro[0] ? ultimo_erro : "sem erro registrado";
}
