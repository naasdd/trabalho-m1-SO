# Roteiro de Defesa — Perguntas Prováveis e Respostas

Estude este documento. O professor pode escolher QUALQUER integrante para
responder. Todos do trio precisam dominar TODOS os pontos abaixo.

---

## 1. IPC (Comunicação entre processos)

**P: O que é IPC? Por que não usar variável global ou arquivo comum?**
R: IPC é qualquer mecanismo de comunicação entre processos. Processos têm
memória separada, então uma variável global seria invisível para o outro
processo. Arquivo comum com polling (ficar relendo o disco) é ineficiente e
não é IPC de verdade. Usamos FIFO nomeado (`mkfifo`), um pipe com nome no
sistema de arquivos que qualquer processo independente pode abrir.

**P: Por que FIFO e não pipe anônimo?**
R: Pipe anônimo (`pipe()`) só funciona entre processos parentes (ex: filho
criado com `fork`). Cliente e servidor são executáveis independentes,
iniciados separadamente — precisam de um pipe NOMEADO conhecido por ambos.

**P: Por que um FIFO de resposta por cliente (`/tmp/db_resp_<pid>`)?**
R: Se todos os clientes lessem o mesmo FIFO, um poderia ler a resposta
destinada a outro (FIFO não endereça mensagens). Com um FIFO por PID, cada
cliente tem canal exclusivo e múltiplos clientes funcionam simultaneamente.

**P: Tiveram deadlock de FIFO?**
R: Sim, no modo "burst" do benchmark: o cliente disparava tudo sem ler, o
FIFO de resposta enchia (64KB), o servidor travava no `write`, e o cliente
travava no `write` também. Solução: thread leitora no cliente drenando
respostas enquanto a main escreve. (Boa resposta = mostra que debugamos
concorrência de verdade.)

## 2. Threads e Pool

**P: Onde estão as threads? Por que pool e não thread por requisição?**
R: O servidor cria um pool fixo de N threads (`std::thread`) na inicialização.
Criar/destruir thread por requisição tem custo alto; no pool, as threads são
reaproveitadas.

**P: O que é produtor-consumidor?**
R: Padrão em que a thread principal (produtora) empilha tarefas numa fila e
as workers (consumidoras) retiram e executam. A fila desacopla a velocidade
de chegada da velocidade de processamento.

**P: Por que criar uma thread leitora no cliente (modo burst)?**
R: Para escrever e ler ao mesmo tempo sem deadlock — a main escreve pedidos
enquanto a leitora drena respostas. Mostra threads nos dois processos.

## 3. Mutex e Condition Variable (o coração do trabalho)

**P: Explique a sincronização da fila. Por que mutex E condition variable?**
R: O `mutex` protege a estrutura da fila (uma thread mexendo por vez). A
`condition_variable` resolve outro problema: esperar por trabalho SEM gastar
CPU. Sem ela, as workers ficariam em loop checando a fila (busy waiting).
O `wait(lock)` destrava o mutex e dorme atomicamente — senão o produtor
nunca conseguiria empilhar.

**P: Por que a tarefa é executada FORA da região crítica?**
R: Se executássemos com o mutex travado, só uma worker rodaria por vez e o
paralelismo morreria. O lock só é seguro durante o saque da tarefa da fila.

**P: Por que `while` e não `if` no `condicao_.wait()`?**
R: Por causa do spurious wakeup: o SO pode acordar a thread sem notificação,
então é preciso re-conferir a condição num loop.

**P: Como o encerramento funciona?**
R: Flag `parar_` setada com mutex, `notify_all()` acorda todas, cada worker
termina a tarefa atual e sai do loop, main faz `join` em cada uma. Banco é
salvo no `banco.txt`. Encerramento limpo (graceful shutdown).

## 4. Shared_mutex (leitores-escritores)

**P: O que é `std::shared_mutex`? Por que não mutex comum na tabela?**
R: É o padrão leitores-escritores: `shared_lock` permite VÁRIOS leitores
simultâneos (SELECTs em paralelo); `unique_lock` é exclusivo (INSERT/UPDATE/
DELETE bloqueiam todos). Com mutex comum, SELECTs se bloqueariam sem
necessidade.

**P: Onde estaria a condição de corrida sem proteção?**
R: Duas threads fazendo INSERT ao mesmo tempo poderiam realocar o vetor
simultaneamente → crash ou dados corrompidos. Ou um DELETE durante um SELECT
lendo o registro → iterator inválido.

## 4b. Robustez (tratamento de erros)

**P: O que acontece se eu mandar um pedido inválido (ex: `DROP 5`)?**
R: O servidor responde `ERR|0|pedido invalido` no FIFO do cliente. Todo pedido
recebe resposta — se só logássemos o erro, o cliente ficaria preso até o timeout
de 5s sem saber o que houve. Princípio de IPC: quem envia sempre deve saber o
destino da mensagem.

**P: E se eu rodar `./servidor abc` ou `./servidor 0`?**
R: O argumento é validado com try/catch: não-numérico → aviso e usa 4 threads
(padrao); menor que 1 → aviso e usa 1. O servidor nunca quebra com entrada
inválida.

**P: Aceita `select` em minúsculo?**
R: Sim — o parser normaliza a operação para maiúsculas antes de comparar,
então `select`, `Select` e `SELECT` funcionam igualmente.

## 5. Resultados (benchmark)

**P: Por que o sequencial (`-n`) não mostra diferença entre 1 e 8 threads?**
R: Porque um único cliente sequencial envia ping-pong: manda uma, espera a
resposta, manda a próxima. Não há trabalho simultâneo — o gargalo é a
LATÊNCIA do IPC, não o processamento.

**P: E no modo burst (`-b`)?**
R: Aí o cliente dispara tudo de uma vez, a fila enche, e as workers dividem
a carga. Vimos throughput de ~0.02 ms/req com várias threads vs. falhas com
1 thread saturaço. (Mostrar a tabela de tempos da bateria de testes.)

---

## Comandos para demonstrar ao vivo

```bash
make                                  # compilar
./servidor 4                          # terminal 1
./cliente                             # terminal 2 (interativo)
  > INSERT 10 Joao
  > SELECT 10
  > UPDATE 10 Maria
  > DELETE 10
  > SELECT 99                         # erro: não encontrado
./cliente -b 4000 SELECT 1            # benchmark burst
# Ctrl+C no servidor -> salva banco.txt e encerra limpo
cat banco.txt                         # mostra persistência
```
