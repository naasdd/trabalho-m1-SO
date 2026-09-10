/*
 * cliente.c - processo cliente.
 *
 * Executavel separado do servidor, com o seu proprio espaco de enderecos.
 * Toda a conversa acontece por IPC:
 *
 *   1. o cliente cria o seu canal de resposta privado ("sgbd_resp_<pid>");
 *   2. abre o canal de requisicoes publicado pelo servidor;
 *   3. envia a requisicao com o nome do canal de resposta dentro dela;
 *   4. bloqueia esperando a resposta chegar no seu canal.
 *
 * O canal de resposta ser privado resolve o problema de varios clientes ao
 * mesmo tempo: se todos lessem de um canal unico de respostas, cada um
 * poderia acabar lendo a resposta do outro, porque um canal entrega o byte a
 * quem chegar primeiro.
 *
 * Modos de uso: interativo (digitando comandos), por arquivo de script e
 * gerador de carga para os experimentos.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "canal.h"
#include "protocolo.h"
#include "relogio.h"

#ifdef _WIN32
#include <windows.h>
static int id_processo(void) { return (int)GetCurrentProcessId(); }
#else
#include <unistd.h>
static int id_processo(void) { return (int)getpid(); }
#endif

typedef struct {
    Canal *requisicoes;
    Canal *respostas;
    char   nome_resposta[MAX_CANAL];
    int    id;
    int    proxima_requisicao;
    int    silencioso;

    /* medicoes do lado do cliente */
    unsigned long enviadas;
    unsigned long com_erro;
    double        latencia_total_ms;
    double        latencia_min_ms;
    double        latencia_max_ms;
} Cliente;

/*
 * Envia um comando e espera a resposta. Devolve 0 se a comunicacao falhar.
 * O par escrever/ler e sincrono de proposito: e o comportamento normal de um
 * cliente de banco, e e o que permite medir a latencia de cada requisicao.
 */
static int enviar(Cliente *cliente, const char *texto)
{
    Requisicao requisicao;
    Resposta resposta;
    double inicio, latencia;

    memset(&requisicao, 0, sizeof(requisicao));
    requisicao.id_cliente = cliente->id;
    requisicao.id_requisicao = ++cliente->proxima_requisicao;
    snprintf(requisicao.canal_resposta, MAX_CANAL, "%s", cliente->nome_resposta);
    snprintf(requisicao.texto, MAX_TEXTO, "%s", texto);

    inicio = relogio_agora_ms();

    if (!canal_escrever(cliente->requisicoes, &requisicao, sizeof(requisicao))) {
        fprintf(stderr, "erro ao enviar a requisicao: %s\n", canal_erro());
        return 0;
    }
    if (!canal_ler(cliente->respostas, &resposta, sizeof(resposta))) {
        fprintf(stderr, "erro ao receber a resposta: %s\n", canal_erro());
        return 0;
    }

    latencia = relogio_agora_ms() - inicio;
    cliente->enviadas++;
    cliente->latencia_total_ms += latencia;
    if (cliente->enviadas == 1 || latencia < cliente->latencia_min_ms)
        cliente->latencia_min_ms = latencia;
    if (latencia > cliente->latencia_max_ms)
        cliente->latencia_max_ms = latencia;
    if (!resposta.sucesso)
        cliente->com_erro++;

    resposta.texto[MAX_TEXTO - 1] = '\0';
    if (!cliente->silencioso)
        printf("  <- [thread %d, %.2f ms] %s\n", resposta.thread, latencia, resposta.texto);

    return 1;
}

/* ------------------------------------------------------------------ */
/* Modos de operacao                                                   */
/* ------------------------------------------------------------------ */

static void modo_interativo(Cliente *cliente)
{
    char linha[MAX_TEXTO];

    printf("Cliente %d conectado. Comandos aceitos:\n", cliente->id);
    printf("  INSERT id=7 nome='Joao'     UPDATE id=7 nome='Joana'\n");
    printf("  SELECT nome WHERE id=7      DELETE WHERE id=7\n");
    printf("  LISTAR                      SHUTDOWN\n");
    printf("Linha vazia ou 'sair' termina o cliente.\n\n");

    for (;;) {
        printf("sgbd> ");
        fflush(stdout);

        if (fgets(linha, sizeof(linha), stdin) == NULL)
            break;
        linha[strcspn(linha, "\r\n")] = '\0';

        if (linha[0] == '\0' || strcmp(linha, "sair") == 0 || strcmp(linha, "exit") == 0)
            break;
        if (!enviar(cliente, linha))
            break;
    }
}

