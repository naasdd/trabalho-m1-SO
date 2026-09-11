# Guia de Estudos — Trabalho M1 (Sistemas Operacionais)

Este documento explica **TUDO** que você precisa saber para entender e defender o trabalho. Começa do absoluto zero e vai até os detalhes mais técnicos da implementação.

---

## PARTE 1: O PROBLEMA (O que o professor pediu?)

### 1.1 O Enunciado em Linguagem Simples

O professor quer que você construa um "banco de dados de mentira" que funciona assim:

1. **Um programa cliente** envia pedidos tipo "adiciona o João", "busca o id 5", "apaga o 7"
2. **Um programa servidor** recebe esses pedidos e processa em paralelo (várias ao mesmo tempo)
3. Os dois programas conversam por um "cano de comunicação" (IPC)
4. O banco de dados é um vetor na memória, protegido por "trancas" (mutex) pra não dar problema

**Por que isso é importante?** Porque é assim que bancos de dados reais funcionam! Quando você acessa o MySQL, seu programa (cliente) manda pedidos pro servidor MySQL, que processa vários clientes ao mesmo tempo usando threads.

### 1.2 Os Requisitos Técnicos (o que não pode faltar)

O professor exige **explicitamente**:

✅ **Dois executáveis separados** — Não pode ser um programa fingindo ser dois. Tem que ser dois programas mesmo (dois processos).

✅ **IPC real** — Tem que usar um mecanismo de comunicação entre processos de verdade (FIFO, memória compartilhada, socket, etc.). Não pode usar variável global nem arquivo temporário com polling.

✅ **Pool de threads** — O servidor tem que ter várias threads processando em paralelo.

✅ **Mutex/Semaphore** — O acesso ao banco de dados tem que ser protegido contra condições de corrida.

---

## PARTE 2: CONCEITOS FUNDAMENTAIS (O básico)

### 2.1 O que é um Processo?

**Processo = um programa em execução.**

Quando você abre o Chrome e o Spotify, são dois processos. Cada um tem:
- Sua própria memória (espaço de endereçamento)
- Seus próprios recursos (arquivos abertos, etc)
- Um identificador único (PID = Process ID)

**Por que isso importa?** Porque processos **NÃO** compartilham memória automaticamente. Se o seu cliente tem uma variável `x = 10`, o servidor simplesmente **não vê** essa variável. Cada um vive na sua própria bolha de memória.

**Pra que os processos conversem**, precisamos de **IPC** (Inter-Process Communication).

### 2.2 O que é IPC?

**IPC = "Telefone" entre processos.**

São mecanismos que o Sistema Operacional fornece pra processos trocarem dados:

| Mecanismo | Como funciona | Quando usar |
|-----------|---------------|-------------|
| **FIFO (Named Pipe)** | Um "cano" com nome. Um escreve, outro lê. | Processos não relacionados (nosso caso!) |
| **Pipe Anônimo** | Um "cano" sem nome. | Só entre pai-filho (fork) |
| **Memória Compartilhada** | Uma área de memória que ambos veem | Quando precisa de alta velocidade |
| **Socket** | Conexão tipo rede | Cliente-servidor em máquinas diferentes |
| **Message Queue** | Fila de mensagens gerenciada pelo SO | Comunicação assíncrona |

**No nosso trabalho, usamos FIFO** (First In, First Out = primeiro a entrar, primeiro a sair).

### 2.3 O que é um FIFO?

**FIFO = um arquivo especial que funciona como um cano.**

Pense assim:
- É como uma caixa de correio com endereço fixo (`/tmp/db_req`)
- O cliente "deposita a carta" (escreve)
- O servidor "pega a carta" (lê)
- As cartas são entregues na ordem em que chegaram (FIFO)

**Por que FIFO e não arquivo comum?**
- Arquivo comum: você escreve e fecha. O outro tem que ficar reabrindo e lendo (polling = desperdício).
- FIFO: é um canal de comunicação. O leitor pode ficar esperando até chegar algo novo.

**Por que FIFO e não pipe anônimo?**
- Pipe anônimo só funciona entre processos "parentes" (tipo um programa que cria outro com `fork()`)
- FIFO tem um nome no sistema de arquivos, então qualquer processo pode abrir

**No nosso trabalho:**
- `/tmp/db_req` = FIFO compartilhado (todos os clientes escrevem aqui)
- `/tmp/db_resp_<pid>` = FIFO privado de cada cliente (pra receber resposta)

