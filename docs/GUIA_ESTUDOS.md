# Guia de Estudos — Trabalho M1 (Sistemas Operacionais)

Este documento explica **TUDO** que o grupo precisa saber para entender e defender
o trabalho, do zero. Ele acompanha a versão final do código (a da branch `main`:
`cliente.cpp`, `servidor.cpp`, `banco.h`, `banco.txt`, `makefile`).

---

## PARTE 1: O PROBLEMA

### 1.1 O enunciado em linguagem simples

O professor quer um "banco de dados de mentira" funcionando assim:

1. Um **programa cliente** manda pedidos: "insere o João com id 7", "busca o id 5";
2. Um **programa servidor** recebe os pedidos e processa **vários ao mesmo tempo**
   (em paralelo), usando threads;
3. Os dois são **programas separados** (processos independentes) — precisam de um
   "telefone" pra conversar: o **IPC**;
4. O servidor tem uma **tabela na memória** (o "banco") que todas as threads
   acessam — e o acesso precisa de "trancas" (**mutex**) pra não corromper nada.

É exatamente o que bancos reais fazem: o MySQL é um servidor, seus programas são
clientes, e o servidor atende muita gente ao mesmo tempo com threads.

### 1.2 Os requisitos técnicos obrigatórios (checklist do enunciado)

| Requisito | Onde está no nosso código |
|---|---|
| Dois binários/processos separados | `servidor.exe` e `cliente.exe` |
| IPC real (pipe nomeado ou memória compartilhada) | Named pipes do Windows: `sgbd_requisicoes` e `sgbd_respostas` |
| Pool de threads no servidor | `pthread_create` de N threads no `servidor.cpp` |
| Mutex ou semáforo REAL (`pthread_mutex_t`, `sem_t`) | 3 mutexes + 1 semáforo (literalmente os citados no enunciado) |
| INSERT, DELETE, SELECT, UPDATE | `executarComando()` no `banco.h` |
| Sintaxe dos exemplos do enunciado | `SELECT nome WHERE id=5`, `INSERT id=7 nome='João'` |

---

## PARTE 2: CONCEITOS FUNDAMENTAIS

### 2.1 Processo

**Processo = programa em execução**, com memória própria e isolada. Quando você
abre o Chrome e o Spotify, são dois processos: um **não enxerga as variáveis do
outro**. Cada processo tem um número de identificação (PID).

Como cliente e servidor são processos separados, se o cliente tem uma variável
`x = 10`, o servidor **não vê** esse `x`. Precisam de IPC pra conversar.

### 2.2 IPC (Inter-Process Communication)

**IPC = o "telefone" entre processos.** Mecanismos que o Sistema Operacional
fornece pra processos trocarem dados:

| Mecanismo | Como funciona |
|---|---|
| **Named pipe (o nosso)** | Canal de bytes com nome, mantido pelo kernel. Uma ponta em cada processo |
| Pipe anônimo | Igual, mas sem nome — só entre pai e filho |
| Memória compartilhada | Uma área de memória que os dois processos enxergam |
| Socket | Comunicação estilo rede (até entre máquinas) |

### 2.3 Named Pipe (o IPC que usamos)

O named pipe do Windows é o **equivalente do FIFO nomeado do POSIX** — o enunciado
fala em "pipe nomeado/FIFO", e é isso. Funciona como um **cano**:

```
cliente (ponta de escrita) ──[ sgbd_requisicoes ]──> servidor (ponta de leitura)
servidor (ponta de escrita) ──[ sgbd_respostas ]──> cliente (ponta de leitura)
```

Pontos importantes (perguntáveis na defesa):

- **Quem cria o canal fica com a ponta de leitura; quem abre, com a de escrita**
  (no nosso fluxo);
- A leitura **bloqueia** (o processo dorme) enquanto não chega dado — não é
  polling (não fica relendo um arquivo à toa);
- **IPC de verdade do SO:** `CreateNamedPipeA`, `ReadFile`, `WriteFile` —
  chamadas do Windows; nenhum dos dois processos enxerga a memória do outro;
- Temos **dois canais**: um pra ida (requisições), um pra volta (respostas) —
  exatamente o que o enunciado pede ("respostas em um segundo canal IPC").

### 2.4 Thread

**Thread = um "trabalhador" dentro de um processo.** Diferente de processos,
threads do **mesmo processo compartilham a mesma memória**. Todas as threads do
servidor enxergam a mesma tabela do banco — e é aí que mora o perigo (seção 2.6).

