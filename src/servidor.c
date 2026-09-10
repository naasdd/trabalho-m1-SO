/*
 * servidor.c - processo servidor (gerenciador do banco).
 *
 * Estrutura interna:
 *
 *      canal IPC de requisicoes (FIFO / named pipe)
 *                 |
 *                 v
 *      thread principal  --enfileira-->  [ fila de tarefas ]
 *                                              |  |  |
 *                                    +---------+  |  +---------+
 *                                    v            v            v
 *                                 thread 0     thread 1  ...  thread N-1
 *                                    \            |            /
 *                                     +---> tabela compartilhada <---+
 *                                           (mutex + semaforos)
 *                                                 |
 *                                                 v
 *                                    canal de resposta de cada cliente
 *
 * A thread principal so faz IPC: le do canal e enfileira. Quem executa o
 * comando, toca no banco e responde ao cliente sao as threads do pool, em
 * paralelo. Assim uma requisicao lenta nunca bloqueia a leitura do canal.
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "banco.h"
#include "canal.h"
#include "fila.h"
#include "protocolo.h"
#include "registro_log.h"
#include "relogio.h"

#define MAX_THREADS        64
#define MAX_DESTINOS       64
#define CAPACIDADE_PADRAO  4096
#define FILA_PADRAO        256

/* Canal de resposta de um cliente, mantido aberto enquanto o servidor vive
 * para nao pagar o custo de abrir o canal a cada requisicao. */
typedef struct {
    char   nome[MAX_CANAL];
    Canal *canal;
} Destino;

typedef struct {
    Banco *banco;
    Fila  *fila;

    pthread_mutex_t mutex_destinos;   /* protege o cache de canais de resposta */
    Destino         destinos[MAX_DESTINOS];
    int             n_destinos;

    /* Instrumentacao por thread: nao precisa de mutex porque cada thread so
     * escreve na sua propria posicao do vetor. */
    unsigned long atendidas[MAX_THREADS];
    double        ocupacao_ms[MAX_THREADS];

    int n_threads;
    int verboso;
} Servidor;

typedef struct {
    Servidor *servidor;
    int       indice;
} ContextoThread;

/* ------------------------------------------------------------------ */
/* Canais de resposta                                                  */
/* ------------------------------------------------------------------ */

/*
 * Devolve o canal de resposta do cliente, abrindo-o na primeira vez. Chamada
 * pelas threads do pool ao mesmo tempo, por isso o cache tem mutex proprio.
 */
static Canal *canal_do_cliente(Servidor *servidor, const char *nome)
{
    Canal *canal = NULL;
    int i;

    pthread_mutex_lock(&servidor->mutex_destinos);

    for (i = 0; i < servidor->n_destinos; i++) {
        if (strcmp(servidor->destinos[i].nome, nome) == 0) {
            canal = servidor->destinos[i].canal;
            break;
        }
    }

    if (canal == NULL && servidor->n_destinos < MAX_DESTINOS) {
        canal = canal_abrir(nome, 2000);
        if (canal != NULL) {
            snprintf(servidor->destinos[servidor->n_destinos].nome, MAX_CANAL, "%s", nome);
            servidor->destinos[servidor->n_destinos].canal = canal;
            servidor->n_destinos++;
        } else {
            log_escrever("ERRO nao foi possivel abrir o canal de resposta '%s' (%s)",
                         nome, canal_erro());
        }
    }

    pthread_mutex_unlock(&servidor->mutex_destinos);
    return canal;
}

static void fechar_destinos(Servidor *servidor)
{
    int i;
    for (i = 0; i < servidor->n_destinos; i++)
        canal_fechar(servidor->destinos[i].canal);
    servidor->n_destinos = 0;
}

static void responder(Servidor *servidor, const Requisicao *requisicao,
                      const Resposta *resposta)
{
    Canal *canal = canal_do_cliente(servidor, requisicao->canal_resposta);
    if (canal == NULL)
        return;

    /* Um canal e uma corrente de bytes: duas threads escrevendo ao mesmo tempo
     * poderiam intercalar as respostas. Como cada cliente tem o seu canal e
     * cada requisicao e atendida por uma unica thread, nao ha duas threads
     * escrevendo no mesmo canal ao mesmo tempo - a nao ser que o mesmo cliente
     * tenha varias requisicoes em voo, caso tratado no cliente, que envia uma
     * por vez em cada fluxo. */
    if (!canal_escrever(canal, resposta, sizeof(*resposta)))
        log_escrever("ERRO falha ao responder o cliente %d (%s)",
                     requisicao->id_cliente, canal_erro());
}

/* ------------------------------------------------------------------ */
/* Execucao de um comando                                              */
/* ------------------------------------------------------------------ */