### 2.4 O que é uma Thread?

**Thread = um "trabalhador" dentro de um processo.**

Diferente de processos, threads **DENTRO do mesmo processo compartilham memória**. Então se você tem um vetor na memória do processo, TODAS as threads desse processo veem esse mesmo vetor.

**Analogia:** Um restaurante é um processo. Os cozinheiros são threads. Todos os cozinheiros compartilham a mesma cozinha (memória), mas cada um trabalha independente.

**Por que usar threads?**
- Processos são pesados (criar processo consome mais recursos)
- Threads são leves (compartilham memória, mais rápido de criar)
- Podem executar em paralelo em CPUs com múltiplos núcleos

**No nosso trabalho:**
- O **servidor** é um processo
- Dentro dele, tem **várias threads** (workers) processando pedidos em paralelo
- Todas as threads veem o mesmo banco de dados na memória

### 2.5 O que é Mutex?

**Mutex = "chave de banheiro" — só uma pessoa usa por vez.**

Como todas as threads do servidor veem o mesmo banco de dados, temos um problema: **e se dois workers tentarem inserir ao mesmo tempo?**

Sem proteção, pode acontecer:
- Worker A começa a inserir
- Worker B também começa a inserir (ao mesmo tempo)
- Os dois mexem no vetor ao mesmo tempo → **dados corrompidos ou crash**

Isso se chama **condição de corrida** (race condition).

**Solução: Mutex**
- Antes de mexer no banco, a thread "pega a chave" (lock)
- Depois de mexer, "devolve a chave" (unlock)
- Se outra thread tentar pegar a chave enquanto alguém está usando, ela **espera**

**Tipos de lock:**

| Tipo | Como funciona | Quando usar |
|------|---------------|-------------|
| **std::mutex** | Só uma thread por vez, sempre | Escritas (INSERT/UPDATE/DELETE) |
| **std::shared_mutex** (leitura) | Várias threads podem ler juntas | Leituras (SELECT) |
| **std::shared_mutex** (escrita) | Só uma thread escreve | Escritas |

**No nosso trabalho:**
- Usamos `shared_mutex` no banco de dados
- SELECT = `shared_lock` (várias threads podem ler ao mesmo tempo)
- INSERT/UPDATE/DELETE = `unique_lock` (só uma por vez)

### 2.6 O que é Condition Variable?

**Condition Variable = "campainha" pra avisar que tem trabalho.**

Imagine o loop de um worker:
```cpp
while (true) {
    if (fila tem trabalho?) {
        pega trabalho e executa
    }
}
```

Problema: esse loop fica rodando o tempo todo, mesmo quando não tem nada pra fazer = **desperdiça CPU**.

**Solução: Condition Variable**
- Worker dorme (não gasta CPU)
- Quando chega trabalho, alguém "toca a campainha" (notify)
- Worker acorda, pega o trabalho, executa, volta a dormir

**Como funciona junto com Mutex:**
1. Worker trava o mutex (pra mexer na fila)
2. Vê que a fila tá vazia
3. `condicao_.wait(mutex)` = dorme E libera o mutex (pra alguém poder colocar trabalho)
4. Quando acorda, re-trava o mutex e pega o trabalho

### 2.7 O que é Pool de Threads?

**Pool = equipe fixa de trabalhadores esperando pedidos.**

**Alternativa ruim:** Criar uma thread nova pra cada pedido e destruir depois.
- Criar/destruir thread é caro (demora)
- Se chegam 1000 pedidos, você cria e destrói 1000 threads

**Pool (a forma certa):**
- Cria N threads no início (ex: 4)
- Elas ficam esperando trabalho na fila
- Quando chega pedido, uma pega e executa
- Depois volta a esperar

**No nosso trabalho:**
- Pool tem N threads (configurável: `./servidor 4`)
- Fila protegida por mutex + condition variable
- Workers dormem quando não tem trabalho (não gastam CPU)

---

## PARTE 3: ARQUITETURA DO NOSSO SISTEMA

### 3.1 Visão Geral (Como tudo se conecta)

