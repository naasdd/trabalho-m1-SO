#ifndef BANCO_HPP
#define BANCO_HPP

// ============================================================
// banco.hpp
// ------------------------------------------------------------
// "Banco de dados" simulado: um vetor de registros {id, nome}
// mantido na memoria do processo servidor.
//
// PONTO CENTRAL DO TRABALHO: varias threads do pool acessam
// essa tabela AO MESMO TEMPO. Para evitar condicao de corrida
// (duas threads mexendo no mesmo dado simultaneamente e
// corrompendo o vetor), todo acesso passa por um
// std::shared_mutex:
//
//   - Leituras (SELECT): usam shared_lock -> varias threads
//     podem ler em paralelo, sem se bloquear.
//   - Escritas (INSERT/UPDATE/DELETE): usam unique_lock ->
//     apenas UMA thread escreve, e nenhuma le durante a escrita.
//
// Isso implementa o padrao "leitores-escritores"
// (readers-writer lock), exigencia do enunciado de usar
// mutex/semaforo para proteger a estrutura compartilhada.
// ============================================================

#include <string>
#include <vector>
#include <shared_mutex>
#include <optional>

// Estrutura do registro, conforme sugerido no enunciado:
//   typedef struct { int id; char nome[50]; } Registro;
// Usamos std::string por comodidade, mas o conceito e o mesmo.
struct Registro {
    int id;
    std::string nome;
};

class Banco {
public:
    // Carrega os registros do arquivo texto (banco.txt).
    // Formato de cada linha: id;nome
    // Chamado UMA vez, na inicializacao do servidor (antes de
    // qualquer thread existir, entao nao precisa de lock aqui).
    bool carregar(const std::string& caminho);

    // Salva todos os registros de volta no arquivo texto.
    // Chamado no encerramento do servidor (apos as threads pararem).
    bool salvar(const std::string& caminho);

    // ---- Operacoes CRUD (chamadas pelas worker threads) ----

    // INSERT: adiciona registro. Falha se o id ja existe. (escrita)
    bool inserir(int id, const std::string& nome);

    // SELECT: busca pelo id. (leitura -> varias threads juntas)
    std::optional<Registro> buscar(int id);

    // UPDATE: altera o nome do registro com o id dado. (escrita)
    bool atualizar(int id, const std::string& novoNome);

    // DELETE: remove o registro com o id dado. (escrita)
    bool remover(int id);

    // Quantidade atual de registros (para logs/estatisticas).
    size_t tamanho();

private:
    std::vector<Registro> tabela_;   // a tabela em memoria
    std::shared_mutex mutex_;        // protege tabela_ (leitores/escritor)
};

#endif // BANCO_HPP
