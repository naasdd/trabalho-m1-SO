/* relogio.c - implementacao do relogio monotonico nos dois sistemas. */
#include "relogio.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
#endif

double relogio_agora_ms(void)
{
#ifdef _WIN32
    static LARGE_INTEGER frequencia;
    LARGE_INTEGER agora;

    if (frequencia.QuadPart == 0)
        QueryPerformanceFrequency(&frequencia);
    QueryPerformanceCounter(&agora);
    return (double)agora.QuadPart * 1000.0 / (double)frequencia.QuadPart;
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1000000.0;
#endif
}

void relogio_gastar_us(unsigned microssegundos)
{
    double limite;
    volatile unsigned long acumulador = 0;

    if (microssegundos == 0)
        return;

    limite = relogio_agora_ms() + microssegundos / 1000.0;
    while (relogio_agora_ms() < limite) {
        /* Um pouco de aritmetica entre as leituras do relogio para nao
         * transformar o laco em uma sequencia de chamadas de sistema. O
         * "volatile" impede o compilador de eliminar o trabalho. */
        int i;
        for (i = 0; i < 64; i++)
            acumulador = acumulador * 1103515245u + 12345u;
    }
}

int relogio_nucleos(void)
{
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return (int)info.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 0;
#endif
}
