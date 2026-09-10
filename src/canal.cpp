/*
 * canal.cpp - IPC com named pipe do Windows.
 *
 * Named pipe e o equivalente Windows do FIFO nomeado do POSIX: um canal com
 * nome, mantido pelo kernel, com as duas pontas em processos diferentes.
 * Quem cria (CreateNamedPipe) espera a conexao e le; quem abre (CreateFile)
 * escreve.
 */
#include "comum.hpp"

#include <windows.h>

namespace {

/* 64 KB: o cliente envia varias requisicoes antes de ler as respostas, entao
 * as respostas prontas ficam esperando aqui dentro. */
constexpr DWORD TAM_BUFFER = 65536;

std::string ultimo_erro;

void guardarErro(const std::string &contexto)
{
    ultimo_erro = contexto + ": erro " + std::to_string(GetLastError());
}

std::string caminho(const std::string &nome)
{
    return "\\\\.\\pipe\\" + nome;
}

}  // namespace

namespace canal {

void *criar(const std::string &nome)
{
    HANDLE h = CreateNamedPipeA(caminho(nome).c_str(),
                                PIPE_ACCESS_DUPLEX,
                                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                PIPE_UNLIMITED_INSTANCES,
                                TAM_BUFFER, TAM_BUFFER, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        guardarErro("CreateNamedPipe");
        return nullptr;
    }
    return h;
}

void *abrir(const std::string &nome, int timeout_ms)
{
    for (int esperado = 0; ; esperado += 50) {
        HANDLE h = CreateFileA(caminho(nome).c_str(), GENERIC_WRITE, 0,
                               nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE)
            return h;

        /* O outro processo pode ainda nao ter criado o canal. */
        if (GetLastError() != ERROR_FILE_NOT_FOUND || esperado >= timeout_ms) {
            guardarErro("CreateFile");
            return nullptr;
        }
        Sleep(50);
    }
}

bool aguardarConexao(void *c)
{
    HANDLE h = static_cast<HANDLE>(c);

    if (ConnectNamedPipe(h, nullptr))
        return true;

    /* O outro processo pode ter se conectado entre a criacao do canal e esta
     * chamada; nesse caso a conexao ja esta feita e nao ha erro nenhum. */
    if (GetLastError() == ERROR_PIPE_CONNECTED)
        return true;

    guardarErro("ConnectNamedPipe");
    return false;
}

bool ler(void *c, Mensagem &mensagem)
{
    HANDLE h = static_cast<HANDLE>(c);
    char *destino = reinterpret_cast<char *>(&mensagem);
    DWORD lidos = 0;

    /* ReadFile pode devolver menos bytes do que o pedido, entao o laco insiste
     * ate completar a mensagem inteira. */
    while (lidos < sizeof(Mensagem)) {
        DWORD agora = 0;
        if (!ReadFile(h, destino + lidos, static_cast<DWORD>(sizeof(Mensagem) - lidos),
                      &agora, nullptr) || agora == 0) {
            guardarErro("ReadFile");
            return false;
        }
        lidos += agora;
    }
    return true;
}

bool escrever(void *c, const Mensagem &mensagem)
{
    HANDLE h = static_cast<HANDLE>(c);
    const char *origem = reinterpret_cast<const char *>(&mensagem);
    DWORD escritos = 0;

    while (escritos < sizeof(Mensagem)) {
        DWORD agora = 0;
        if (!WriteFile(h, origem + escritos, static_cast<DWORD>(sizeof(Mensagem) - escritos),
                       &agora, nullptr)) {
            guardarErro("WriteFile");
            return false;
        }
        escritos += agora;
    }
    return true;
}

void fechar(void *c)
{
    if (c != nullptr)
        CloseHandle(static_cast<HANDLE>(c));
}

std::string ultimoErro()
{
    return ultimo_erro.empty() ? "sem erro registrado" : ultimo_erro;
}

}  // namespace canal