```
Cliente 1 ──┐
Cliente 2 ──┼─> /tmp/db_req (FIFO) ──> Servidor (main thread)
Cliente 3 ──┘                              │
                                           │ empilha na fila
                                           ▼
                                   ┌─────────────┐
                                   │ Fila interna│ (mutex + condition var)
                                   └─────────────┘
                                           │
                    ┌──────────────────────┼──────────────────────┐
                    ▼                      ▼                      ▼
              Worker Thread 1      Worker Thread 2        Worker Thread N
                    │                      │                      │
                    └──────────────────────┼──────────────────────┘
                                           ▼
                                  Banco de Dados
                                  (vetor + shared_mutex)
                                           │
                                           ▼
                                   Resposta ──> /tmp/db_resp_<pid>
                                           (FIFO privado do cliente)
```

### 3.2 Fluxo de uma Requisição

1. **Cliente escreve:** `"1234|INSERT|7|Joao"` no FIFO `/tmp/db_req`
2. **Servidor (main thread)** lê do FIFO e empilha na fila interna
3. **Pool de workers:** Uma worker dorme, acorda, pega a tarefa da fila
4. **Worker executa:** Chama `banco.inserir(7, "Joao")` com `unique_lock`
5. **Worker responde:** Escreve `"OK|7|inserido"` em `/tmp/db_resp_1234`
6. **Cliente lê:** Resposta do seu FIFO privado

### 3.3 Por que FIFO privado pra resposta?

**Problema:** Se todos os clientes lessem o mesmo FIFO, um poderia "roubar" a resposta do outro.

**Solução:** Cada cliente cria seu próprio FIFO `/tmp/db_resp_<pid>`. O servidor responde nesse FIFO específico. Assim múltiplos clientes funcionam simultaneamente sem misturar respostas.

### 3.4 Protocolo (A "língua" que cliente e servidor falam)

**Requisição (texto simples):**
```
<pid>|<operacao>|<id>|<nome>
```

Exemplos:
- `"1234|INSERT|7|Joao"` = cliente 1234 quer inserir id=7 nome=Joao
- `"1234|SELECT|5|"` = cliente 1234 quer buscar id=5
- `"1234|UPDATE|7|Maria"` = cliente 1234 quer atualizar id=7 pra Maria
- `"1234|DELETE|7|"` = cliente 1234 quer deletar id=7

**Resposta:**
```
<status>|<id>|<info>
```

Exemplos:
- `"OK|7|Joao"` = sucesso, id=7, nome=Joao
- `"ERR|7|nao encontrado"` = erro, id=7 não existe

**Por que texto?** Porque dá pra testar com `echo "1234|SELECT|5|" > /tmp/db_req` e ler com `cat /tmp/db_resp_1234`. Facilita muito a depuração.

**Detalhes de robustez (implementados):**
- A operação é **case-insensitive**: `select`, `Select` e `SELECT` são aceitos igualmente (o parser converte pra maiúsculas antes de comparar)
- **Todo pedido tem resposta.** Se o pedido é mal formatado (ex: `DROP`), o servidor responde `ERR|0|pedido invalido` no FIFO do cliente, em vez de deixar ele esperando até o timeout. Isso é boa prática de IPC: *quem envia sempre precisa saber o destino da mensagem*
- O número de threads do servidor também é validado: `./servidor abc` avisa e usa 4; `./servidor 0` avisa e usa 1. O programa nunca quebra com entrada inválida

### 3.5 Banco de Dados (A "tabela")

**Estrutura:**
```cpp
struct Registro {
    int id;
    std::string nome;
};
```

**A tabela:** Um `std::vector<Registro>` na memória do servidor.

**Proteção:** `std::shared_mutex`
- SELECT: `shared_lock` → várias threads leem juntas
- INSERT/UPDATE/DELETE: `unique_lock` → só uma thread mexe

**Persistência:**
- No início: carrega `banco.txt` pra memória
- No Ctrl+C: salva de volta em `banco.txt`

### 3.6 Pool de Threads (Os Workers)

**Construção:**
```cpp
class Pool {
    std::vector<std::thread> workers_;
    std::queue<Tarefa> fila_;
    std::mutex mutex_;                    // protege a fila
    std::condition_variable condicao_;    // "campainha"
    bool parar_ = false;
};
```

**Como funciona:**
1. Construtor cria N threads, cada uma rodando `loopWorker()`
2. `loopWorker()` = loop infinito:
   - Espera trabalho (dorme na condition variable)
   - Pega trabalho da fila (com mutex)
   - Libera o mutex
   - Executa o trabalho FORA do mutex (pra rodar em paralelo)
   - Volta a esperar

