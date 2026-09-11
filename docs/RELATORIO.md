# Sistema de Processamento Paralelo de Requisições a um Banco de Dados com IPC e Threads

**Autores:** [Nome Completo 1], [Nome Completo 2], [Nome Completo 3]
**Universidade:** Universidade do Vale do Itajaí (UNIVALI)
**Disciplina:** Sistemas Operacionais
**Professor:** [Nome do Professor]
**Data:** [Data de entrega]

---

## Resumo

Este trabalho apresenta a implementação de um sistema cliente-servidor que simula um gerenciador de requisições a um banco de dados, utilizando os conceitos de Comunicação Entre Processos (IPC), threads e paralelismo. O sistema é composto por dois processos independentes: um cliente que envia requisições (INSERT, SELECT, UPDATE, DELETE) via IPC e um servidor que processa essas requisições em paralelo utilizando um pool de threads. O acesso à estrutura de dados compartilhada é protegido por mecanismos de exclusão mútua (mutex e shared_mutex), garantindo a integridade dos dados. Foram realizados experimentos para avaliar o desempenho do sistema variando o número de threads, demonstrando os ganhos e limitações do paralelismo em cenários de leitura e escrita.

**Palavras-chave:** Sistemas Operacionais, IPC, Threads, Paralelismo, Exclusão Mútua, FIFO

---

## 1. Introdução

### 1.1 Contexto

Em sistemas de banco de dados reais, múltiplos clientes enviam requisições simultaneamente que precisam ser processadas de forma eficiente e segura. O processamento paralelo permite que várias operações sejam executadas ao mesmo tempo, melhorando o desempenho do sistema. No entanto, o acesso concorrente a dados compartilhados pode causar condições de corrida e inconsistências, exigindo mecanismos de sincronização.

### 1.2 Problema

O desafio consiste em desenvolver um sistema que:
- Permita a comunicação entre processos independentes (cliente e servidor)
- Processe requisições em paralelo utilizando threads
- Garanta a integridade dos dados através de exclusão mútua
- Suporte operações de banco de dados (INSERT, SELECT, UPDATE, DELETE)

### 1.3 Objetivos

- Implementar comunicação entre processos usando IPC (FIFO nomeado)
- Criar um pool de threads para processamento paralelo de requisições
- Proteger o acesso a dados compartilhados com mutex/semaphore
- Avaliar o desempenho do sistema com diferentes números de threads
- Compreender na prática os conceitos de concorrência e paralelismo

---

## 2. Fundamentação Teórica

### 2.1 Comunicação Entre Processos (IPC)

IPC (Inter-Process Communication) refere-se aos mecanismos que permitem que processos independentes troquem dados. Processos possuem espaços de memória isolados, portanto não podem compartilhar variáveis diretamente.

**FIFO (First In, First Out) ou Named Pipe:** É um tipo especial de arquivo que permite comunicação unidirecional entre processos não relacionados. Diferente de pipes anônimos (que só funcionam entre processos pai-filho), FIFOs possuem um nome no sistema de arquivos, permitindo que qualquer processo os acesse.

### 2.2 Threads

Threads são unidades de execução dentro de um processo que compartilham o mesmo espaço de memória. Permitem paralelismo real em sistemas multicore, pois múltiplas threads podem executar simultaneamente em diferentes núcleos do processador.

**Pool de Threads:** Ao invés de criar e destruir threads para cada tarefa (operação custosa), um pool mantém um conjunto fixo de threads reutilizáveis que aguardam e processam tarefas de uma fila.

### 2.3 Sincronização

**Mutex (Mutual Exclusion):** Mecanismo que garante que apenas uma thread por vez acesse uma região crítica (código que manipula dados compartilhados).

**Condition Variable:** Permite que threads durmam enquanto aguardam uma condição (ex: fila não vazia), evitando busy waiting (loop que consome CPU verificando repetidamente).

**Shared Mutex (Readers-Writer Lock):** Permite múltiplas leituras simultâneas (operações que não modificam dados) mas apenas uma escrita por vez (operações que modificam dados). Otimiza cenários onde leituras são mais frequentes que escritas.

### 2.4 Condição de Corrida

Ocorre quando múltiplas threads acessam dados compartilhados simultaneamente e pelo menos uma delas modifica os dados, podendo causar inconsistências ou corrupção de dados.

---

## 3. Arquitetura do Sistema

### 3.1 Visão Geral

O sistema é composto por dois processos independentes:

```
┌─────────┐      FIFO       ┌──────────┐      Fila      ┌─────────┐
│ Cliente │ ───────────────> │ Servidor │ ─────────────> │  Pool   │
│  (N)    │   /tmp/db_req    │  (main)  │   (interna)    │ Threads │
└─────────┘                  └──────────┘                └─────────┘
     ▲                                                        │
     │           FIFO privado (/tmp/db_resp_<pid>)            │
     └────────────────────────────────────────────────────────┘
                              │
                              ▼
                    ┌──────────────────┐
                    │  Banco de Dados  │
                    │  (shared_mutex)  │
                    └──────────────────┘
```

