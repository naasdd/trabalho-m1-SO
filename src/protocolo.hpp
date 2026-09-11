#ifndef PROTOCOLO_HPP
#define PROTOCOLO_HPP

// ============================================================
// protocolo.hpp
// ------------------------------------------------------------
// Define o "idioma" que cliente e servidor usam para conversar
// atraves do FIFO (IPC). Como sao dois processos separados,
// precisamos de um formato de mensagem combinado previamente.
//
// Formato do PEDIDO (cliente -> servidor):
//   <pid>|<operacao>|<id>|<nome>
//   Exemplo: "1234|INSERT|7|Joao"
//
// Formato da RESPOSTA (servidor -> cliente):
//   <status>|<id>|<nome ou mensagem>
//   Exemplo: "OK|7|Joao"  ou  "ERR|0|registro nao encontrado"
//
// Por que texto? Porque podemos testar manualmente com
// "echo ... > /tmp/db_req" e ler as respostas com "cat",
// o que facilita muito a demonstracao e a depuracao.
// ============================================================

#include <string>
#include <sstream>
#include <vector>

namespace proto {

// Caminho do FIFO "caixa de entrada" de pedidos.
// O servidor cria e le deste FIFO; os clientes escrevem nele.
// Fica em /tmp porque e um diretorio acessivel por qualquer processo.
inline const char* FIFO_PEDIDOS = "/tmp/db_req";

// Prefixo do FIFO de resposta de cada cliente.
// Cada cliente cria o seu proprio: /tmp/db_resp_<pid>
// Assim varios clientes podem esperar resposta ao mesmo tempo
// sem misturar as respostas (cada um tem sua "mesa" no restaurante).
inline std::string fifoResposta(int pid) {
    return std::string("/tmp/db_resp_") + std::to_string(pid);
}

// Tamanho maximo de uma linha de mensagem.
constexpr size_t TAM_MAX_MSG = 256;

// Operacoes suportadas pelo banco (as 4 exigidas no enunciado).
enum class Op { INSERT, SELECT, UPDATE, DELETE, INVALIDA };

// ------------------------------------------------------------
// Estrutura de um pedido ja interpretado (depois do parsing).
// ------------------------------------------------------------
struct Pedido {
    int pid;        // PID do cliente (usado para achar o FIFO de resposta)
    Op operacao;    // INSERT, SELECT, UPDATE ou DELETE
    int id;         // id do registro (0 se nao se aplica)
    std::string nome; // nome (vazio para SELECT/DELETE)
};

// Converte string da operacao para o enum.
// Converte a operacao para maiusculas antes de comparar.
// Assim "select", "Select" e "SELECT" sao aceitos igualmente
// (robustez: o usuario nao deve decorar maiusculas/minusculas).
inline Op parseOp(const std::string& s) {
    std::string mai = s;
    for (char& c : mai) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    if (mai == "INSERT") return Op::INSERT;
    if (mai == "SELECT") return Op::SELECT;
    if (mai == "UPDATE") return Op::UPDATE;
    if (mai == "DELETE") return Op::DELETE;
    return Op::INVALIDA;
}

// ------------------------------------------------------------
// Extrai o PID de uma linha de pedido MESMO MAL FORMATADA.
// Usado pelo servidor para avisar o cliente (com uma resposta
// ERR) quando o pedido nao pode ser processado — sem isso, o
// cliente ficaria esperando ate o timeout.
// ------------------------------------------------------------
inline int extrairPid(const std::string& linha) {
    size_t pos = linha.find('|');
    std::string primeiro = (pos == std::string::npos) ? linha : linha.substr(0, pos);
    try {
        return std::stoi(primeiro);
    } catch (...) {
        return -1; // nao da nem pra saber quem mandou
    }
}

// ------------------------------------------------------------
// Faz o parsing de uma linha de pedido.
// Retorna true se a linha esta no formato valido.
// ------------------------------------------------------------
inline bool parsePedido(const std::string& linha, Pedido& p) {
    // Divide a linha nos campos separados por '|'
    std::vector<std::string> campos;
    std::stringstream ss(linha);
    std::string campo;
    while (std::getline(ss, campo, '|')) campos.push_back(campo);

    if (campos.size() < 3) return false; // minimo: pid|op|id

    try {
        p.pid = std::stoi(campos[0]);
        p.operacao = parseOp(campos[1]);
        p.id = std::stoi(campos[2]);
    } catch (...) {
        return false; // pid ou id nao eram numeros
    }

    // nome so existe para INSERT e UPDATE
    p.nome = (campos.size() >= 4) ? campos[3] : "";

    // Validacoes basicas por operacao
    switch (p.operacao) {
        case Op::INSERT:
        case Op::UPDATE:
            return !p.nome.empty() && p.id > 0; // precisam de id e nome
        case Op::SELECT:
        case Op::DELETE:
            return p.id > 0;                    // precisam so do id
        default:
            return false;
    }
}

// Monta a string de resposta para enviar de volta ao cliente.
inline std::string montarResposta(const std::string& status, int id,
                                  const std::string& info) {
    return status + "|" + std::to_string(id) + "|" + info + "\n";
}

} // namespace proto

#endif // PROTOCOLO_HPP