**Encerramento (Ctrl+C):**
1. Handler de sinal seta `parar_ = true`
2. `notify_all()` acorda todos os workers
3. Workers veem a flag e saem do loop
4. Main faz `join()` em cada uma (espera terminar)
5. Banco é salvo em `banco.txt`

---

## PARTE 4: DETALHES DE IMPLEMENTAÇÃO

### 4.1 Cliente (cliente.cpp)

**Dois modos:**

**A) Interativo (você digita):**
```bash
./cliente
> INSERT 7 Joao
> SELECT 7
> UPDATE 7 Maria
> DELETE 7
> sair
```

**B) Benchmark (automático):**
```bash
./cliente -n 1000 SELECT 1     # sequencial (ping-pong)
./cliente -b 4000 SELECT 1     # burst (dispara tudo de uma vez)
```

**Diferença entre -n e -b:**
- `-n` = manda um, espera resposta, manda o próximo (latência)
- `-b` = dispara todos de uma vez com thread leitora separada (throughput)

**Thread leitora no modo -b:**
- Se o cliente disparasse tudo sem ler, o FIFO de resposta encheria (64KB)
- O servidor travaria no write FIFO cheio
- Cliente também travaria no write → **deadlock**
- Solução: thread separada só lendo respostas enquanto a main escreve

### 4.2 Servidor (servidor.cpp)

**Inicialização:**
1. Carrega `banco.txt` pra memória
2. Cria FIFO `/tmp/db_req` com `mkfifo()`
3. Cria pool de N threads
4. Trata sinal SIGINT (Ctrl+C) pra encerramento limpo

**Loop principal:**
1. Lê linhas do FIFO
2. Faz parsing (protocolo.hpp)
3. Empilha na fila do pool
4. Workers pegam e executam em paralelo

**Encerramento:**
1. Ctrl+C → flag `parar_ = true`
2. Pool para e junta threads
3. Salva `banco.txt`
4. Remove FIFO

### 4.3 Banco (banco.hpp/.cpp)

**CRUD:**
- `inserir(id, nome)` → unique_lock (escrita)
- `buscar(id)` → shared_lock (leitura)
- `atualizar(id, novoNome)` → unique_lock (escrita)
- `remover(id)` → unique_lock (escrita)

**Por que shared_mutex?**
- Se todos SELECTs usassem mutex comum, eles se bloqueariam entre si
- Com shared_mutex, SELECTs rodam em paralelo (ganho de performance)
- UPDATEs são raros comparados a SELECTs em bancos reais

### 4.4 Pool (pool.hpp/.cpp)

**Despachar tarefa (produtor):**
```cpp
{
    lock_guard lock(mutex_);
    fila_.push(tarefa);
}
condicao_.notify_one();  // acorda UMA worker
```

**Worker (consumidor):**
```cpp
unique_lock lock(mutex_);
condicao_.wait(lock, [&] { return parar_ || !fila_.empty(); });
if (parar_ && fila_.empty()) return;
tarefa = move(fila_.front());
fila_.pop();
// lock liberado aqui (fim do escopo)
tarefa();  // executa FORA do mutex
```

**Pontos importantes:**
- `wait(lock)`: dorme E libera o lock atomicamente
- `while` (não `if`) no wait: protege contra spurious wakeup
- Executa FORA do lock: senão só uma worker rodaria por vez

---

## PARTE 5: COMO DEFENDER (Perguntas e Respostas)

Veja `docs/ROTEIRO_DEFESA.md` para a lista completa de perguntas prováveis do professor com respostas detalhadas.

**Os 3 conceitos mais importantes (decore estes):**

1. **Por que FIFO?**
   > "Porque cliente e servidor são processos independentes, então precisam de um pipe nomeado. Pipe anônimo só funciona entre pai-filho. FIFO tem um nome no sistema de arquivos e qualquer processo pode abrir."

2. **Por que mutex E condition variable?**
   > "Mutex protege a fila (só uma thread mexe por vez). Condition variable evita busy waiting (worker dorme quando fila vazia, acorda quando chega trabalho). Sozinhas não bastam: você precisa das duas juntas."

3. **Por que shared_mutex no banco?**
   > "SELECTs são maioria e podem rodar em paralelo (shared_lock). UPDATEs são raros e precisam de lock exclusivo (unique_lock). Com mutex comum, SELECTs se bloqueariam entre si sem necessidade."

