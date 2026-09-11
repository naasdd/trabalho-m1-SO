// ============================================================
// banco.cpp
// ------------------------------------------------------------
// Implementacao do banco simulado. Preste atencao nos dois
// tipos de "cadeado" usados:
//
//   std::shared_lock    -> cadeado de LEITURA: varias threads
//                          podem segura-lo ao mesmo tempo.
//   std::unique_lock    -> cadeado de ESCRITA: so uma thread
//                          pode segura-lo, e bloqueia leitores.
//
// Se esquecessemos os locks, duas threads fazendo INSERT ao
// mesmo tempo poderiam, por exemplo, realocar o vetor ao mesmo
// tempo -> crash ou dados corrompidos (condicao de corrida).
// ============================================================

#include "banco.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>

using namespace std;

// ------------------------------------------------------------
// Carrega o banco.txt para a memoria.
// Formato de cada linha: id;nome
// ------------------------------------------------------------
bool Banco::carregar(const string& caminho) {
    ifstream arq(caminho);
    if (!arq.is_open()) return false; // arquivo pode nem existir ainda

    tabela_.clear();
    string linha;
    while (getline(arq, linha)) {
        if (linha.empty()) continue;
        stringstream ss(linha);
        string sid, nome;
        if (getline(ss, sid, ';') && getline(ss, nome)) {
            try {
                tabela_.push_back({stoi(sid), nome});
            } catch (...) {
                // linha mal formatada: ignora
            }
        }
    }
    return true;
}

// ------------------------------------------------------------
// Salva a tabela de volta no banco.txt.
// Usa lock de leitura: so precisamos garantir que ninguem esta
// alterando a tabela enquanto lemos para gravar.
// ------------------------------------------------------------
bool Banco::salvar(const string& caminho) {
    shared_lock lock(mutex_);          // LEITURA
    ofstream arq(caminho, ios::trunc); // trunc = regrava do zero
    if (!arq.is_open()) return false;
    for (const auto& r : tabela_) {
        arq << r.id << ';' << r.nome << '\n';
    }
    return true;
}

// ------------------------------------------------------------
// INSERT — escrita: lock EXCLUSIVO.
// Enquanto este lock estiver travado, nenhuma outra thread le
// nem escreve na tabela.
// ------------------------------------------------------------
bool Banco::inserir(int id, const string& nome) {
    unique_lock lock(mutex_);          // ESCRITA (exclusiva)
    auto it = find_if(tabela_.begin(), tabela_.end(),
                      [&](const Registro& r){ return r.id == id; });
    if (it != tabela_.end()) return false; // id duplicado
    tabela_.push_back({id, nome});
    return true;
}

// ------------------------------------------------------------
// SELECT — leitura: lock COMPARTILHADO.
// Varias threads podem executar esta funcao simultaneamente.
// E aqui que esta o ganho de paralelismo: consultas nao se
// bloqueiam entre si.
// ------------------------------------------------------------
optional<Registro> Banco::buscar(int id) {
    shared_lock lock(mutex_);          // LEITURA (compartilhada)
    auto it = find_if(tabela_.begin(), tabela_.end(),
                      [&](const Registro& r){ return r.id == id; });
    if (it == tabela_.end()) return nullopt;
    return *it; // devolve uma copia do registro
}

// ------------------------------------------------------------
// UPDATE — escrita: lock EXCLUSIVO.
// ------------------------------------------------------------
bool Banco::atualizar(int id, const string& novoNome) {
    unique_lock lock(mutex_);          // ESCRITA
    auto it = find_if(tabela_.begin(), tabela_.end(),
                      [&](const Registro& r){ return r.id == id; });
    if (it == tabela_.end()) return false;
    it->nome = novoNome;
    return true;
}

// ------------------------------------------------------------
// DELETE — escrita: lock EXCLUSIVO.
// ------------------------------------------------------------
bool Banco::remover(int id) {
    unique_lock lock(mutex_);          // ESCRITA
    auto it = find_if(tabela_.begin(), tabela_.end(),
                      [&](const Registro& r){ return r.id == id; });
    if (it == tabela_.end()) return false;
    tabela_.erase(it);
    return true;
}

// ------------------------------------------------------------
// Quantidade de registros (leitura).
// ------------------------------------------------------------
size_t Banco::tamanho() {
    shared_lock lock(mutex_);          // LEITURA
    return tabela_.size();
}
