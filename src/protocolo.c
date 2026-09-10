/*
 * protocolo.c - interpretacao do texto das requisicoes.
 *
 * O parser roda dentro das threads do pool, ou seja, em paralelo. Por isso ele
 * e deliberadamente puro: nao usa nenhuma variavel global nem funcoes com
 * estado interno (strtok, por exemplo), so o buffer recebido como parametro.
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "protocolo.h"

/* Comparacao sem distinguir maiusculas de minusculas (evita depender de
 * strcasecmp, que nao e padrao C). */
static int igual_ci(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

/* Copia a primeira palavra de "texto" para "destino". Devolve o ponteiro para
 * o caractere seguinte a palavra. */
static const char *primeira_palavra(const char *texto, char *destino, size_t n)
{
    size_t i = 0;

    while (*texto && isspace((unsigned char)*texto))
        texto++;
    while (*texto && !isspace((unsigned char)*texto)) {
        if (i + 1 < n)
            destino[i++] = *texto;
        texto++;
    }
    destino[i] = '\0';
    return texto;
}

/*
 * Procura a chave "campo=" no texto, respeitando limites de palavra: "id=" nao
 * pode casar com o final de "uid=". Devolve o ponteiro para o primeiro
 * caractere depois do '=' ou NULL.
 */
static const char *achar_campo(const char *texto, const char *campo)
{
    size_t tam = strlen(campo);
    const char *p = texto;

    while (*p) {
        if (tolower((unsigned char)*p) == tolower((unsigned char)campo[0])) {
            size_t i = 0;
            while (i < tam && p[i] &&
                   tolower((unsigned char)p[i]) == tolower((unsigned char)campo[i]))
                i++;
            if (i == tam && p[tam] == '=') {
                char anterior = (p == texto) ? ' ' : p[-1];
                if (!isalnum((unsigned char)anterior) && anterior != '_')
                    return p + tam + 1;
            }
        }
        p++;
    }
    return NULL;
}

/*
 * Le o valor de "nome". Aceita 'entre aspas simples', "entre aspas duplas" ou
 * uma palavra solta. Devolve 1 se encontrou o campo.
 */
static int ler_nome(const char *texto, char *destino, size_t n)
{
    const char *p = achar_campo(texto, "nome");
    size_t i = 0;
    char fecha;

    if (p == NULL)
        return 0;

    if (*p == '\'' || *p == '"') {
        fecha = *p++;
        while (*p && *p != fecha) {
            if (i + 1 < n)
                destino[i++] = *p;
            p++;
        }
    } else {
        while (*p && !isspace((unsigned char)*p)) {
            if (i + 1 < n)
                destino[i++] = *p;
            p++;
        }
    }
    destino[i] = '\0';
    return i > 0;
}

/* Le o valor inteiro de "id". Devolve 1 se encontrou um numero valido. */
static int ler_id(const char *texto, int *destino)
{
    const char *p = achar_campo(texto, "id");
    int sinal = 1;
    long valor = 0;
    int digitos = 0;

    if (p == NULL)
        return 0;

    if (*p == '-') {
        sinal = -1;
        p++;
    }
    while (isdigit((unsigned char)*p)) {
        valor = valor * 10 + (*p - '0');
        digitos++;
        p++;
        if (valor > 1000000000L)  /* corta entradas absurdas antes de estourar */
            return 0;
    }
    if (digitos == 0)
        return 0;

    *destino = (int)(valor * sinal);
    return 1;
}

static Comando invalido(const char *motivo)
{
    Comando c;
    memset(&c, 0, sizeof(c));
    c.tipo = CMD_INVALIDO;
    snprintf(c.erro, sizeof(c.erro), "%s", motivo);
    return c;
}

Comando protocolo_interpretar(const char *texto)
{
    Comando cmd;
    char verbo[16];
    int tem_id, tem_nome;

    memset(&cmd, 0, sizeof(cmd));
    primeira_palavra(texto, verbo, sizeof(verbo));

    if (verbo[0] == '\0')
        return invalido("comando vazio");

    tem_id = ler_id(texto, &cmd.id);
    tem_nome = ler_nome(texto, cmd.nome, sizeof(cmd.nome));

    if (igual_ci(verbo, "INSERT")) {
        if (!tem_id || !tem_nome)
            return invalido("INSERT exige id=<n> e nome=<texto>");
        cmd.tipo = CMD_INSERT;
    } else if (igual_ci(verbo, "SELECT")) {
        if (!tem_id)
            return invalido("SELECT exige um id (ex.: SELECT nome WHERE id=5)");
        cmd.tipo = CMD_SELECT;
    } else if (igual_ci(verbo, "UPDATE")) {
        if (!tem_id || !tem_nome)
            return invalido("UPDATE exige id=<n> e nome=<texto>");
        cmd.tipo = CMD_UPDATE;
    } else if (igual_ci(verbo, "DELETE")) {
        if (!tem_id)
            return invalido("DELETE exige um id (ex.: DELETE WHERE id=5)");
        cmd.tipo = CMD_DELETE;
    } else if (igual_ci(verbo, "LISTAR") || igual_ci(verbo, "DUMP")) {
        cmd.tipo = CMD_LISTAR;
    } else if (igual_ci(verbo, "SHUTDOWN")) {
        cmd.tipo = CMD_SHUTDOWN;
    } else {
        return invalido("comando desconhecido");
    }

    if (cmd.tipo != CMD_LISTAR && cmd.tipo != CMD_SHUTDOWN && tem_id && cmd.id < 0)
        return invalido("id deve ser maior ou igual a zero");

    return cmd;
}

const char *protocolo_nome_tipo(TipoComando tipo)
{
    switch (tipo) {
    case CMD_INSERT:   return "INSERT";
    case CMD_SELECT:   return "SELECT";
    case CMD_UPDATE:   return "UPDATE";
    case CMD_DELETE:   return "DELETE";
    case CMD_LISTAR:   return "LISTAR";
    case CMD_SHUTDOWN: return "SHUTDOWN";
    default:           return "INVALIDO";
    }
}
