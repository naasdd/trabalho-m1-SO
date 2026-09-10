/*
 * canal.h - canal de comunicacao entre processos (IPC).
 *
 * A interface e a mesma nos dois sistemas; muda so a implementacao:
 *
 *   Linux/macOS (src/canal_posix.c) -> FIFO nomeado POSIX (mkfifo), criado em
 *                                      /tmp, aberto com open/read/write.
 *   Windows     (src/canal_win.c)   -> named pipe (\\.\pipe\...), criado com
 *                                      CreateNamedPipe.
 *
 * Nos dois casos e IPC de verdade do sistema operacional: dois processos
 * distintos, sem memoria em comum, sem arquivo lido por polling.
 *
 * O canal e unidirecional. O sistema usa dois tipos de canal:
 *   - um canal de requisicoes, criado pelo servidor, que todos os clientes
 *     abrem para escrever;
 *   - um canal de resposta por cliente, criado pelo cliente, que o servidor
 *     abre para escrever (o nome vai dentro da propria requisicao).
 */
#ifndef CANAL_H
#define CANAL_H

#include <stddef.h>

#define MAX_CAMINHO 256  /* caminho do FIFO ou nome do named pipe */

typedef struct Canal Canal;

/*
 * Cria o canal e assume a ponta de leitura. Quem chama e o dono do canal:
 * o servidor para as requisicoes, o cliente para as suas respostas.
 * Nao bloqueia esperando um escritor.
 */
Canal *canal_criar(const char *nome);

/*
 * Abre um canal ja existente para escrita. Espera ate "timeout_ms"
 * milissegundos pelo canal aparecer (use 0 para nao esperar). Devolve NULL se
 * o canal nao existir dentro do prazo.
 */
Canal *canal_abrir(const char *nome, int timeout_ms);

/*
 * Le exatamente "n" bytes. Bloqueia enquanto nao houver dados.
 * Devolve 1 em caso de sucesso e 0 se o canal foi encerrado.
 */
int canal_ler(Canal *canal, void *destino, size_t n);

/* Escreve exatamente "n" bytes. Devolve 1 em caso de sucesso. */
int canal_escrever(Canal *canal, const void *origem, size_t n);

/* Fecha o canal e, se este processo for o dono, remove-o do sistema. */
void canal_fechar(Canal *canal);

/* Remove um canal orfao deixado por uma execucao anterior. */
void canal_remover(const char *nome);

/* Descricao do ultimo erro do canal, para mensagens ao usuario. */
const char *canal_erro(void);

/* Nome do canal de requisicoes usado por cliente e servidor. */
#define CANAL_REQUISICOES "sgbd_requisicoes"

#endif /* CANAL_H */
