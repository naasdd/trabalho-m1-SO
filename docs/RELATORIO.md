# Sistema de Processamento Paralelo de Requisições a um Banco de Dados

**Autores:** José Gabriel Santos Gomes, Matheus Pompeo, Nathan Gustavo Padilha Reichert
**Universidade:** Universidade do Vale do Itajaí (UNIVALI)
**Disciplina:** Sistemas Operacionais
**Professor:** [Nome do Professor]
**Data:** [Data de entrega]

---

## Resumo

Este trabalho apresenta a implementação de um sistema cliente-servidor que simula
um gerenciador de requisições a um banco de dados, aplicando os conceitos de
comunicação entre processos (IPC), threads, concorrência e paralelismo. O sistema
é composto por dois processos independentes: um processo cliente que envia
requisições (INSERT, SELECT, UPDATE, DELETE) através de named pipes do Windows, e
um processo servidor que as processa em paralelo utilizando um pool de threads
implementado com a biblioteca Pthreads. O acesso à tabela compartilhada que
simula o banco de dados é protegido por mutex (`pthread_mutex_t`) e a distribuição
de tarefas entre as threads utiliza um semáforo contador (`sem_t`). Experimentos
comparam o processamento sequencial e paralelo com diferentes quantidades de
threads, evidenciando o comportamento do pool em cenários de carga.

**Palavras-chave:** Sistemas Operacionais, IPC, Named Pipes, Pthreads, Mutex,
Semáforo, Paralelismo.

---

## 1. Introdução

### 1.1 Contexto

Sistemas gerenciadores de banco de dados reais atendem múltiplas requisições
concorrentemente: enquanto um cliente consulta um registro, outro insere um novo.
O processamento paralelo, via threads, permite aproveitar os múltiplos núcleos dos
processadores modernos. O desafio central é que threads de um mesmo processo
compartilham a mesma memória — o acesso simultâneo e descoordenado a estruturas
compartilhadas causa condições de corrida e corrompe dados. A solução está nos
mecanismos de sincronização fornecidos pelo sistema operacional: mutexes e
semáforos.

### 1.2 Problema Proposto

Conforme o enunciado, deve-se desenvolver um sistema que simule o funcionamento
interno de um gerenciador de requisições a um banco de dados, com múltiplos
processos e threads:

- um processo **cliente** envia requisições de consulta ou inserção via IPC;
- um processo **servidor** recebe as requisições e usa várias threads para
  processá-las em paralelo;
- as threads utilizam **mutex ou semáforo** para garantir acesso seguro a uma
  estrutura compartilhada que simula o banco de dados (vetor + arquivo banco.txt).

Requisições suportadas: INSERT, SELECT, UPDATE e DELETE, com sintaxe conforme os
exemplos do enunciado (`SELECT nome WHERE id=5`, `INSERT id=7 nome='João'`).

### 1.3 Objetivos

- Implementar comunicação real entre dois processos independentes usando IPC
  (named pipes);
- Processar requisições em paralelo com um pool de threads (Pthreads);
- Proteger a estrutura compartilhada com `pthread_mutex_t` e coordenar a fila de
  tarefas com `sem_t`;
- Comparar, experimentalmente, o processamento sequencial e o paralelo.

---

## 2. Fundamentação Teórica

### 2.1 Processos e IPC

Processos são programas em execução com espaços de endereçamento isolados: um
processo não enxerga as variáveis de outro. Para que o cliente e o servidor
troquem dados, é preciso um mecanismo de **comunicação entre processos (IPC)**
fornecido pelo sistema operacional. Neste trabalho, o IPC utilizado é o **named
pipe do Windows**, equivalente direto do FIFO nomeado POSIX: um canal de bytes
com nome, mantido pelo kernel, com uma ponta em cada processo. A leitura bloqueia
enquanto não há dados — não há leitura repetida de arquivo (polling) nem memória
compartilhada via variáveis globais.