### 3.2 Componentes

#### 3.2.1 Processo Cliente
- Cria um FIFO privado para receber respostas (`/tmp/db_resp_<pid>`)
- Envia requisições ao servidor via FIFO compartilhado (`/tmp/db_req`)
- Aguarda e exibe respostas
- Suporta modo interativo e modo benchmark

#### 3.2.2 Processo Servidor
- **Thread Principal:** Lê requisições do FIFO e as enfileira
- **Pool de Threads:** N threads trabalhadoras que:
  - Retiram requisições da fila (protegida por mutex + condition variable)
  - Processam operações no banco de dados
  - Enviam respostas ao FIFO privado do cliente

#### 3.2.3 Banco de Dados
- Vetor em memória de registros `{id, nome}`
- Protegido por `shared_mutex`:
  - SELECT: leitura compartilhada (múltiplas threads simultâneas)
  - INSERT/UPDATE/DELETE: escrita exclusiva (uma thread por vez)
- Persistência em arquivo `banco.txt`

### 3.3 Protocolo de Comunicação

**Requisição (Cliente → Servidor):**
```
<pid>|<operacao>|<id>|<nome>
Exemplo: "1234|INSERT|7|Joao"
```

**Resposta (Servidor → Cliente):**
```
<status>|<id>|<info>
Exemplo: "OK|7|Joao" ou "ERR|7|nao encontrado"
```

---

## 4. Implementação

### 4.1 Tecnologias Utilizadas

- **Linguagem:** C++17
- **IPC:** FIFOs nomeados (POSIX: `mkfifo`, `open`, `read`, `write`)
- **Threads:** `std::thread`
- **Sincronização:** `std::mutex`, `std::condition_variable`, `std::shared_mutex`
- **Compilação:** Makefile com flags `-std=c++17 -pthread`

### 4.2 Estrutura de Arquivos

```
trabalho-m1-so/
├── Makefile              # Automação de compilação
├── README.md             # Documentação de uso
├── banco.txt             # Persistência de dados
├── src/
│   ├── protocolo.hpp     # Formato das mensagens IPC
│   ├── banco.hpp/.cpp    # CRUD + shared_mutex + persistência
│   ├── pool.hpp/.cpp     # Pool de threads + fila
│   ├── servidor.cpp      # Processo servidor
│   └── cliente.cpp       # Processo cliente
└── docs/
    └── ROTEIRO_DEFESA.md # Guia de apresentação
```

### 4.3 Códigos Principais

#### 4.3.1 Pool de Threads (Trecho)

```cpp
void Pool::loopWorker() {
    while (true) {
        Tarefa tarefa;
        {
            unique_lock lock(mutex_);
            condicao_.wait(lock, [&] { 
                return parar_ || !fila_.empty(); 
            });
            if (parar_ && fila_.empty()) return;
            tarefa = move(fila_.front());
            fila_.pop();
        } // Mutex liberado aqui
        tarefa(); // Executa FORA da região crítica
    }
}
```

**Explicação:** O worker dorme na condition variable até haver trabalho. Ao acordar, pega a tarefa com o mutex travado, libera o mutex e então executa a tarefa. Isso permite que múltiplas threads executem tarefas em paralelo.

#### 4.3.2 Proteção do Banco de Dados (Trecho)

```cpp
// Leitura: lock compartilhado (várias threads simultâneas)
optional<Registro> Banco::buscar(int id) {
    shared_lock lock(mutex_);
    auto it = find_if(tabela_.begin(), tabela_.end(),
                      [&](const Registro& r){ return r.id == id; });
    return (it != tabela_.end()) ? optional<Registro>(*it) : nullopt;
}

// Escrita: lock exclusivo (apenas uma thread)
bool Banco::inserir(int id, const string& nome) {
    unique_lock lock(mutex_);
    auto it = find_if(...);
    if (it != tabela_.end()) return false;
    tabela_.push_back({id, nome});
    return true;
}
```

**Explicação:** SELECTs podem executar em paralelo (shared_lock), mas escritas são serializadas (unique_lock), garantindo integridade sem sacrificar desempenho em leituras.

---

## 5. Resultados

### 5.1 Metodologia

Foram realizados experimentos variando o número de threads do servidor (1, 2, 4, 8) e o tipo de operação (SELECT e UPDATE). Cada configuração foi executada 5 vezes e calculada a média.

**Ambiente de Testes:**
- Processador: [especificar]
- Sistema Operacional: macOS
- Número de requisições: 4000 (modo burst) / 1000 (modo sequencial)

### 5.2 Resultados - Modo Burst (Throughput)

**Tabela 1: Tempo total para 4000 requisições (média de 5 execuções)**

