/*
 * comum.hpp - o que cliente e servidor precisam combinar entre si.
 *
 * Mensagem atravessa a fronteira entre dois processos, entao e uma struct de
 * tamanho fixo, com vetor de char em vez de std::string: o que viaja pelo
 * canal e uma sequencia de bytes, e um std::string guarda um ponteiro para
 * memoria que so existe dentro do processo que o criou.
 */
#ifndef COMUM_HPP
#define COMUM_HPP

#include <string>

constexpr int MAX_TEXTO = 200;

struct Mensagem {
    int  id;                  /* numero da requisicao, repetido na resposta */
    int  thread;              /* qual thread do pool atendeu (-1 na requisicao) */
    char texto[MAX_TEXTO];    /* o comando, ou o resultado dele */
};

/* Os dois canais nomeados usados pelo sistema. */
constexpr const char *CANAL_REQUISICOES = "sgbd_requisicoes";
constexpr const char *CANAL_RESPOSTAS   = "sgbd_respostas";

/*
 * Canal de comunicacao entre processos: um named pipe do Windows, que e o
 * equivalente do FIFO nomeado do POSIX. E IPC de verdade do sistema
 * operacional - o dado fica em um buffer do kernel, a leitura bloqueia
 * enquanto nao ha o que ler, e nenhum dos dois processos enxerga a memoria do
 * outro.
 *
 * Quem cria o canal fica com a ponta de leitura; quem abre, com a de escrita.
 */
namespace canal {

/* Cria o canal e assume a ponta de leitura. Devolve nullptr em caso de erro. */
void *criar(const std::string &nome);

/* Abre um canal existente para escrita, esperando ate timeout_ms por ele. */
void *abrir(const std::string &nome, int timeout_ms);

/* Quem criou o canal chama isto para esperar o outro processo se conectar. */
bool aguardarConexao(void *canal);

/* Le / escreve exatamente uma Mensagem. Devolvem false em caso de erro. */
bool ler(void *canal, Mensagem &mensagem);
bool escrever(void *canal, const Mensagem &mensagem);

void fechar(void *canal);

/* Descricao do ultimo erro, para as mensagens ao usuario. */
std::string ultimoErro();

}  // namespace canal

#endif /* COMUM_HPP */