### 2.2 Threads e Pthreads

Threads são unidades de execução dentro de um processo que **compartilham a mesma
memória**. A biblioteca Pthreads (`pthread_create`, `pthread_join`) permite criar
threads no padrão POSIX. Um **pool de threads** mantém um conjunto fixo de threads
reutilizáveis que esperam por tarefas — criar uma thread por requisição seria
custoso e desnecessário.

### 2.3 Mutex e Exclusão Mútua

Um **mutex** (`pthread_mutex_t`) garante que apenas uma thread por vez execute uma
**seção crítica** — o trecho de código que acessa um recurso compartilhado. Sem
exclusão mútua, duas threads inserindo simultaneamente no vetor poderiam corrompê-lo
(condição de corrida).

### 2.4 Semáforo Contador

Um **semáforo** (`sem_t`) é um contador atômico: `sem_wait` decrementa (bloqueando
quando chega a zero) e `sem_post` incrementa. No padrão produtor-consumidor, o
produtor faz `sem_post` a cada item enfileirado; cada consumidor faz `sem_wait`
para retirar um item — as threads dormem enquanto não há trabalho, **sem gastar
CPU** (sem busy waiting).

### 2.5 Concorrência e Ordem

Processamento paralelo implica **ausência de garantia de ordem** entre requisições:
elas são atendidas conforme a disponibilidade das threads. Por isso, cada
requisição carrega um número identificador que é repetido na resposta, permitindo
ao cliente casar pergunta e resposta mesmo quando chegam fora de ordem.

---

## 3. Arquitetura do Sistema

### 3.1 Visão Geral

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

### 3.2 Componentes

**Processo cliente (`cliente.cpp`)** — cria o canal de respostas (fica com a ponta
de leitura), abre o canal de requisições publicado pelo servidor, e opera em dois
modos:
- **Sequencial (padrão):** envia um comando, espera a resposta, envia o próximo.
  As respostas chegam na ordem;
- **`--paralelo`:** envia todas as requisições de uma vez e depois recolhe as
  respostas — que voltam **fora de ordem**, cada uma marcada com o número da
  thread que a atendeu. É a demonstração visível do paralelismo.

**Processo servidor (`servidor.cpp`)** — a thread principal faz **apenas IPC**: lê
mensagens do canal de requisições e as enfileira. Quem interpreta comandos, acessa
a tabela e escreve respostas são as threads do pool, em paralelo. Assim, uma
requisição demorada nunca bloqueia a leitura do canal.

**Camada compartilhada (`banco.h`)** — dividida em duas partes:
- *Parte 1 (IPC):* formato da mensagem (`struct Mensagem` de tamanho fixo) e as
  funções do canal (`criarCanal`, `abrirCanal`, `aguardarConexao`, `lerMensagem`,
  `escreverMensagem`). Um único arquivo garante que cliente e servidor interpretem
  exatamente os mesmos bytes;
- *Parte 2 (Banco):* a tabela compartilhada (`std::vector<Registro>`), o mutex que
  a protege, a persistência em `banco.txt` e o interpretador de comandos.

### 3.3 Protocolo de Mensagens

A mensagem é uma **struct de tamanho fixo** — o que viaja pelo canal é uma
sequência de bytes, e a struct viaja inteira (não se pode enviar um `std::string`,
que guarda um ponteiro válido só dentro do processo que o criou):

```cpp
struct Mensagem {
    int  id;                // número da requisição, repetido na resposta
    int  thread;           // qual thread atendeu (-1 na requisição)
    char texto[MAX_TEXTO]; // o comando, ou o resultado dele
};
```

### 3.4 Comandos Suportados

Sintaxe idêntica aos exemplos do enunciado:

```
INSERT id=7 nome='João'
SELECT nome WHERE id=5
UPDATE id=7 nome='Maria'
DELETE WHERE id=7
LISTAR
```

---

## 4. Implementação

### 4.1 Tecnologias