| Threads | SELECT (ms) | UPDATE (ms) |
|---------|-------------|-------------|
| 1       | 76.9        | 97.7        |
| 2       | 71.8        | 98.8        |
| 4       | 72.1        | 87.9        |
| 8       | 66.0        | 83.7        |

**Gráfico 1: Tempo de execução vs Número de Threads (SELECT)**
```
Tempo (ms)
80 |●
75 |  ●
70 |     ●
65 |        ●
   +------------------
     1    2    4    8  Threads
```

**Gráfico 2: Tempo de execução vs Número de Threads (UPDATE)**
```
Tempo (ms)
100|●  ●
 95|
 90|      ●
 85|         ●
   +------------------
     1   2   4   8  Threads
```

### 5.3 Resultados - Modo Sequencial (Latência)

**Tabela 2: Tempo total para 1000 requisições sequenciais (média de 3 execuções)**

| Threads | SELECT (ms) | Tempo/requisição (ms) |
|---------|-------------|----------------------|
| 1       | 11716.9     | 11.72                |
| 2       | 11790.2     | 11.79                |
| 4       | 12136.8     | 12.14                |
| 8       | 12043.7     | 12.04                |

---

## 6. Análise e Discussão

### 6.1 Throughput vs Latência

Os resultados mostram comportamentos distintos:

**Modo Burst (paralelismo efetivo):**
- SELECT: melhoria de ~14% de 1 para 8 threads (76.9ms → 66.0ms)
- UPDATE: melhoria de ~14% de 1 para 8 threads (97.7ms → 83.7ms)
- O ganho é modesto porque as operações são muito rápidas (busca em vetor pequeno)

**Modo Sequencial (sem paralelismo):**
- Não há diferença significativa entre 1 e 8 threads (~12ms/requisição)
- O gargalo é a latência do IPC (ida e volta via FIFO), não o processamento
- Um único cliente sequencial não gera trabalho paralelo suficiente

### 6.2 Leituras vs Escritas

- UPDATEs são consistentemente mais lentos que SELECTs (~25-30%)
- Isso ocorre porque escritas exigem lock exclusivo, serializando o acesso
- SELECTs se beneficiam do shared_mutex, permitindo leituras paralelas

### 6.3 Limitações e Trade-offs

1. **Operações muito rápidas:** Como o banco é um vetor pequeno em memória, o tempo de processamento é desprezível comparado ao overhead de IPC e sincronização.

2. **Gargalo do IPC:** A comunicação via FIFO tem latência significativa (~12ms por requisição no modo sequencial), limitando o throughput máximo.

3. **Shared_mutex:** Demonstra seu valor em cenários com muitas leituras, mas o benefício seria mais evidente com um banco maior e operações mais custosas.

### 6.4 Aprendizados

- O paralelismo só compensa quando há trabalho suficiente para dividir
- A escolha correta de primitivas de sincronização (mutex vs shared_mutex) impacta o desempenho
- Condições de corrida foram evitadas através de locks apropriados
- O padrão produtor-consumidor (fila + pool) é eficaz para desacoplar chegada e processamento

---

## 7. Conclusão

O sistema implementado demonstra com sucesso os conceitos de IPC, threads e paralelismo em um cenário realista de banco de dados. A comunicação via FIFO nomeado permite que processos independentes troquem dados de forma eficiente. O pool de threads com fila protegida por mutex e condition variable implementa o padrão produtor-consumidor clássico. O uso de shared_mutex otimiza o acesso ao banco, permitindo leituras paralelas.

Os experimentos mostraram que o ganho de desempenho com paralelismo depende do tipo de operação e da carga de trabalho. Em cenários de alta concorrência (modo burst), múltiplas threads reduzem o tempo total em aproximadamente 14%. Já em cenários sequenciais, o gargalo é a latência do IPC, não o processamento.

O trabalho permitiu compreender na prática os desafios de programação concorrente: sincronização, condições de corrida, deadlocks e trade-offs entre diferentes mecanismos de exclusão mútua.

---

## 8. Referências

- TANENBAUM, A. S.; BOS, H. **Modern Operating Systems**. 4. ed. Pearson, 2014.
- SILBERSCHATZ, A.; GALVIN, P. B.; GAGNE, G. **Operating System Concepts**. 10. ed. Wiley, 2018.
- Documentação POSIX Threads (pthreads). Disponível em: https://pubs.opengroup.org/onlinepubs/7908799/xsh/pthread.h.html
- Documentação C++ Thread Support Library. Disponível em: https://en.cppreference.com/w/cpp/thread

---

## Apêndice A - Código-Fonte Completo

O código-fonte completo está disponível no repositório:
https://github.com/naasdd/trabalho-m1-SO (branch `jose`)

## Apêndice B - Comandos de Execução

```bash
# Compilar
make

# Executar servidor (4 threads)
./servidor 4

# Executar cliente interativo
./cliente

# Benchmark burst (throughput)
./cliente -b 4000 SELECT 1

# Benchmark sequencial (latência)
./cliente -n 1000 SELECT 1
```