Usamos a biblioteca **Pthreads** (POSIX Threads):
- `pthread_create(&t, nullptr, trabalhador, arg)` — cria a thread executando a
  função `trabalhador`;
- `pthread_join(t, nullptr)` — espera a thread terminar (o `main` não morre
  deixando threads vivas).

### 2.5 Pool de Threads

**Pool = equipe fixa de trabalhadores contratados**, em vez de contratar/demitir
um trabalhador novo pra cada pedido. Criar e destruir thread tem custo; no pool,
as N threads nascem no início e ficam esperando trabalho numa fila.

No servidor: `./servidor 4` cria 4 threads; cada uma roda o loop `trabalhador()`
tirando requisições da fila até o fim do programa.

### 2.6 Condição de Corrida e Mutex

**Condição de corrida:** duas threads mexendo no mesmo dado ao mesmo tempo. Exemplo:
duas threads fazem INSERT simultâneo e as duas mandam o vetor crescer junto —
crash ou registro corrompido.

**Mutex (`pthread_mutex_t`) = a chave do banheiro:** só uma thread entra na seção
crítica por vez.

```cpp
pthread_mutex_lock(&mutex_tabela);   // pega a chave (ou espera)
// ... mexe na tabela e no banco.txt ...
pthread_mutex_unlock(&mutex_tabela);  // devolve a chave
```

Se a chave não existisse: dois INSERTs simultâneos corromperiam o vetor; dois
"saves" simultâneos deixariam o `banco.txt` escrito pela metade; um SELECT poderia
ler a tabela **no meio** de um DELETE.

### 2.7 Semáforo Contador (`sem_t`)

**Semáforo = dispenser de fichas.** `sem_wait` = pega uma ficha (se não tem,
espera); `sem_post` = coloca uma ficha (e acorda alguém esperando).

No servidor, o semáforo `sem_itens` **conta quantas tarefas existem na fila**:

```
thread principal:                     threads do pool:
  enfileira tarefa                       sem_wait(&sem_itens)   // pega ficha
  sem_post(&sem_itens)  // +1 ficha      retira tarefa da fila
                                         executa
```

**Por que semáforo E mutex na fila?** São papéis diferentes:
- o **mutex** protege a *estrutura* da fila (só uma thread mexe por vez);
- o **semáforo** conta os *itens* e faz as threads **dormirem** quando não há
  trabalho (zero de CPU) — sem ele, as threads ficariam em loop checando a fila
  (busy waiting, desperdício).

### 2.8 Deadlock (impasse)

**Deadlock = duas (ou mais) partes esperando uma pela outra, pra sempre.**
Clássico: thread A trava o mutex X e quer o Y; thread B travou o Y e quer o X.
Ninguém avança.

No nosso sistema há um caso mais sutil — o **impasse de 4 pontas** que a fila
ilimitada evita (explicado na PARTE 4, item 4.3). É um dos pontos fortes da
defesa.

### 2.9 Ordem vs Paralelismo

Processar em paralelo significa: **não há garantia de ordem**. No modo
`--paralelo`, um UPDATE enviado depois de um INSERT pode rodar **antes** dele.
Por isso:
- cada requisição tem um **número (id)** que volta repetido na resposta — o
  cliente casa pergunta e resposta mesmo fora de ordem;
- quando a ordem importa, usa-se o modo sequencial (padrão do cliente).

---

## PARTE 3: ARQUITETURA DO NOSSO SISTEMA

### 3.1 O fluxo completo

```
   [ cliente ]                                [ servidor ]
       |                                           |
       |-- sgbd_requisicoes (named pipe) ----> thread principal
       |                                           | (só IPC: lê e enfileira)
       |                                           v
       |                                    [ fila de tarefas ]
       |                                    mutex_fila + sem_itens
       |                                        |     |     |
       |                                     thread 0 ... thread N-1
       |                                        \    |    /
       |                                      tabela + banco.txt
       |                                    (mutex_tabela — seção crítica)
       |                                           |
       <--- sgbd_respostas (named pipe) ------ (mutex_resposta)
```

Passo a passo de uma requisição:
1. Cliente monta a mensagem e escreve no canal `sgbd_requisicoes`;
2. A thread principal do servidor estava bloqueada em `lerMensagem` — acorda, lê a
   mensagem inteira;
