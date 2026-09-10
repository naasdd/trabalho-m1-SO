/* registro_log.c - escrita concorrente no arquivo de log, com mutex proprio. */
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

#include "registro_log.h"

static FILE            *arquivo;
static int              eco_terminal;
static pthread_mutex_t  mutex = PTHREAD_MUTEX_INITIALIZER;

int log_iniciar(const char *caminho, int eco)
{
    eco_terminal = eco;
    arquivo = fopen(caminho, "a");
    return arquivo != NULL;
}

void log_escrever(const char *formato, ...)
{
    va_list argumentos;
    char horario[32];
    time_t agora = time(NULL);
    struct tm partes;

#ifdef _WIN32
    localtime_s(&partes, &agora);
#else
    localtime_r(&agora, &partes);
#endif
    strftime(horario, sizeof(horario), "%H:%M:%S", &partes);

    /* Secao critica: monta e despeja a linha inteira antes de soltar o mutex,
     * para que nenhuma outra thread consiga se intercalar no meio dela. */
    pthread_mutex_lock(&mutex);

    if (arquivo != NULL) {
        va_start(argumentos, formato);
        fprintf(arquivo, "[%s] ", horario);
        vfprintf(arquivo, formato, argumentos);
        fputc('\n', arquivo);
        va_end(argumentos);
        fflush(arquivo);
    }
    if (eco_terminal) {
        va_start(argumentos, formato);
        printf("[%s] ", horario);
        vprintf(formato, argumentos);
        putchar('\n');
        va_end(argumentos);
        fflush(stdout);
    }

    pthread_mutex_unlock(&mutex);
}

void log_encerrar(void)
{
    pthread_mutex_lock(&mutex);
    if (arquivo != NULL) {
        fclose(arquivo);
        arquivo = NULL;
    }
    pthread_mutex_unlock(&mutex);
}
