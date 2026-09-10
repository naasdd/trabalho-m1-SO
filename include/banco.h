/*
 * banco.h - a "tabela de dados" compartilhada pelas threads do servidor.
 *
 * Este e o recurso disputado do trabalho: todas as threads do pool operam
 * sobre o mesmo vetor de registros. O acesso e controlado por uma solucao
 * classica de leitores/escritores, montada com semaforos (sem_t) e um mutex
 * (pthread_mutex_t):
 *
 *   - SELECT e LISTAR sao leitores: varios podem executar ao mesmo tempo;
 *   - INSERT, UPDATE e DELETE sao escritores: exclusao mutua total.
 *
 * Um semaforo extra funciona como catraca na entrada, para que uma sequencia
 * continua de leitores nao deixe um escritor esperando para sempre
 * (starvation).
 */
#ifndef BANCO_H
#define BANCO_H

#include <stddef.h>

#include "protocolo.h"

typedef struct {
    int  id;
    char nome[MAX_NOME];
    int  ativo;          /* 0 apos um DELETE; o slot pode ser reaproveitado */
} Registro;

typedef struct Banco Banco;

/* Numeros coletados durante a execucao, usados no relatorio. */
typedef struct {
    unsigned long operacoes[CMD_INVALIDO + 1]; /* contagem por tipo de comando */
    unsigned long leituras;
    unsigned long escritas;
    unsigned long conflitos;      /* vezes que um escritor encontrou o recurso ocupado */
    int           pico_leitores;  /* maior numero de leitores simultaneos observado */
    int           registros_ativos;
} EstatisticasBanco;

/* Cria o banco com espaco para "capacidade" registros. */
Banco *banco_criar(int capacidade);
void   banco_destruir(Banco *banco);

/*
 * Define quantos microssegundos de CPU cada operacao gasta dentro da secao
 * critica. Serve para simular o custo de uma operacao real de banco e tornar
 * visivel, na medicao, a diferenca entre leituras concorrentes e escritas
 * serializadas. Zero desliga a simulacao.
 */
void banco_definir_custo(Banco *banco, unsigned custo_us);

/* Persistencia simples em CSV, para o banco sobreviver entre execucoes. */
int banco_carregar(Banco *banco, const char *caminho);
int banco_salvar(const Banco *banco, const char *caminho);

/*
 * Operacoes. Todas devolvem 1 em caso de sucesso e 0 em caso de erro, e
 * escrevem em "saida" a mensagem que sera devolvida ao cliente.
 */
int banco_inserir(Banco *banco, int id, const char *nome, char *saida, size_t n);
int banco_selecionar(Banco *banco, int id, char *saida, size_t n);
int banco_atualizar(Banco *banco, int id, const char *nome, char *saida, size_t n);
int banco_remover(Banco *banco, int id, char *saida, size_t n);
int banco_listar(Banco *banco, char *saida, size_t n);

void banco_estatisticas(Banco *banco, EstatisticasBanco *destino);

#endif /* BANCO_H */
