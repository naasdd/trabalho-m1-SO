/*
 * banco.h - cabecalho unico do trabalho, dividido em duas partes:
 *
 *   PARTE 1 - IPC: o formato da mensagem e o canal entre os dois processos.
 *             Usada por cliente.cpp e servidor.cpp.
 *   PARTE 2 - BANCO: a tabela compartilhada, o mutex que a protege e as
 *             operacoes. Usada so por servidor.cpp.
 *
 * As duas partes estao no mesmo arquivo porque cliente e servidor precisam
 * combinar exatamente o mesmo formato de mensagem: se cada um tivesse a sua
 * copia da struct e uma delas mudasse, os dois processos passariam a
 * interpretar os mesmos bytes de maneiras diferentes, e o erro so apareceria
 * em tempo de execucao.
 */
#ifndef BANCO_H
#define BANCO_H

#include <windows.h>
#include <pthread.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

/* ================================================================== */
/* PARTE 1 - COMUNICACAO ENTRE PROCESSOS (IPC)                        */
/* ==================================================================
 *
 * Mensagem atravessa a fronteira entre dois processos, entao e uma struct de
 * tamanho fixo, com vetor de char em vez de std::string: o que viaja pelo
 * canal e uma sequencia de bytes, e um std::string guarda um ponteiro para
 * memoria que so existe dentro do processo que o criou.
 *
 * O canal e um named pipe do Windows, o equivalente do FIFO nomeado do POSIX:
 * um canal com nome, mantido pelo kernel, com as duas pontas em processos
 * diferentes. E IPC de verdade do sistema operacional - a leitura bloqueia
 * enquanto nao ha o que ler e nenhum dos dois processos enxerga a memoria do
 * outro. Quem cria o canal fica com a ponta de leitura; quem abre, com a de
 * escrita.
 */

constexpr int MAX_TEXTO = 200;

struct Mensagem {
    int  id;                /* numero da requisicao, repetido na resposta */
    int  thread;            /* qual thread do pool atendeu (-1 na requisicao) */
    char texto[MAX_TEXTO];  /* o comando, ou o resultado dele */
};

/* Os dois canais usados pelo sistema: um para ir, outro para voltar. */
constexpr const char *CANAL_REQUISICOES = "sgbd_requisicoes";
constexpr const char *CANAL_RESPOSTAS   = "sgbd_respostas";

/* 64 KB: no modo paralelo o cliente envia varias requisicoes antes de ler as
 * respostas, entao as respostas prontas ficam esperando dentro do canal. */
constexpr DWORD TAM_BUFFER = 65536;

inline std::string ultimo_erro;

inline void guardarErro(const std::string &contexto)
{
    ultimo_erro = contexto + ": erro " + std::to_string(GetLastError());
}

inline std::string ultimoErro()
{
    return ultimo_erro.empty() ? "sem erro registrado" : ultimo_erro;
}

inline std::string caminhoDoCanal(const std::string &nome)
{
    return "\\\\.\\pipe\\" + nome;
}

/* Cria o canal e assume a ponta de leitura. Devolve nullptr em caso de erro. */
inline HANDLE criarCanal(const std::string &nome)
{
    HANDLE canal = CreateNamedPipeA(caminhoDoCanal(nome).c_str(),
                                    PIPE_ACCESS_DUPLEX,
                                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                    PIPE_UNLIMITED_INSTANCES,
                                    TAM_BUFFER, TAM_BUFFER, 0, nullptr);
    if (canal == INVALID_HANDLE_VALUE) {
        guardarErro("CreateNamedPipe");
        return nullptr;
    }
    return canal;
}

/* Abre um canal existente para escrita, esperando ate timeout_ms por ele. */
inline HANDLE abrirCanal(const std::string &nome, int timeout_ms)
{
    for (int esperado = 0; ; esperado += 50) {
        HANDLE canal = CreateFileA(caminhoDoCanal(nome).c_str(), GENERIC_WRITE, 0,
                                   nullptr, OPEN_EXISTING, 0, nullptr);
        if (canal != INVALID_HANDLE_VALUE)
            return canal;

        /* O outro processo pode ainda nao ter criado o canal. */
        if (GetLastError() != ERROR_FILE_NOT_FOUND || esperado >= timeout_ms) {
            guardarErro("CreateFile");
            return nullptr;
        }
        Sleep(50);
    }
}