static void executar(Servidor *servidor, const Comando *comando, Resposta *resposta)
{
    char *saida = resposta->texto;
    size_t n = sizeof(resposta->texto);

    switch (comando->tipo) {
    case CMD_INSERT:
        resposta->sucesso = banco_inserir(servidor->banco, comando->id, comando->nome, saida, n);
        break;
    case CMD_SELECT:
        resposta->sucesso = banco_selecionar(servidor->banco, comando->id, saida, n);
        break;
    case CMD_UPDATE:
        resposta->sucesso = banco_atualizar(servidor->banco, comando->id, comando->nome, saida, n);
        break;
    case CMD_DELETE:
        resposta->sucesso = banco_remover(servidor->banco, comando->id, saida, n);
        break;
    case CMD_LISTAR:
        resposta->sucesso = banco_listar(servidor->banco, saida, n);
        break;
    case CMD_SHUTDOWN:
        resposta->sucesso = 1;
        snprintf(saida, n, "OK servidor encerrando");
        break;
    default:
        resposta->sucesso = 0;
        snprintf(saida, n, "ERRO %s", comando->erro);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Thread do pool                                                      */
/* ------------------------------------------------------------------ */

static void *trabalhador(void *argumento)
{
    ContextoThread *contexto = argumento;
    Servidor *servidor = contexto->servidor;
    int indice = contexto->indice;
    Requisicao requisicao;

    log_escrever("thread %d do pool iniciada", indice);

    /* fila_desenfileirar bloqueia enquanto nao ha trabalho e devolve 0 quando
     * o servidor manda encerrar. */
    while (fila_desenfileirar(servidor->fila, &requisicao)) {
        double inicio = relogio_agora_ms();
        Comando comando;
        Resposta resposta;

        memset(&resposta, 0, sizeof(resposta));
        resposta.id_requisicao = requisicao.id_requisicao;
        resposta.id_cliente = requisicao.id_cliente;
        resposta.thread = indice;

        comando = protocolo_interpretar(requisicao.texto);
        executar(servidor, &comando, &resposta);
        responder(servidor, &requisicao, &resposta);

        servidor->atendidas[indice]++;
        servidor->ocupacao_ms[indice] += relogio_agora_ms() - inicio;

        if (servidor->verboso)
            log_escrever("thread %d | cliente %d req %d | %-8s | %s",
                         indice, requisicao.id_cliente, requisicao.id_requisicao,
                         protocolo_nome_tipo(comando.tipo), resposta.texto);
    }

    log_escrever("thread %d do pool encerrada apos %lu requisicoes",
                 indice, servidor->atendidas[indice]);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Relatorio final                                                     */
/* ------------------------------------------------------------------ */

static void imprimir_resumo(Servidor *servidor, double tempo_ms,
                            unsigned long total, unsigned custo_us,
                            const char *caminho_csv)
{
    EstatisticasBanco estatisticas;
    double throughput;
    double soma_ocupacao = 0.0;
    int i;

    banco_estatisticas(servidor->banco, &estatisticas);
    throughput = tempo_ms > 0.0 ? (double)total * 1000.0 / tempo_ms : 0.0;
    for (i = 0; i < servidor->n_threads; i++)
        soma_ocupacao += servidor->ocupacao_ms[i];

    printf("\n=============== RESUMO DA EXECUCAO ===============\n");
    printf("Threads no pool ............ %d\n", servidor->n_threads);
    printf("Custo simulado por operacao. %u us\n", custo_us);
    printf("Requisicoes atendidas ...... %lu\n", total);
    printf("Tempo total de servico ..... %.2f ms\n", tempo_ms);
    printf("Vazao ...................... %.1f req/s\n", throughput);
    printf("Tempo medio por requisicao . %.3f ms\n",
           total > 0 ? soma_ocupacao / (double)total : 0.0);
    printf("Paralelismo medio .......... %.2fx\n",
           tempo_ms > 0.0 ? soma_ocupacao / tempo_ms : 0.0);
    printf("--------------------------------------------------\n");
    printf("INSERT %lu | SELECT %lu | UPDATE %lu | DELETE %lu | LISTAR %lu | invalidos %lu\n",
           estatisticas.operacoes[CMD_INSERT], estatisticas.operacoes[CMD_SELECT],
           estatisticas.operacoes[CMD_UPDATE], estatisticas.operacoes[CMD_DELETE],
           estatisticas.operacoes[CMD_LISTAR], estatisticas.operacoes[CMD_INVALIDO]);
    printf("Leituras %lu | escritas %lu | conflitos de escrita %lu\n",
           estatisticas.leituras, estatisticas.escritas, estatisticas.conflitos);
    printf("Pico de leitores simultaneos %d | pico da fila %d\n",
           estatisticas.pico_leitores, fila_pico(servidor->fila));
    printf("Registros ativos no banco .. %d\n", estatisticas.registros_ativos);
    printf("--------------------------------------------------\n");
    for (i = 0; i < servidor->n_threads; i++)
        printf("  thread %-2d | %6lu requisicoes | %8.2f ms ocupada | %5.1f%% do tempo\n",
               i, servidor->atendidas[i], servidor->ocupacao_ms[i],
               tempo_ms > 0.0 ? servidor->ocupacao_ms[i] * 100.0 / tempo_ms : 0.0);
    printf("==================================================\n");

    if (caminho_csv != NULL) {
        /* Uma linha por execucao, para montar as tabelas do relatorio. */
        FILE *csv = fopen(caminho_csv, "a");
        if (csv != NULL) {
            long posicao;
            fseek(csv, 0, SEEK_END);
            posicao = ftell(csv);
            if (posicao == 0)
                fprintf(csv, "threads;custo_us;requisicoes;tempo_ms;vazao_req_s;"
                             "leituras;escritas;conflitos;pico_leitores;pico_fila\n");
            fprintf(csv, "%d;%u;%lu;%.2f;%.1f;%lu;%lu;%lu;%d;%d\n",
                    servidor->n_threads, custo_us, total, tempo_ms, throughput,
                    estatisticas.leituras, estatisticas.escritas,
                    estatisticas.conflitos, estatisticas.pico_leitores,
                    fila_pico(servidor->fila));
            fclose(csv);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Programa principal                                                  */
/* ------------------------------------------------------------------ */

static void uso(const char *programa)
{
    printf("uso: %s [opcoes]\n\n", programa);
    printf("  --threads N       tamanho do pool de threads (padrao: nucleos da maquina)\n");
    printf("  --fila N          capacidade da fila de tarefas (padrao: %d)\n", FILA_PADRAO);
    printf("  --capacidade N    numero maximo de registros (padrao: %d)\n", CAPACIDADE_PADRAO);
    printf("  --custo-us N      custo de CPU simulado por operacao (padrao: 0)\n");
    printf("  --dados ARQUIVO   arquivo de persistencia (padrao: dados/banco.csv)\n");
    printf("  --log ARQUIVO     arquivo de log (padrao: log/servidor.log)\n");
    printf("  --csv ARQUIVO     acrescenta uma linha de resultados ao arquivo\n");
    printf("  --verboso         registra no log cada requisicao atendida\n");
    printf("  --eco             repete o log no terminal\n");
    printf("  --ajuda           mostra esta mensagem\n");
}

int main(int argc, char **argv)
{
    Servidor servidor;
    ContextoThread contextos[MAX_THREADS];
    pthread_t threads[MAX_THREADS];
    Canal *canal_requisicoes;
    Requisicao requisicao;

    int n_threads = 0;
    int capacidade_fila = FILA_PADRAO;
    int capacidade_banco = CAPACIDADE_PADRAO;
    unsigned custo_us = 0;
    const char *caminho_dados = "dados/banco.csv";
    const char *caminho_log = "log/servidor.log";
    const char *caminho_csv = NULL;
    int eco = 0;
    int i;

    unsigned long total = 0;
    double inicio_servico = 0.0;
    double fim_servico = 0.0;
    int carregados;

    memset(&servidor, 0, sizeof(servidor));

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc)
            n_threads = atoi(argv[++i]);
        else if (strcmp(argv[i], "--fila") == 0 && i + 1 < argc)
            capacidade_fila = atoi(argv[++i]);
        else if (strcmp(argv[i], "--capacidade") == 0 && i + 1 < argc)
            capacidade_banco = atoi(argv[++i]);
        else if (strcmp(argv[i], "--custo-us") == 0 && i + 1 < argc)
            custo_us = (unsigned)atoi(argv[++i]);
        else if (strcmp(argv[i], "--dados") == 0 && i + 1 < argc)
            caminho_dados = argv[++i];
        else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc)
            caminho_log = argv[++i];
        else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc)
            caminho_csv = argv[++i];
        else if (strcmp(argv[i], "--verboso") == 0)
            servidor.verboso = 1;
        else if (strcmp(argv[i], "--eco") == 0)
            eco = 1;
        else if (strcmp(argv[i], "--ajuda") == 0 || strcmp(argv[i], "-h") == 0) {
            uso(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "opcao desconhecida: %s\n", argv[i]);
            uso(argv[0]);
            return 1;
        }
    }

    if (n_threads <= 0) {
        n_threads = relogio_nucleos();
        if (n_threads <= 0)
            n_threads = 4;
    }
    if (n_threads > MAX_THREADS)
        n_threads = MAX_THREADS;
    servidor.n_threads = n_threads;

    if (!log_iniciar(caminho_log, eco))
        fprintf(stderr, "aviso: nao foi possivel abrir '%s'; seguindo sem log em arquivo\n",
                caminho_log);

    servidor.banco = banco_criar(capacidade_banco);
    servidor.fila = fila_criar(capacidade_fila);
    if (servidor.banco == NULL || servidor.fila == NULL) {
        fprintf(stderr, "erro: falta de memoria na inicializacao\n");
        return 1;
    }
    banco_definir_custo(servidor.banco, custo_us);
    pthread_mutex_init(&servidor.mutex_destinos, NULL);

    carregados = banco_carregar(servidor.banco, caminho_dados);

    /* O canal precisa existir antes das threads, senao um cliente rapido
     * poderia tentar escrever num canal que ainda nao foi criado. */
    canal_requisicoes = canal_criar(CANAL_REQUISICOES);
    if (canal_requisicoes == NULL) {
        fprintf(stderr, "erro: nao foi possivel criar o canal de requisicoes: %s\n",
                canal_erro());
        return 1;
    }

    for (i = 0; i < n_threads; i++) {
        contextos[i].servidor = &servidor;
        contextos[i].indice = i;
        if (pthread_create(&threads[i], NULL, trabalhador, &contextos[i]) != 0) {
            fprintf(stderr, "erro: nao foi possivel criar a thread %d\n", i);
            servidor.n_threads = n_threads = i;
            break;
        }
    }

    printf("Servidor no ar.\n");
    printf("  canal de requisicoes ... %s\n", CANAL_REQUISICOES);
    printf("  threads no pool ........ %d\n", n_threads);
    printf("  fila de tarefas ........ %d posicoes\n", capacidade_fila);
    printf("  custo por operacao ..... %u us\n", custo_us);
    printf("  registros carregados ... %d de '%s'\n", carregados, caminho_dados);
    printf("Encerre com o comando SHUTDOWN (cliente --encerrar).\n\n");
    fflush(stdout);
    log_escrever("servidor iniciado com %d threads, fila de %d, custo %u us",
                 n_threads, capacidade_fila, custo_us);

    /* Laco da thread principal: so IPC. Le uma requisicao e a entrega ao pool. */
    while (canal_ler(canal_requisicoes, &requisicao, sizeof(requisicao))) {
        Comando espiada;

        requisicao.texto[MAX_TEXTO - 1] = '\0';
        requisicao.canal_resposta[MAX_CANAL - 1] = '\0';

        if (total == 0)
            inicio_servico = relogio_agora_ms();
        total++;

        /* SHUTDOWN e tratado aqui, e nao no pool: a thread principal precisa
         * parar de ler o canal, e o cliente ainda merece uma resposta. */
        espiada = protocolo_interpretar(requisicao.texto);
        if (espiada.tipo == CMD_SHUTDOWN) {
            Resposta resposta;
            memset(&resposta, 0, sizeof(resposta));
            resposta.id_requisicao = requisicao.id_requisicao;
            resposta.id_cliente = requisicao.id_cliente;
            resposta.thread = -1;
            resposta.sucesso = 1;
            snprintf(resposta.texto, sizeof(resposta.texto), "OK servidor encerrando");
            responder(&servidor, &requisicao, &resposta);
            total--;   /* o SHUTDOWN nao conta como requisicao de banco */
            log_escrever("SHUTDOWN recebido do cliente %d", requisicao.id_cliente);
            break;
        }

        if (!fila_enfileirar(servidor.fila, &requisicao))
            break;
    }

    /* Encerramento ordenado: para a fila, espera o pool terminar o que ja
     * estava enfileirado e so entao mexe no banco sem sincronizacao. */
    fila_encerrar(servidor.fila, n_threads);
    for (i = 0; i < n_threads; i++)
        pthread_join(threads[i], NULL);
    fim_servico = relogio_agora_ms();

    {
        int salvos = banco_salvar(servidor.banco, caminho_dados);
        if (salvos >= 0)
            printf("\n%d registro(s) gravado(s) em '%s'.\n", salvos, caminho_dados);
        else
            fprintf(stderr, "\naviso: nao foi possivel gravar '%s'\n", caminho_dados);
    }

    imprimir_resumo(&servidor, fim_servico - inicio_servico, total, custo_us, caminho_csv);
    log_escrever("servidor encerrado apos %lu requisicoes", total);

    fechar_destinos(&servidor);
    canal_fechar(canal_requisicoes);
    pthread_mutex_destroy(&servidor.mutex_destinos);
    fila_destruir(servidor.fila);
    banco_destruir(servidor.banco);
    log_encerrar();
    return 0;
}
