/*
 * canal_win.c - implementacao do canal IPC sobre named pipe do Windows.
 *
 * Existe para que o trabalho possa ser desenvolvido e demonstrado tambem em
 * Windows. O conceito e o mesmo do FIFO POSIX: um canal nomeado mantido pelo
 * kernel, com duas pontas em processos diferentes, e leitura bloqueante.
 * A implementacao de referencia, exigida pelo enunciado, e a POSIX
 * (src/canal_posix.c).
 *
 * Diferenca de comportamento que vale citar na apresentacao: uma instancia de
 * named pipe atende um cliente por vez. Enquanto um cliente esta conectado ao
 * canal de requisicoes, os outros ficam esperando em CreateFile. No FIFO POSIX
 * varios clientes escrevem no mesmo canal ao mesmo tempo, e a atomicidade da
 * escrita ate PIPE_BUF garante que as mensagens nao se misturam. O paralelismo
 * do servidor (o pool de threads) nao muda: ele continua processando varias
 * requisicoes simultaneamente nos dois sistemas.
 */
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "canal.h"

#define TAM_BUFFER_PIPE 4096

struct Canal {
    HANDLE handle;
    int    dono;        /* 1 = criou o pipe (ponta de leitura) */
    int    conectado;   /* 1 = ha um cliente conectado do outro lado */
    char   nome[MAX_CAMINHO];
};

static _Thread_local char ultimo_erro[256];

static void guardar_erro(const char *contexto, DWORD codigo)
{
    char mensagem[180] = "";

    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, codigo, 0, mensagem, sizeof(mensagem), NULL);
    /* FormatMessage devolve a mensagem terminada em \r\n. */
    mensagem[strcspn(mensagem, "\r\n")] = '\0';
    snprintf(ultimo_erro, sizeof(ultimo_erro), "%s: (%lu) %s",
             contexto, (unsigned long)codigo, mensagem);
}

static void montar_nome(char *destino, size_t n, const char *nome)
{
    snprintf(destino, n, "\\\\.\\pipe\\%s", nome);
}

Canal *canal_criar(const char *nome)
{
    Canal *canal = calloc(1, sizeof(Canal));
    if (canal == NULL) {
        snprintf(ultimo_erro, sizeof(ultimo_erro), "sem memoria para o canal");
        return NULL;
    }

    montar_nome(canal->nome, sizeof(canal->nome), nome);

    canal->handle = CreateNamedPipeA(canal->nome,
                                     PIPE_ACCESS_DUPLEX,
                                     PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                     PIPE_UNLIMITED_INSTANCES,
                                     TAM_BUFFER_PIPE, TAM_BUFFER_PIPE,
                                     0, NULL);
    if (canal->handle == INVALID_HANDLE_VALUE) {
        guardar_erro("CreateNamedPipe", GetLastError());
        free(canal);
        return NULL;
    }

    canal->dono = 1;
    return canal;
}

Canal *canal_abrir(const char *nome, int timeout_ms)
{
    Canal *canal;
    char nome_completo[MAX_CAMINHO];
    HANDLE handle;
    int esperado = 0;

    montar_nome(nome_completo, sizeof(nome_completo), nome);

    for (;;) {
        handle = CreateFileA(nome_completo, GENERIC_WRITE, 0, NULL,
                             OPEN_EXISTING, 0, NULL);
        if (handle != INVALID_HANDLE_VALUE)
            break;

        {
            DWORD erro = GetLastError();
            /* ERROR_PIPE_BUSY: o pipe existe, mas a instancia esta ocupada com
             * outro cliente. ERROR_FILE_NOT_FOUND: o outro processo ainda nao
             * criou o canal. Nos dois casos vale esperar. */
            if ((erro != ERROR_PIPE_BUSY && erro != ERROR_FILE_NOT_FOUND) ||
                esperado >= timeout_ms) {
                guardar_erro("CreateFile no named pipe", erro);
                return NULL;
            }
        }
        Sleep(50);
        esperado += 50;
    }

    canal = calloc(1, sizeof(Canal));
    if (canal == NULL) {
        CloseHandle(handle);
        snprintf(ultimo_erro, sizeof(ultimo_erro), "sem memoria para o canal");
        return NULL;
    }

    canal->handle = handle;
    canal->dono = 0;
    snprintf(canal->nome, sizeof(canal->nome), "%s", nome_completo);
    return canal;
}

/* Espera um cliente se conectar a instancia do pipe. */
static int aguardar_conexao(Canal *canal)
{
    if (canal->conectado)
        return 1;

    if (ConnectNamedPipe(canal->handle, NULL)) {
        canal->conectado = 1;
        return 1;
    }
    if (GetLastError() == ERROR_PIPE_CONNECTED) {
        /* O cliente conectou entre CreateNamedPipe e ConnectNamedPipe. */
        canal->conectado = 1;
        return 1;
    }

    guardar_erro("ConnectNamedPipe", GetLastError());
    return 0;
}

int canal_ler(Canal *canal, void *destino, size_t n)
{
    char *p = destino;
    size_t lidos = 0;

    while (lidos < n) {
        DWORD r = 0;

        if (canal->dono && !aguardar_conexao(canal))
            return 0;

        if (ReadFile(canal->handle, p + lidos, (DWORD)(n - lidos), &r, NULL) && r > 0) {
            lidos += r;
            continue;
        }

        /* O cliente fechou a ponta dele. Como o dono do canal e um servidor de
         * vida longa, ele volta a esperar o proximo cliente em vez de encerrar
         * - mesmo papel do truque do O_RDWR na versao POSIX. */
        if (canal->dono && GetLastError() == ERROR_BROKEN_PIPE && lidos == 0) {
            DisconnectNamedPipe(canal->handle);
            canal->conectado = 0;
            continue;
        }

        guardar_erro("ReadFile no named pipe", GetLastError());
        return 0;
    }
    return 1;
}

int canal_escrever(Canal *canal, const void *origem, size_t n)
{
    const char *p = origem;
    size_t escritos = 0;

    while (escritos < n) {
        DWORD w = 0;
        if (!WriteFile(canal->handle, p + escritos, (DWORD)(n - escritos), &w, NULL)) {
            guardar_erro("WriteFile no named pipe", GetLastError());
            return 0;
        }
        escritos += w;
    }
    return 1;
}

void canal_fechar(Canal *canal)
{
    if (canal == NULL)
        return;

    if (canal->handle != INVALID_HANDLE_VALUE) {
        if (canal->dono) {
            if (canal->conectado) {
                FlushFileBuffers(canal->handle);
                DisconnectNamedPipe(canal->handle);
            }
        }
        CloseHandle(canal->handle);
    }
    free(canal);
}

void canal_remover(const char *nome)
{
    /* No Windows nao ha o que remover: o named pipe deixa de existir sozinho
     * quando o ultimo handle e fechado. A funcao existe para manter a mesma
     * interface da versao POSIX, onde o FIFO fica no sistema de arquivos. */
    (void)nome;
}

const char *canal_erro(void)
{
    return ultimo_erro[0] ? ultimo_erro : "sem erro registrado";
}