---

## PARTE 6: RESULTADOS E ANÁLISE

### 6.1 Modo Burst (-b)

Mede **throughput** (requisições por segundo quando chegam em rajada).

**Resultados (4000 SELECTs, média de 5):**
| Threads | Tempo Total | Por que não melhora tanto? |
|---------|-------------|----------------------------|
| 1 | 76.9 ms | SELECT é muito rápido (vetor pequeno) |
| 2 | 71.8 ms | Gargalo é IPC, não CPU |
| 4 | 72.1 ms | Overhead de sincronização |
| 8 | 66.0 ms | Melhor ~14% |

**UPDATE (4000, média de 5):**
| Threads | Tempo Total | Por que mais lento? |
|---------|-------------|---------------------|
| 1 | 97.7 ms | Escrita exige lock exclusivo |
| 2 | 98.8 ms | Serialização |
| 4 | 87.9 ms | ~10% melhor que 1 |
| 8 | 83.7 ms | ~14% melhor que 1 |

**Análise:** O ganho é modesto porque as operações são muito rápidas (busca em vetor pequeno). O gargalo é o IPC, não o processamento.

### 6.2 Modo Sequencial (-n)

Mede **latência** (tempo de ida e volta de cada requisição).

**Resultados (1000 SELECTs, média de 3):**
| Threads | Tempo Total | Por quê? |
|---------|-------------|----------|
| 1 | 11716.9 ms | Gargalo é IPC, não paralelismo |
| 2 | 11790.2 ms | Um cliente sozinho não gera trabalho paralelo |
| 4 | 12136.8 ms | Ping-pong sequencial |
| 8 | 12043.7 ms | Não há diferença |

**Análise:** Com um único cliente sequencial, não há paralelismo pra ganhar. O modo burst é o que mostra o efeito das threads.

---

## PARTE 7: COMANDOS ÚTEIS

### Testar manualmente (sem escrever código)

**Terminal 1 (servidor):**
```bash
./servidor 4
```

**Terminal 2 (cliente manual):**
```bash
# Manda um SELECT pro servidor
echo "1234|SELECT|1|" > /tmp/db_req

# Lê a resposta (num outro terminal)
cat /tmp/db_resp_1234
# Saída: OK|1|Ana Silva
```

### Benchmark completo

```bash
# Burst (throughput)
./cliente -b 4000 SELECT 1
./cliente -b 4000 UPDATE 1 "NomeX"

# Sequencial (latência)
./cliente -n 1000 SELECT 1
```

---

## PARTE 8: GLOSSÁRIO RÁPIDO

| Termo | Significado |
|-------|-------------|
| **Processo** | Programa em execução (memória isolada) |
| **Thread** | Trabalhador dentro de um processo (memória compartilhada) |
| **IPC** | Comunicação entre processos |
| **FIFO** | Pipe nomeado (cano com nome) |
| **Mutex** | Tranca de exclusão mútua (uma thread por vez) |
| **Condition Variable** | Campainha pra avisar que tem trabalho |
| **Pool de Threads** | Equipe fixa de workers reutilizáveis |
| **Condição de Corrida** | Bug quando duas threads mexem no mesmo dado sem proteção |
| **Deadlock** | Duas threads esperando uma a outra pra sempre |
| **Throughput** | Requisições por segundo (rajada) |
| **Latência** | Tempo de ida e volta (ping-pong) |
| **Spurious Wakeup** | Thread acorda sem notificação (tem que conferir de novo) |
| **Busy Waiting** | Loop queimando CPU esperando algo |
| **Shared Mutex** | Leitores simultâneos / escritor exclusivo |

---

## PARTE 9: PARA O RELATÓRIO

Use o `docs/RELATORIO.md` como base. Ele tem:
- Resumo (abstract)
- Introdução (contexto, problema, objetivos)
- Fundamentação teórica
- Arquitetura (com diagrama)
- Implementação (códigos principais)
- Resultados (tabelas de benchmark)
- Análise e discussão
- Conclusão
- Referências (ABNT)

**Só falta:**
1. Preencher os nomes dos integrantes
2. Transformar em Google Docs/PDF
3. Fazer os gráficos (dados estão na seção de resultados)

---

Boa sorte na defesa! Se você dominar este guia, você consegue explicar cada linha do código. 💪