static void modo_arquivo(Cliente *cliente, const char *caminho)
{
    FILE *arquivo = fopen(caminho, "r");
    char linha[MAX_TEXTO];

    if (arquivo == NULL) {
        fprintf(stderr, "erro: nao foi possivel abrir '%s'\n", caminho);
        return;
    }

    while (fgets(linha, sizeof(linha), arquivo) != NULL) {
        linha[strcspn(linha, "\r\n")] = '\0';
        if (linha[0] == '\0' || linha[0] == '#')   /* linha vazia ou comentario */
            continue;
        if (!cliente->silencioso)
            printf("-> %s\n", linha);
        if (!enviar(cliente, linha))
            break;
    }

    fclose(arquivo);
}

/*
 * Gerador de carga. Sorteia comandos dentro de uma faixa de ids exclusiva
 * deste cliente, para que varios clientes rodando ao mesmo tempo disputem o
 * banco sem gerar erros artificiais de id repetido.
 */
static void modo_carga(Cliente *cliente, int quantidade, int percentual_leitura,
                       int faixa_inicio, int faixa_tamanho, unsigned semente)
{
    char comando[MAX_TEXTO];
    int i;

    srand(semente);

    for (i = 0; i < quantidade; i++) {
        int sorteio = rand() % 100;
        int id = faixa_inicio + (rand() % faixa_tamanho);

        if (sorteio < percentual_leitura)
            snprintf(comando, sizeof(comando), "SELECT nome WHERE id=%d", id);
        else if (sorteio < percentual_leitura + (100 - percentual_leitura) / 2)
            snprintf(comando, sizeof(comando), "INSERT id=%d nome='cliente%d_reg%d'",
                     id, cliente->id % 1000, i);
        else if (sorteio < percentual_leitura + (100 - percentual_leitura) * 3 / 4)
            snprintf(comando, sizeof(comando), "UPDATE id=%d nome='atualizado%d'", id, i);
        else
            snprintf(comando, sizeof(comando), "DELETE WHERE id=%d", id);

        if (!enviar(cliente, comando))
            break;
    }
}

/* ------------------------------------------------------------------ */
/* Programa principal                                                  */
/* ------------------------------------------------------------------ */

static void uso(const char *programa)
{
    printf("uso: %s [modo] [opcoes]\n\n", programa);
    printf("modos:\n");
    printf("  (nenhum)              interativo, le comandos do teclado\n");
    printf("  --arquivo ARQUIVO     envia cada linha do arquivo como comando\n");
    printf("  --carga N             envia N requisicoes geradas automaticamente\n");
    printf("  --comando \"TEXTO\"     envia um unico comando e sai\n");
    printf("  --encerrar            envia SHUTDOWN ao servidor\n\n");
    printf("opcoes:\n");
    printf("  --leituras P          percentual de SELECT na carga (padrao: 70)\n");
    printf("  --faixa INICIO:TAM    faixa de ids usada na carga (padrao: 1:500)\n");
    printf("  --semente N           semente do sorteio, para repetir o experimento\n");
    printf("  --csv ARQUIVO         acrescenta uma linha de resultados ao arquivo\n");
    printf("  --silencioso          nao imprime cada resposta (use nas medicoes)\n");
    printf("  --ajuda               mostra esta mensagem\n");
}