/* Quem criou o canal chama isto para esperar o outro processo se conectar. */
inline bool aguardarConexao(HANDLE canal)
{
    if (ConnectNamedPipe(canal, nullptr))
        return true;

    /* O outro processo pode ter se conectado entre a criacao do canal e esta
     * chamada; nesse caso a conexao ja esta feita e nao ha erro nenhum. */
    if (GetLastError() == ERROR_PIPE_CONNECTED)
        return true;

    guardarErro("ConnectNamedPipe");
    return false;
}

/* Le exatamente uma Mensagem. ReadFile pode devolver menos bytes do que o
 * pedido, entao o laco insiste ate completar a mensagem inteira. */
inline bool lerMensagem(HANDLE canal, Mensagem &mensagem)
{
    char *destino = reinterpret_cast<char *>(&mensagem);
    DWORD lidos = 0;

    while (lidos < sizeof(Mensagem)) {
        DWORD agora = 0;
        if (!ReadFile(canal, destino + lidos, static_cast<DWORD>(sizeof(Mensagem) - lidos),
                      &agora, nullptr) || agora == 0) {
            guardarErro("ReadFile");
            return false;
        }
        lidos += agora;
    }
    return true;
}

inline bool escreverMensagem(HANDLE canal, const Mensagem &mensagem)
{
    const char *origem = reinterpret_cast<const char *>(&mensagem);
    DWORD escritos = 0;

    while (escritos < sizeof(Mensagem)) {
        DWORD agora = 0;
        if (!WriteFile(canal, origem + escritos,
                       static_cast<DWORD>(sizeof(Mensagem) - escritos), &agora, nullptr)) {
            guardarErro("WriteFile");
            return false;
        }
        escritos += agora;
    }
    return true;
}

inline void fecharCanal(HANDLE canal)
{
    if (canal != nullptr)
        CloseHandle(canal);
}

/* ================================================================== */
/* PARTE 2 - O BANCO DE DADOS SIMULADO                                */
/* ==================================================================
 *
 * Esta e a estrutura compartilhada do trabalho: todas as threads do pool do
 * servidor operam sobre a mesma tabela. O acesso e protegido por um mutex real
 * (pthread_mutex_t), que garante a exclusao mutua - so uma thread por vez
 * dentro da secao critica.
 *
 * O banco existe em dois lugares ao mesmo tempo:
 *   - em memoria, no vetor "tabela", que e onde as consultas acontecem;
 *   - em disco, no arquivo banco.txt, reescrito a cada alteracao para que os
 *     dados sobrevivam ao encerramento do servidor.
 *
 * O arquivo tambem e recurso compartilhado, e por isso a gravacao acontece
 * dentro da mesma secao critica da tabela: se duas threads reescrevessem
 * banco.txt ao mesmo tempo, o arquivo sairia corrompido.
 */

constexpr int MAX_NOME = 50;
constexpr const char *ARQUIVO_BANCO = "banco.txt";

struct Registro {
    int         id;
    std::string nome;
};

/* A tabela compartilhada e o mutex que a protege. */
inline std::vector<Registro> tabela;
inline pthread_mutex_t       mutex_tabela = PTHREAD_MUTEX_INITIALIZER;

/* ------------------------------------------------------------------ */
/* Persistencia em banco.txt (formato "id;nome" por linha)             */
/* ------------------------------------------------------------------ */

/* Chamadas sempre com o mutex ja travado. */
inline void salvarBanco()
{
    std::ofstream arquivo(ARQUIVO_BANCO, std::ios::trunc);
    for (const Registro &registro : tabela)
        arquivo << registro.id << ';' << registro.nome << '\n';
}