3. Trava `mutex_fila`, empilha na fila, destrava, e faz `sem_post` (acorda uma
   thread do pool);
4. Uma thread do pool estava em `sem_wait` — acorda, trava `mutex_fila`, retira a
   tarefa, destrava;
5. Chama `executarComando()` que trava `mutex_tabela`, opera no vetor, salva o
   `banco.txt`, destrava, devolve o texto de resposta;
6. A thread trava `mutex_resposta` e escreve a resposta no canal `sgbd_respostas`;
7. O cliente, bloqueado lendo esse canal, recebe a resposta.

### 3.2 Por que a thread principal não processa nada?

A thread principal **só faz IPC** (lê e enfileira). Quem processa é o pool. Assim,
**uma requisição demorada nunca bloqueia a leitura do canal** — o servidor segue
engolindo requisições enquanto as threads trabalham.

### 3.3 Os dois modos do cliente

- **Sequencial (padrão):** envia 1, espera resposta, envia 2... O servidor recebe
  uma tarefa por vez → o pool quase não paraleliza (só 1 thread trabalha a cada
  instante). É o modo de um cliente normal de banco, e torna a demonstração
  previsível;
- **`--paralelo`:** envia o lote INTEIRO antes de ler qualquer resposta. A fila
  enche, as threads competem pelas tarefas → as respostas voltam **fora de ordem**,
  cada uma marcada com o número da thread que atendeu. É a **prova visível do
  paralelismo**.

### 3.4 O protocolo: struct de tamanho fixo

```cpp
struct Mensagem {
    int  id;                // número da requisicao, repetido na resposta
    int  thread;           // qual thread atendeu (-1 na requisicao)
    char texto[MAX_TEXTO]; // o comando, ou o resultado dele
};
```

**Por que struct com `char[200]` e não `std::string`?** Porque a mensagem
atravessa a **fronteira entre processos** — o que viaja é uma sequência de bytes.
Um `std::string` guarda um **ponteiro** para memória que só existe dentro do
processo que o criou; do outro lado do canal esse ponteiro seria lixo. A struct de
tamanho fixo viaja inteira e os dois lados a interpretam igual.

**Por que o `banco.h` é um arquivo só?** Cliente e servidor precisam combinar
**exatamente** o mesmo formato de mensagem. Se cada um tivesse sua cópia da
struct e um lado mudasse, os dois processos passariam a ler os mesmos bytes de
maneiras diferentes — bug que só aparece em tempo de execução. Um arquivo único
elimina essa possibilidade.

### 3.5 O banco de dados

```cpp
struct Registro { int id; std::string nome; };
std::vector<Registro> tabela;        // o banco, na memória do servidor
pthread_mutex_t mutex_tabela;        // a "chave" que o protege
```

- **Em memória** (`tabela`) é onde as consultas acontecem;
- **Em disco** (`banco.txt`, formato `id;nome` por linha) é reescrito **a cada
  INSERT/UPDATE/DELETE**, dentro da mesma seção crítica da tabela — se duas
  threads reescrevessem juntas, o arquivo sairia corrompido. Como salva a cada
  alteração, os dados sobrevivem até a um crash do servidor.

### 3.6 Encerramento limpo

Quando o cliente desconecta, `lerMensagem` falha e o laço da thread principal
acaba. Então:
1. A thread principal faz **um `sem_post` por thread do pool** — cada uma acorda
   do `sem_wait`, olha a fila, e (se vazia) sai do laço;
2. Quem ainda encontra trabalho enfileirado, processa antes de sair;
3. `pthread_join` em cada thread — o `main` só termina quando todas morreram;
4. Imprime as estatísticas: quantas requisições cada thread atendeu.

---

## PARTE 4: OS PONTOS DE MAIOR RISCO NA DEFESA (estude com calma!)

### 4.1 Três recursos compartilhados, três mutexes separados

| Recurso | Mutex | Por quê |
|---|---|---|
| Tabela + banco.txt | `mutex_tabela` | Evita vetor corrompido e arquivo escrito pela metade |
| Fila de tarefas | `mutex_fila` | Só uma thread mexe na fila por vez |
| Canal de respostas | `mutex_resposta` | Um canal é uma corrente de bytes: duas threads escrevendo juntas entregariam respostas **intercaladas** (misturadas no meio) |