int main(int argc, char **argv)
{
    Cliente cliente;
    const char *caminho_arquivo = NULL;
    const char *comando_unico = NULL;
    const char *caminho_csv = NULL;
    int carga = 0;
    int encerrar = 0;
    int percentual_leitura = 70;
    int faixa_inicio = 1, faixa_tamanho = 500;
    unsigned semente = 0;
    int i;
    double inicio, tempo_ms;

    memset(&cliente, 0, sizeof(cliente));
    cliente.id = id_processo();

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--arquivo") == 0 && i + 1 < argc)
            caminho_arquivo = argv[++i];
        else if (strcmp(argv[i], "--carga") == 0 && i + 1 < argc)
            carga = atoi(argv[++i]);
        else if (strcmp(argv[i], "--comando") == 0 && i + 1 < argc)
            comando_unico = argv[++i];
        else if (strcmp(argv[i], "--encerrar") == 0)
            encerrar = 1;
        else if (strcmp(argv[i], "--leituras") == 0 && i + 1 < argc)
            percentual_leitura = atoi(argv[++i]);
        else if (strcmp(argv[i], "--faixa") == 0 && i + 1 < argc) {
            const char *valor = argv[++i];
            const char *doispontos = strchr(valor, ':');
            faixa_inicio = atoi(valor);
            if (doispontos != NULL)
                faixa_tamanho = atoi(doispontos + 1);
        } else if (strcmp(argv[i], "--semente") == 0 && i + 1 < argc)
            semente = (unsigned)atoi(argv[++i]);
        else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc)
            caminho_csv = argv[++i];
        else if (strcmp(argv[i], "--silencioso") == 0)
            cliente.silencioso = 1;
        else if (strcmp(argv[i], "--ajuda") == 0 || strcmp(argv[i], "-h") == 0) {
            uso(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "opcao desconhecida: %s\n", argv[i]);
            uso(argv[0]);
            return 1;
        }
    }

    if (percentual_leitura < 0) percentual_leitura = 0;
    if (percentual_leitura > 100) percentual_leitura = 100;
    if (faixa_tamanho <= 0) faixa_tamanho = 1;
    if (semente == 0) semente = (unsigned)cliente.id;

    /* O canal de resposta precisa existir antes da primeira requisicao: o
     * servidor vai tentar abri-lo assim que receber o comando. */
    snprintf(cliente.nome_resposta, MAX_CANAL, "sgbd_resp_%d", cliente.id);
    cliente.respostas = canal_criar(cliente.nome_resposta);
    if (cliente.respostas == NULL) {
        fprintf(stderr, "erro: nao foi possivel criar o canal de resposta: %s\n",
                canal_erro());
        return 1;
    }

    cliente.requisicoes = canal_abrir(CANAL_REQUISICOES, 5000);
    if (cliente.requisicoes == NULL) {
        fprintf(stderr, "erro: o servidor nao esta no ar (%s)\n", canal_erro());
        fprintf(stderr, "      suba o servidor antes de rodar o cliente.\n");
        canal_fechar(cliente.respostas);
        return 1;
    }

    inicio = relogio_agora_ms();

    if (encerrar)
        enviar(&cliente, "SHUTDOWN");
    else if (comando_unico != NULL)
        enviar(&cliente, comando_unico);
    else if (caminho_arquivo != NULL)
        modo_arquivo(&cliente, caminho_arquivo);
    else if (carga > 0)
        modo_carga(&cliente, carga, percentual_leitura, faixa_inicio, faixa_tamanho, semente);
    else
        modo_interativo(&cliente);

    tempo_ms = relogio_agora_ms() - inicio;

    if (cliente.enviadas > 0 && (carga > 0 || caminho_arquivo != NULL)) {
        printf("\n--- cliente %d ---\n", cliente.id);
        printf("Requisicoes enviadas ....... %lu (%lu com erro)\n",
               cliente.enviadas, cliente.com_erro);
        printf("Tempo total ................ %.2f ms\n", tempo_ms);
        printf("Vazao ...................... %.1f req/s\n",
               tempo_ms > 0.0 ? (double)cliente.enviadas * 1000.0 / tempo_ms : 0.0);
        printf("Latencia media / min / max . %.3f / %.3f / %.3f ms\n",
               cliente.latencia_total_ms / (double)cliente.enviadas,
               cliente.latencia_min_ms, cliente.latencia_max_ms);

        if (caminho_csv != NULL) {
            FILE *csv = fopen(caminho_csv, "a");
            if (csv != NULL) {
                fseek(csv, 0, SEEK_END);
                if (ftell(csv) == 0)
                    fprintf(csv, "cliente;requisicoes;erros;tempo_ms;vazao_req_s;"
                                 "latencia_media_ms;latencia_min_ms;latencia_max_ms\n");
                fprintf(csv, "%d;%lu;%lu;%.2f;%.1f;%.3f;%.3f;%.3f\n",
                        cliente.id, cliente.enviadas, cliente.com_erro, tempo_ms,
                        tempo_ms > 0.0 ? (double)cliente.enviadas * 1000.0 / tempo_ms : 0.0,
                        cliente.latencia_total_ms / (double)cliente.enviadas,
                        cliente.latencia_min_ms, cliente.latencia_max_ms);
                fclose(csv);
            }
        }
    }

    canal_fechar(cliente.requisicoes);
    canal_fechar(cliente.respostas);
    return 0;
}