/* Chamada uma unica vez, na subida do servidor, antes de criar as threads. */
inline int carregarBanco()
{
    std::ifstream arquivo(ARQUIVO_BANCO);
    std::string linha;
    int carregados = 0;

    while (std::getline(arquivo, linha)) {
        if (!linha.empty() && linha.back() == '\r')
            linha.pop_back();
        const std::size_t separador = linha.find(';');
        if (separador == std::string::npos)
            continue;
        tabela.push_back(Registro{std::stoi(linha.substr(0, separador)),
                                  linha.substr(separador + 1)});
        ++carregados;
    }
    return carregados;
}

/* ------------------------------------------------------------------ */
/* Operacoes                                                           */
/* ------------------------------------------------------------------ */

/* Procura um id na tabela. Chamada sempre com o mutex ja travado. */
inline int procurar(int id)
{
    for (std::size_t i = 0; i < tabela.size(); ++i) {
        if (tabela[i].id == id)
            return static_cast<int>(i);
    }
    return -1;
}

/*
 * Interpreta e executa um comando, devolvendo a mensagem de resposta.
 *
 * Toda manipulacao da tabela acontece entre pthread_mutex_lock e
 * pthread_mutex_unlock: e a secao critica do trabalho. Sem ela, duas threads
 * inserindo ao mesmo tempo poderiam corromper o vetor, e um SELECT poderia ler
 * a tabela no meio de um DELETE.
 */
inline std::string executarComando(const char *comando)
{
    int  id = 0;
    char nome[MAX_NOME] = "";

    if (std::sscanf(comando, "INSERT id=%d nome='%49[^']'", &id, nome) == 2) {
        pthread_mutex_lock(&mutex_tabela);
        std::string resultado;
        if (procurar(id) >= 0) {
            resultado = "ERRO id " + std::to_string(id) + " ja existe";
        } else {
            tabela.push_back(Registro{id, nome});
            salvarBanco();
            resultado = "OK inserido id=" + std::to_string(id) + " nome='" + nome + "'";
        }
        pthread_mutex_unlock(&mutex_tabela);
        return resultado;
    }

    if (std::sscanf(comando, "SELECT nome WHERE id=%d", &id) == 1 ||
        std::sscanf(comando, "SELECT id=%d", &id) == 1) {
        pthread_mutex_lock(&mutex_tabela);
        const int posicao = procurar(id);
        std::string resultado = (posicao < 0)
            ? "OK 0 registros para id=" + std::to_string(id)
            : "OK id=" + std::to_string(id) + " nome='" + tabela[posicao].nome + "'";
        pthread_mutex_unlock(&mutex_tabela);
        return resultado;
    }

    if (std::sscanf(comando, "UPDATE id=%d nome='%49[^']'", &id, nome) == 2) {
        pthread_mutex_lock(&mutex_tabela);
        const int posicao = procurar(id);
        std::string resultado;
        if (posicao < 0) {
            resultado = "ERRO id " + std::to_string(id) + " nao encontrado";
        } else {
            tabela[posicao].nome = nome;
            salvarBanco();
            resultado = "OK atualizado id=" + std::to_string(id) + " nome='" + nome + "'";
        }
        pthread_mutex_unlock(&mutex_tabela);
        return resultado;
    }

    if (std::sscanf(comando, "DELETE WHERE id=%d", &id) == 1 ||
        std::sscanf(comando, "DELETE id=%d", &id) == 1) {
        pthread_mutex_lock(&mutex_tabela);
        const int posicao = procurar(id);
        std::string resultado;
        if (posicao < 0) {
            resultado = "ERRO id " + std::to_string(id) + " nao encontrado";
        } else {
            tabela.erase(tabela.begin() + posicao);
            salvarBanco();
            resultado = "OK removido id=" + std::to_string(id);
        }
        pthread_mutex_unlock(&mutex_tabela);
        return resultado;
    }

    if (std::strcmp(comando, "LISTAR") == 0) {
        pthread_mutex_lock(&mutex_tabela);
        std::string resultado = "OK " + std::to_string(tabela.size()) + " registro(s):";
        for (const Registro &registro : tabela)
            resultado += " [" + std::to_string(registro.id) + "," + registro.nome + "]";
        pthread_mutex_unlock(&mutex_tabela);
        return resultado.substr(0, MAX_TEXTO - 1);
    }

    return "ERRO comando invalido";
}

#endif /* BANCO_H */