**Pergunta provável: "por que mutexes separados? Um só não bastava?"**
R: "Um só funcionaria (correto), mas **mataria o paralelismo**: uma thread
escrevendo resposta bloquearia outra que só queria fazer um SELECT. Mutexes
separados = threads só se bloqueiam quando disputam o MESMO recurso."

### 4.2 Semáforo + mutex na fila: papéis diferentes

- **Pergunta: "por que os dois?"**
  R: "O mutex protege a estrutura da fila. O semáforo **conta** as tarefas e faz
  as threads **dormirem** quando não há nada (sem busy waiting). São problemas
  diferentes: exclusão mútua e espera eficiente."

- **Pergunta: "e no encerramento, por que um post por thread?"**
  R: "Cada thread está possivelmente bloqueada num `sem_wait`. Um post acorda uma.
  Um post POR thread garante que todas acordem, vejam a fila vazia (ou terminem o
  que sobrou) e saiam. Aí o `join` completa."

### 4.3 A fila sem limite e o impasse de 4 pontas

**Pergunta quase certa: "por que a fila não tem tamanho máximo?"**
R: "De propósito. Uma fila limitada permite um deadlock em cadeia no modo
`--paralelo`:

1. O cliente escreve TODAS as requisições antes de ler respostas;
2. As respostas prontas enchem o canal de respostas → as threads do pool
   **bloqueiam escrevendo** nele;
3. Se a fila tivesse limite, ela encheria → a thread principal **bloqueia**
   enfileirando e para de drenar o canal de requisições;
4. O canal de requisições enche → o cliente **bloqueia** escrevendo → e ele
   jamais chegaria a ler as respostas que destravariam as threads.

**Impasse de 4 pontas: ninguém nunca avança.** Com a fila sempre aceitando, a
thread principal nunca deixa de drenar o canal; o cliente termina de enviar,
começa a ler, e as threads destravam."

### 4.4 Ordem de execução no modo paralelo

**Pergunta: "respostas fora de ordem não é um bug?"**
R: "Não — é o **custo inerente do paralelismo**, e está documentado como limitação
consciente. Sem transações, um banco real também não garante ordem entre
conexões diferentes. Quando a ordem importa, o cliente tem o modo sequencial.
E o `id` na mensagem permite casar pergunta e resposta mesmo fora de ordem."

### 4.5 Um cliente por vez

**Pergunta: "dá pra conectar 2 clientes?"**
R: "Não — uma instância do named pipe atende uma conexão, e o servidor encerra no
fim da primeira sessão. O enunciado pede 'um processo cliente'; o paralelismo
exigido é o do **pool de threads**, que é o que demonstramos."

### 4.6 Named pipe vs FIFO

**Pergunta: "o enunciado fala de FIFO POSIX; por que named pipe do Windows?"**
R: "São equivalentes — canal nomeado, kernel, leitura bloqueante, pontas em
processos diferentes. `CreateNamedPipeA`/`ConnectNamedPipe` são as chamadas do
Windows para o mesmo conceito. O trabalho foi desenvolvido em Windows/MinGW; a
camada de IPC está isolada na Parte 1 do `banco.h` (criarCanal, abrirCanal,
lerMensagem, escreverMensagem), portar para FIFO POSIX é trocar essas funções."

---

## PARTE 5: ARQUIVO POR ARQUIVO

### 5.1 `banco.h` — Parte 1 (IPC)

| Função | O que faz |
|---|---|
| `criarCanal(nome)` | Cria o named pipe e fica com a ponta de LEITURA |
| `abrirCanal(nome, timeout)` | Abre um canal existente pra ESCRITA, esperando até timeout (o servidor pode ainda não estar no ar) |
| `aguardarConexao(canal)` | Quem criou espera o outro processo se conectar |
| `lerMensagem` / `escreverMensagem` | Leem/escrevem a struct **inteira** — o laço interno insiste até completar os bytes (o SO pode entregar menos do que o pedido) |
| `fecharCanal` | `CloseHandle` |

### 5.2 `banco.h` — Parte 2 (Banco)

- `carregarBanco()` — lê `banco.txt` na subida do servidor (antes das threads:
  sem concorrência ainda);
- `procurar(id)` — busca linear no vetor (sempre chamada com o mutex JÁ travado);
- `executarComando(texto)` — interpreta o comando com `sscanf` e executa:
  INSERT/SELECT/UPDATE/DELETE/LISTAR. Toda manipulação da tabela acontece entre
  `pthread_mutex_lock(&mutex_tabela)` e `unlock` — **esta é a seção crítica do
  trabalho**;