- Linguagem: C++17
- IPC: named pipes do Windows (`CreateNamedPipeA`, `ReadFile`, `WriteFile`)
- Threads: Pthreads (`pthread_create`, `pthread_join`)
- Sincronização: `pthread_mutex_t` (três instâncias) e `sem_t`
- Build: makefile (MinGW g++)

### 4.2 Estrutura de Arquivos

```
cliente.cpp     processo cliente
servidor.cpp    processo servidor
banco.h         protocolo IPC + banco de dados (arquivo único compartilhado)
banco.txt       persistência (formato "id;nome" por linha)
makefile        compilação (mingw32-make)
docs/           documentação, relatório e enunciado
```

### 4.3 Três Recursos Compartilhados, Três Seções Críticas

Cada recurso compartilhado do servidor tem seu próprio mutex — separados de
propósito, para que threads operando em recursos diferentes não se bloqueiem
sem necessidade:

| Recurso | Proteção | Por quê |
|---|---|---|
| Tabela + `banco.txt` | `mutex_tabela` | Duas threads inserindo ao mesmo tempo corromperiam o vetor; duas reescrevendo o arquivo o deixariam pela metade |
| Fila de tarefas | `mutex_fila` + `sem_itens` | O mutex protege a estrutura; o semáforo conta as tarefas e acorda consumidores sem gastar CPU |
| Canal de respostas | `mutex_resposta` | Um canal é uma corrente de bytes: duas threads escrevendo juntas entregariam respostas intercaladas |

### 4.4 O Loop das Threads do Pool

```cpp
for (;;) {
    sem_wait(&sem_itens);              // dorme até existir tarefa

    pthread_mutex_lock(&mutex_fila);   // seção crítica da fila
    if (fila.empty()) {                // só ocorre no encerramento
        pthread_mutex_unlock(&mutex_fila);
        break;
    }
    const Mensagem requisicao = fila.front();
    fila.pop();
    pthread_mutex_unlock(&mutex_fila); // libera a fila

    // executa o comando na tabela (mutex_tabela, dentro de executarComando)
    const std::string texto = executarComando(requisicao.texto);

    pthread_mutex_lock(&mutex_resposta);   // seção crítica do canal
    escreverMensagem(canal_respostas, resposta);
    pthread_mutex_unlock(&mutex_resposta);
}
```

O semáforo cumpre o papel de "campainha": a thread dorme no `sem_wait` enquanto
não há tarefas (zero consumo de CPU) e acorda quando a thread principal faz
`sem_post` após enfileirar uma requisição.

### 4.5 A Fila Sem Limite de Tamanho

A fila de tarefas **não tem capacidade máxima de propósito**. Uma fila limitada
permitiria um impasse em quatro pontas no modo paralelo: o cliente envia tudo
antes de ler respostas; se o canal de respostas encher, as threads do pool ficam
bloqueadas escrevendo; com a fila cheia, a thread principal também travaria ao
enfileirar e pararia de drenar o canal de requisições; que encheria e travaria o
cliente — que nunca chegaria a ler as respostas que destravariam as threads. Com
a fila sempre aceitando, a thread principal nunca deixa de drenar o canal e o
impasse não se forma.

### 4.6 Persistência Dentro da Seção Crítica

`banco.txt` é reescrito a cada INSERT/UPDATE/DELETE, **dentro** do lock da tabela:
se duas threads reescrevessem o arquivo simultaneamente, ele sairia corrompido —
o arquivo é um recurso compartilhado como o vetor. Como a gravação ocorre a cada
alteração, os dados sobrevivem mesmo a um encerramento abrupto do servidor.

### 4.7 Encerramento

Quando o cliente desconecta, o laço de leitura da thread principal termina. O
encerramento envia um `sem_post` por thread do pool — cada uma acorda, encontra a
fila vazia e sai do laço (quem ainda encontra trabalho enfileirado processa antes
de sair). O `pthread_join` garante que nenhuma thread fica viva após o `main`.

