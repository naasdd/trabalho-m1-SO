# Sistema de Processamento Paralelo de Requisições a um Banco de Dados

Trabalho M1 – Sistemas Operacionais (UNIVALI) – IPC, Threads e Paralelismo.
Enunciado completo em [`docs/`](docs/).

Um processo **cliente** envia requisições de banco por **IPC**; um processo
**servidor** as distribui para um **pool de threads**, que operam sobre uma
tabela compartilhada protegida por **mutex**.

## Como compilar

Precisa do MinGW-w64 (`g++` com suporte a pthreads) no PATH:

```
mingw32-make
```

Gera `bin\servidor.exe` e `bin\cliente.exe`.

## Como executar

Em um terminal, suba o servidor (o argumento é o tamanho do pool, padrão 4):

```
bin\servidor.exe 4
```

Em outro terminal, rode o cliente:

```
bin\cliente.exe scripts\comandos_demo.txt      Executa um roteiro de comandos
bin\cliente.exe "INSERT id=9 nome='Ana'"       Executa um único comando
bin\cliente.exe                                Lê comandos do teclado (Ctrl+Z para terminar)
```

O servidor encerra sozinho quando o cliente desconecta, e imprime quantas
requisições cada thread atendeu.

## Comandos aceitos

```
INSERT id=7 nome='Joao'
SELECT nome WHERE id=7        (ou SELECT id=7)
UPDATE id=7 nome='Joana'
DELETE WHERE id=7             (ou DELETE id=7)
LISTAR
```

## Como o projeto atende cada requisito

| Requisito do enunciado | Onde está |
|---|---|
| Cliente e servidor são executáveis distintos, processos de SO separados | `src/cliente.cpp` e `src/servidor.cpp`, dois binários em `bin/` |
| Comunicação por IPC real (canal nomeado) | `src/canal.cpp` — named pipe do Windows (`CreateNamedPipe`), o equivalente do FIFO nomeado POSIX |
| Dois canais: requisições e respostas | `CANAL_REQUISICOES` e `CANAL_RESPOSTAS` em `include/comum.hpp` |
| Servidor com pool de threads em paralelo | `trabalhador()` em `src/servidor.cpp`, criadas com `pthread_create` |
| Fila de tarefas produtor/consumidor | `fila` + `sem_itens` (`sem_t`) + `mutex_fila` em `src/servidor.cpp` |
| Tabela compartilhada protegida por mutex real | `tabela` protegida por `mutex_tabela` (`pthread_mutex_t`) |
| INSERT, SELECT, UPDATE, DELETE | `executar()` em `src/servidor.cpp` |

## Detalhes que valem citar na apresentação

**Por que as respostas voltam fora de ordem.** O cliente envia todas as
requisições antes de ler as respostas. O servidor fica então com várias
requisições enfileiradas e as threads do pool as processam ao mesmo tempo —
cada resposta sai quando a sua thread termina, não na ordem de envio. É a
prova visível de que o processamento é paralelo: cada linha de resposta mostra
o número da requisição e qual thread a atendeu.

**Três recursos compartilhados, três seções críticas.** A tabela
(`mutex_tabela`), a fila de tarefas (`mutex_fila`) e o canal de respostas
(`mutex_resposta`). O canal precisa de mutex porque é uma corrente de bytes:
duas threads escrevendo ao mesmo tempo entregariam duas respostas
intercaladas.

**Por que a fila não tem limite de tamanho.** Se ela bloqueasse quando cheia,
o sistema poderia travar: as threads podem ficar presas escrevendo em um canal
de respostas cheio enquanto o cliente ainda está escrevendo requisições. Com a
fila sempre aceitando, a thread principal continua drenando o canal de
requisições e o impasse não acontece.

**Encerramento das threads.** Um semáforo não tem o equivalente ao *broadcast*
de uma variável de condição, então o encerramento libera `sem_itens` uma vez
para cada thread do pool. Quem acorda e encontra a fila vazia entende que é o
sinal de parada.

## Limitação conhecida

A camada de IPC usa named pipes do Windows. Para rodar em Linux seria preciso
trocar `src/canal.cpp` por uma versão com `mkfifo`/`open`/`read`/`write` — a
interface em `include/comum.hpp` já isola essa troca em um arquivo só.