- `salvarBanco()` — regrava `banco.txt` inteiro, dentro da seção crítica.

### 5.3 `servidor.cpp`

- `main`: carrega o banco, cria o semáforo, cria o canal de requisições, espera o
  cliente, abre o canal de respostas, cria as N threads e entra no laço de
  leitura/enfileiramento;
- `trabalhador()`: o loop de cada thread do pool (sem_wait → retira → executa →
  responde), já mostrado na PARTE 3;
- No fim: um post por thread, join de todos, estatísticas por thread, fecha tudo.

### 5.4 `cliente.cpp`

- Lê comandos de: arquivo passado como argumento OU digitação (Ctrl+Z + Enter
  termina no Windows); ignora linhas vazias e comentários (`#`);
- Cria o canal de respostas ANTES de conectar (o servidor o abre assim que
  aceita a conexão — a ordem importa);
- Modos: sequencial (padrão) e `--paralelo` (envia tudo, depois lê tudo);
- Imime cada resposta como `<- #5 [thread 2] OK id=5 nome='...'`.

### 5.5 `makefile`

- `mingw32-make` compila `servidor.exe` e `cliente.exe` (g++ do MinGW,
  `-std=c++17 -Wall -Wextra -O2 -lpthread`);
- `mingw32-make limpar` apaga os executáveis.

---

## PARTE 6: COMO DEMONSTRAR AO VIVO

```
# terminal 1                       # terminal 2
servidor 4                         cliente
                                   INSERT id=10 nome='Teste'
                                   SELECT nome WHERE id=10
                                   UPDATE id=10 nome='Outro'
                                   LISTAR
                                   DELETE WHERE id=10
                                   (Ctrl+Z + Enter)
```

Depois, a demo do paralelismo — crie um arquivo `lote.txt`:

```
# lote.txt
INSERT id=100 nome='A'
INSERT id=101 nome='B'
( ... 20 comandos ... )
LISTAR
```

```
cliente lote.txt            # sequencial: respostas em ordem (#1, #2, #3...)
cliente lote.txt --paralelo # fora de ordem, cada uma com [thread N]
```

E, ao encerrar o servidor (Ctrl+C ou fim do cliente), as estatísticas:

```
Requisicoes atendidas por thread:
  thread 0: 11
  thread 1: 9
  thread 2: 12
  thread 3: 8
```

**Roteiro da fala:** "No modo sequencial as respostas chegam na ordem. No modo
paralelo, o servidor recebe o lote inteiro, a fila enche, e as threads competem:
vejam que a resposta #7 chegou antes da #4 e foi atendida pela thread 2. No fim,
o servidor mostra a carga distribuída entre as 4 threads — 40 requisições
divididas quase igualmente. Isso é o pool em ação."

---

## PARTE 7: GLOSSÁRIO RÁPIDO

| Termo | Significado |
|---|---|
| Processo | Programa em execução, memória isolada |
| Thread | Trabalhador dentro do processo, memória compartilhada |
| IPC | Comunicação entre processos |
| Named pipe / FIFO | Canal nomeado de bytes entre processos |
| Pthreads | Biblioteca POSIX de threads (pthread_create, pthread_join) |
| Mutex | Chave da seção crítica (pthread_mutex_lock/unlock) |
| Semáforo | Contador atômico: sem_wait pega ficha, sem_post coloca |
| Seção crítica | Trecho que acessa recurso compartilhado, sob mutex |
| Condição de corrida | Bug de acesso concorrente sem sincronização |
| Deadlock/impasse | Todos esperando uns pelos outros, pra sempre |
| Busy waiting | Loop queimando CPU esperando algo (evitamos com semáforo) |
| Pool de threads | Equipe fixa de threads reutilizáveis |
| Latência | Tempo de ida e volta de uma requisição |
| Throughput | Requisições processadas por unidade de tempo |

---

## PARTE 8: ONDE ESTÃO OS OUTROS DOCUMENTOS

- `docs/RELATORIO.md` — o relatório (converter em PDF pro entrega)
- `docs/BENCHMARK.md` — como rodar as medições no Windows e preencher o relatório
- `docs/ROTEIRO_DEFESA.md` — perguntas prováveis e respostas prontas
- PDF do enunciado — `docs/Trabalho M1_2026_2_propost.pdf`
