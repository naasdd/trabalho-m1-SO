/*
 * protocolo.h - formato das mensagens trocadas entre cliente e servidor.
 *
 * As mensagens sao structs de tamanho fixo. Isso e proposital: em um FIFO
 * POSIX, toda escrita menor que PIPE_BUF (>= 512 bytes) e atomica, entao
 * varios clientes podem escrever no mesmo canal sem que as requisicoes se
 * misturem. Com mensagens de tamanho variavel seria preciso um enquadramento
 * proprio para separar uma requisicao da outra.
 */
#ifndef PROTOCOLO_H
#define PROTOCOLO_H

#define MAX_NOME     50   /* tamanho do campo "nome" do registro */
#define MAX_TEXTO   180   /* tamanho do comando cru / do texto de resposta */
#define MAX_ERRO    120   /* mensagem de erro de sintaxe; cabe em MAX_TEXTO
                             junto com o prefixo da resposta */
#define MAX_CANAL    64   /* tamanho do nome de um canal IPC */

/* Comando enviado pelo cliente, ainda em texto ("INSERT id=7 nome='Joao'"). */
typedef struct {
    int  id_requisicao;          /* sequencial dentro do cliente */
    int  id_cliente;             /* PID do cliente; identifica o canal de volta */
    char canal_resposta[MAX_CANAL]; /* canal onde o cliente espera a resposta */
    char texto[MAX_TEXTO];       /* comando cru, terminado em '\0' */
} Requisicao;

/* Resposta devolvida pelo servidor no canal privado do cliente. */
typedef struct {
    int  id_requisicao;
    int  id_cliente;
    int  sucesso;                /* 1 = comando executado, 0 = erro */
    int  thread;                 /* indice da thread do pool que atendeu */
    char texto[MAX_TEXTO];       /* resultado ou mensagem de erro */
} Resposta;

/* Comando ja interpretado pelo servidor. */
typedef enum {
    CMD_INSERT,
    CMD_SELECT,
    CMD_UPDATE,
    CMD_DELETE,
    CMD_LISTAR,     /* extra: despeja a tabela inteira, util na apresentacao */
    CMD_SHUTDOWN,   /* extra: encerra o servidor de forma ordenada */
    CMD_INVALIDO
} TipoComando;

typedef struct {
    TipoComando tipo;
    int         id;
    char        nome[MAX_NOME];
    char        erro[MAX_ERRO];  /* preenchido quando tipo == CMD_INVALIDO */
} Comando;

/*
 * Interpreta o texto da requisicao. Sempre devolve um Comando valido; em caso
 * de erro de sintaxe o tipo e CMD_INVALIDO e o campo erro explica o motivo.
 *
 * Sintaxe aceita (palavras-chave sem distincao de maiusculas/minusculas):
 *   INSERT id=7 nome='Joao'
 *   SELECT nome WHERE id=5      (a lista de campos e ignorada)
 *   SELECT id=5
 *   UPDATE id=5 nome='Maria'
 *   DELETE WHERE id=5
 *   DELETE id=5
 *   LISTAR
 *   SHUTDOWN
 */
Comando protocolo_interpretar(const char *texto);

/* Nome legivel do tipo de comando, para log e estatisticas. */
const char *protocolo_nome_tipo(TipoComando tipo);

#endif /* PROTOCOLO_H */