---

## 5. Resultados

### 5.1 Metodologia

Cenário: lote de **N = 2000** requisições enviado pelo cliente, variando:
- quantidade de threads do servidor (1, 2, 4, 8);
- modo do cliente (sequencial vs `--paralelo`);
- tipo de operação (SELECT — só leitura; UPDATE — escrita com regravação de
  `banco.txt` dentro da seção crítica).

Lotes: `lote_select.txt` (2000× `SELECT nome WHERE id=<1..3>`) e
`lote_update.txt` (2000× `UPDATE id=<1..3> nome='bench<N>'`), gerados por
`docs/benchmark3x.ps1`. Antes de cada medição o `banco.txt` foi restaurado ao
estado inicial (3 registros) e o servidor foi subido do zero com o número de
threads da configuração; o tempo medido (`Measure-Command`) cobre somente o
cliente, do primeiro envio à última resposta. Cada configuração foi medida
**3 vezes**; reporta-se cada rodada e a média aritmética. Ambiente:
**AMD Ryzen 3 3300X (4 núcleos / 8 threads lógicos), 16 GB RAM,
Windows 11 Pro (10.0.26200)**, MinGW-w64 `g++ -O2`. Dados brutos em
`docs/evidencias/resultados.csv` (matriz 16×3) e logs por configuração em
`docs/evidencias/servidor_<modo>_<op>_<threads>_r<rodada>.log`.

### 5.2 Resultados — Modo Paralelo (throughput)

**Tabela 1: Tempo total para 2000 requisições SELECT (modo `--paralelo`)**

| Threads | Rodada 1 (s) | Rodada 2 (s) | Rodada 3 (s) | Média (s) |
|---------|--------------|--------------|--------------|-----------|
| 1       | 0.083        | 0.058        | 0.048        | 0.063     |
| 2       | 0.066        | 0.058        | 0.058        | 0.061     |
| 4       | 0.086        | 0.105        | 0.087        | 0.093     |
| 8       | 0.063        | 0.884*       | 0.048        | 0.332     |

\**outlier* isolado (ver §5.5); remedicoes da mesma célula: 0.078, 0.056 e
0.070 s — mediana das 3 rodadas originais: 0.063 s.

**Tabela 2: Tempo total para 2000 requisições UPDATE (modo `--paralelo`)**

| Threads | Rodada 1 (s) | Rodada 2 (s) | Rodada 3 (s) | Média (s) |
|---------|--------------|--------------|--------------|-----------|
| 1       | 1.954        | 2.012        | 2.051        | 2.006     |
| 2       | 2.317        | 2.822        | 2.227        | 2.455     |
| 4       | 3.117        | 6.827*       | 3.332        | 4.425     |
| 8       | 3.578        | 3.277        | 3.780        | 3.545     |

\**outlier* isolado (ver §5.5); remedicoes: 3.405 e 3.333 s — mediana das 3
rodadas originais: 3.332 s.

### 5.3 Resultados — Modo Sequencial (latência)

**Tabela 3: Tempo total para 2000 requisições SELECT (modo sequencial)**

| Threads | Rodada 1 (s) | Rodada 2 (s) | Rodada 3 (s) | Média (s) |
|---------|--------------|--------------|--------------|-----------|
| 1       | 0.121        | 0.084        | 0.112        | 0.106     |
| 4       | 0.101        | 0.092        | 1.496*       | 0.563     |

\**outlier* isolado (ver §5.5); remedicoes: 0.099, 0.067 e 0.089 s — medianas:
1 thread 0.112 s, 4 threads 0.101 s (diferença < 10%).

**Tabela 4 (apoio): Tempo total para 2000 UPDATEs (modo sequencial)**

