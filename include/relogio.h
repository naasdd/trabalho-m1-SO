/*
 * relogio.h - medicao de tempo monotonico e trabalho simulado.
 *
 * Usado em dois lugares: para cronometrar os experimentos (throughput, tempo
 * medio de resposta) e para dar as operacoes do banco um custo de CPU
 * controlado, de modo que o efeito do paralelismo apareca na medicao.
 */
#ifndef RELOGIO_H
#define RELOGIO_H

/* Instante atual em milissegundos, a partir de um relogio monotonico (nao
 * anda para tras se o relogio do sistema for ajustado). */
double relogio_agora_ms(void);

/*
 * Consome aproximadamente "microssegundos" de CPU em espera ocupada.
 *
 * E espera ocupada de proposito: dormir (sleep/nanosleep) devolveria o
 * processador ao sistema e as "operacoes" pareceriam paralelas mesmo quando
 * estivessem serializadas por um mutex. Gastando CPU de verdade, o ganho
 * medido reflete o paralelismo real da maquina.
 */
void relogio_gastar_us(unsigned microssegundos);

/* Numero de nucleos de processamento disponiveis (0 se nao for possivel
 * descobrir). Serve para escolher o tamanho padrao do pool de threads. */
int relogio_nucleos(void);

#endif /* RELOGIO_H */
