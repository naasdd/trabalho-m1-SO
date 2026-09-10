/*
 * registro_log.h - log de execucao do servidor.
 *
 * O arquivo de log e um segundo recurso compartilhado pelas threads do pool.
 * Sem protecao, duas threads escrevendo ao mesmo tempo produziriam linhas
 * intercaladas no meio da frase, entao ha um mutex proprio para ele.
 *
 * O mutex do log e separado do controle da tabela de dados de proposito: se
 * fosse o mesmo, escrever no log serializaria consultas que poderiam rodar em
 * paralelo, e o log passaria a interferir na medicao do paralelismo.
 */
#ifndef REGISTRO_LOG_H
#define REGISTRO_LOG_H

/* Abre o arquivo de log. "eco" liga a copia das mensagens no terminal.
 * Devolve 1 em caso de sucesso. */
int log_iniciar(const char *caminho, int eco);

/* Escreve uma linha no log, com data/hora. Seguro para chamar de qualquer
 * thread. */
void log_escrever(const char *formato, ...);

void log_encerrar(void);

#endif /* REGISTRO_LOG_H */