| Threads | Rodada 1 (s) | Rodada 2 (s) | Rodada 3 (s) | Média (s) |
|---------|--------------|--------------|--------------|-----------|
| 1       | 4.658        | 2.637        | 3.039        | 3.445     |
| 2       | 3.962        | 2.708        | 2.820        | 3.163     |
| 4       | 3.214        | 2.442        | 2.620        | 2.759     |
| 8       | 2.806        | 2.084        | 1.962        | 2.284     |

A rodada 1 (cache frio de disco) é sistematicamente a mais lenta. Remedições
com cache quente: 1 thread 2.366/2.302 s vs 8 threads 2.344/2.069 s —
praticamente empatados, como prevê a teoria (§6.1).

### 5.4 Distribuição de Carga Entre as Threads

O servidor imprime, ao encerrar, quantas requisições cada thread atendeu
(valores reais, rodada 3, modo `--paralelo`, 4 threads):

```
Paralelo SELECT, 4 threads (docs/evidencias/servidor_paralelo_SELECT_4_r3.log):
  thread 0: 488
  thread 1: 491
  thread 2: 526
  thread 3: 495
  total: 2000 | registros em banco.txt: 3

Paralelo UPDATE, 4 threads (docs/evidencias/servidor_paralelo_UPDATE_4_r3.log):
  thread 0: 490
  thread 1: 504
  thread 2: 526
  thread 3: 480
  total: 2000 | registros em banco.txt: 3
```

Mesmo no modo sequencial a carga se divide (ex.: `docs/evidencias/servidor_sequencial_SELECT_4_r1.log`:
500/501/499/500) — cada requisição vai para a primeira thread livre, o que
confirma que o pool distribui trabalho nos dois modos; o que muda é quantas
tarefas coexistem na fila.

### 5.5 Nota sobre variação (outliers)

Quatro das 48 medições destoaram isoladamente da célula (paralelo SELECT 8/R2
0.884 s; paralelo UPDATE 4/R2 6.827 s; sequencial SELECT 2/R3 0.409 s e
SELECT 4/R3 1.496 s, contra vizinhas de 0.05–0.17 s e 2.0–3.8 s). Cada uma foi
remedida 2–3 vezes e voltou ao patamar das demais rodadas, sem reproduzir o
pico. Atribuímos a interferência externa do SO (escalonamento/antivírus),
não ao sistema — por isso as tabelas mantêm os valores originais e a média
aritmética simples, com medianas e remedições registradas ao lado.

---

## 6. Análise e Discussão

### 6.1 Sequencial vs Paralelo

No modo sequencial o cliente faz ping-pong (envia, espera, envia): há no máximo
**uma tarefa por vez** na fila, de modo que o número de threads não deveria
influenciar — e é o que os dados quentes confirmam. Em SELECT, as medianas com
1 e 4 threads diferem menos de 10% (0.112 s vs 0.101 s); em UPDATE com cache
quente, 1 thread (2.37/2.30 s) empata com 8 threads (2.34/2.07 s). A aparente
"escala" da Tabela 4 (3.445 s → 2.284 s) vem quase toda da rodada 1 com cache
frio, que penalizou mais as execuções longas — não de paralelismo real. O tempo
sequencial é dominado pela **latência de ida e volta pelo canal**, não por falta
de threads. Já no modo `--paralelo` o lote inteiro é enfileirado de uma vez e
as threads competem pelas tarefas — só aí o tamanho do pool importa, e cada
operação reage de um jeito (§6.2).

### 6.2 Leituras vs Escritas

A diferença de patamar é de quase duas ordens de grandeza: ~0.06–0.11 s para
2000 SELECTs contra ~2.0–3.5 s para 2000 UPDATEs. A causa está no código: o
SELECT segura o `mutex_tabela` apenas para uma busca linear em 3 elementos,
enquanto o UPDATE reescreve o `banco.txt` **dentro da mesma seção crítica**, a
cada requisição — 2000 escritas de arquivo serializadas no mutex e no disco.
Pior: no paralelo, mais threads **pioram** o UPDATE (1 thread 2.01 s →
8 threads 3.55 s), porque N threads disputando o mesmo lock só adicionam
contenção e trocas de contexto sem nenhum trabalho sobreposto — o trecho
serial domina (efeito previsto pela lei de Amdahl). O SELECT paralelo, leve
demais (~30 µs/req), fica limitado pelo custo do IPC e não escala: 1, 2, 4 e 8
threads empatam em ~0.06–0.09 s. Moral do experimento: paralelismo acelera o
que é paralelizável; a escrita serializada no mutex + disco não é.

### 6.3 Escala de Threads

Na máquina de teste (4 núcleos / 8 threads lógicos) não houve ganho ao passar
de 1–2 para 4–8 threads em nenhum cenário: o SELECT já satura o canal com 1
thread e o UPDATE serializa no lock. Isso é o resultado correto para esta carga
— um banco de brinquedo com 3 registros e seção crítica por operação inteira —
e não uma falha do pool: a distribuição §5.4 mostra as 4 threads recebendo
~500 requisições cada, ou seja, o mecanismo de divisão funciona; o que falta é
trabalho paralelizável para dividir. Num cenário com comandos mais pesados
(simulação de I/O fora do lock, ou travas por registro em vez de trava da
tabela inteira), esperar-se-ia ganho até ~4 threads e estabilização acima disso
por contenção de mutex.

### 6.4 Ordem de Chegada das Respostas

No modo `--paralelo`, as respostas chegam fora de ordem e identificadas por
`id` e `thread` — evidência direta de execução concorrente. O custo é a ausência
de garantia de ordem entre requisições, aceita e documentada como limitação.

---

## 7. Conclusão

O sistema implementa com sucesso os requisitos do enunciado: dois processos
independentes comunicando-se por IPC real (named pipes), um pool de threads
Pthreads processando requisições em paralelo e exclusão mútua protegendo a
estrutura compartilhada — com `pthread_mutex_t` nas três seções críticas (tabela
e arquivo, fila de tarefas, canal de respostas) e `sem_t` coordenando
produtor-consumidor sem consumo de CPU. O encerramento é limpo: um post por
thread, join e estatísticas por thread. A escolha consciente de uma fila sem
limite evita um impasse de quatro pontas no modo paralelo — análise de deadlock
aplicada, não teórica. Os experimentos (§5) confirmam o modelo: carga dividida
de forma equilibrada entre as threads, modo sequencial limitado pela latência
do canal, UPDATEs serializados pela regravação de `banco.txt` dentro da seção
crítica (1 thread mais rápida que 8 no paralelo) e SELECTs leves limitados
pelo custo do IPC.

---

## 8. Referências

- TANENBAUM, A. S.; BOS, H. **Modern Operating Systems**. 4. ed. Pearson, 2014.
- SILBERSCHATZ, A.; GALVIN, P. B.; GAGNE, G. **Operating System Concepts**. 10.
  ed. Wiley, 2018.
- Open Group. **POSIX Threads Programming**. Disponível em:
  https://pubs.opengroup.org/onlinepubs/7908799/xsh/pthread.h.html
- Microsoft. **Named Pipes — Win32 API**. Disponível em:
  https://learn.microsoft.com/windows/win32/ipc/named-pipes

---

## Apêndice A — Execução

```
mingw32-make           # compila servidor.exe e cliente.exe
servidor 4             # terminal 1 (4 threads no pool)
cliente                # terminal 2, digita comandos, Ctrl+Z + Enter para terminar
cliente comandos.txt           # lote de comandos de arquivo
cliente comandos.txt --paralelo # lote em rajada (respostas fora de ordem)
```

## Apêndice B — Roteiro de Demonstração

1. Sequencial com comandos variados (INSERT/SELECT/UPDATE/DELETE/LISTAR) — ordem
   preservada;
2. `--paralelo` com o mesmo lote — respostas fora de ordem, cada uma marcada com
   a thread que atendeu;
3. Encerramento do servidor — estatísticas de atendimento por thread.
