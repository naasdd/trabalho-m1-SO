# Roteiro de Defesa — Perguntas Prováveis e Respostas

O professor pode escolher QUALQUER integrante do trio. Todos devem dominar este
documento. Ele cobre a versão final do código (branch `main`).

---

## 1. Visão geral (responda qualquer pergunta começando daqui)

**P: Explique o trabalho em uma frase.**
R: Um servidor que gerencia um banco de dados simulado, atendendo requisições de
um cliente em paralelo com um pool de threads, com comunicação entre os dois
processos via named pipes e sincronização com mutexes e semáforo.

**P: Quais são os dois processos e por que são processos, não threads?**
R: `servidor.exe` e `cliente.exe` são executáveis independentes, com espaços de
memória isolados. O enunciado exige explicitamente dois binários distintos
rodando como processos separados — a comunicação entre eles só é possível via
IPC, porque não compartilham memória.

**P: Onde está o paralelismo?**
R: No pool de threads do servidor. A thread principal só faz IPC (lê e enfileira);
as N threads do pool (`pthread_create`) retiram tarefas da fila e as processam
simultaneamente.

---

## 2. IPC (named pipes)

**P: Por que named pipe e não variável global ou arquivo comum?**
R: Variável global é invisível entre processos (memória isolada). Arquivo comum
exigiria polling — reler o disco repetidamente. Named pipe é IPC de sistema
operacional: canal de bytes com nome, mantido pelo kernel, leitura bloqueante
(o leitor dorme até chegar dado), pontas em processos diferentes.

**P: O enunciado fala de FIFO POSIX. Named pipe do Windows serve?**
R: Serve — são o mesmo conceito (canal nomeado, kernel, bloqueante). O trabalho
foi feito em Windows; `CreateNamedPipeA`/`ConnectNamedPipe` são as chamadas
equivalentes. A camada de IPC está isolada na Parte 1 do `banco.h`; portar para
FIFO POSIX é trocar criarCanal/abrirCanal/lerMensagem/escreverMensagem.

**P: Por que dois canais?**
R: `sgbd_requisicoes` leva os pedidos (cliente→servidor); `sgbd_respostas` traz
as respostas (servidor→cliente). O enunciado pede as respostas "em um segundo
canal IPC" — e assim nenhuma ponta precisa alternar entre ler e escrever.

**P: Quem cria qual canal?**
R: O servidor cria `sgbd_requisicoes` e espera a conexão; o cliente cria
`sgbd_respostas` e abre o de requisições. A mensagem de erro e o timeout do
`abrirCanal` tratam o caso do servidor ainda não estar no ar.

**P: Por que a mensagem é uma struct de tamanho fixo com char[200]?**
R: A mensagem atravessa a fronteira de processos: o que viaja são bytes. Um
`std::string` guarda ponteiro para memória que só existe no processo que o criou
— do outro lado seria lixo. A struct fixa viaja inteira e os dois lados (mesmo
`banco.h`) interpretam os mesmos bytes de forma idêntica.

---

## 3. Threads e pool

**P: Por que pool e não uma thread por requisição?**
R: Criar/destruir thread tem custo. O pool mantém N threads criadas no início,
reutilizadas a vida toda — espera trabalho no semáforo, processa, volta a
esperar.

**P: O que faz a thread principal?**
R: Só IPC: `lerMensagem` do canal de requisições, `mutex_fila` lock, empilha,
unlock, `sem_post`. Assim uma requisição demorada nunca bloqueia a leitura do
canal — a fila desacopla chegada de processamento.

**P: Quantas threads e como escolher?**
R: `servidor N` (padrão 4, máx 32). Para CPU-bound, o ideal é próximo do número
de núcleos; nosso servidor é dominado por I/O (canal e disco), então o ganho
estabiliza além disso.

---

## 4. Sincronização (mutex + semáforo)

**P: Quais recursos compartilhados existem no servidor?**
R: Três: (1) a tabela + `banco.txt`; (2) a fila de tarefas; (3) o canal de
respostas. Cada um com seu mutex: `mutex_tabela`, `mutex_fila`, `mutex_resposta`.

**P: Por que três mutexes separados? Um só não bastava?**
R: Um só seria correto mas destruiria o paralelismo: uma thread escrevendo a
resposta bloquearia outra que só queria um SELECT. Separados, threads só se
esperam quando disputam o MESMO recurso.

**P: Por que a fila usa mutex E semáforo?**
R: Papéis diferentes. O mutex protege a estrutura (exclusão mútua). O semáforo
`sem_itens` CONTA as tarefas: `sem_post` ao enfileirar, `sem_wait` ao consumir —
as threads dormem sem consumir CPU quando a fila está vazia (sem busy waiting).

**P: O que é busy waiting e por que evitamos?**
R: Ficar num loop checando a fila eternamente — queima CPU à toa. O `sem_wait`
bloqueia a thread no kernel; ela só volta a consumir CPU quando há trabalho.

**P: Por que gravar o banco.txt dentro da seção crítica?**
R: O arquivo é compartilhado como o vetor. Duas threads reescrevendo juntas
deixariam o arquivo pela metade (intercalado). Como está dentro do lock da
tabela, escrita do vetor e do arquivo são atômicas em relação às outras threads.

**P: O que seria condição de corrida aqui?**
R: Sem `mutex_tabela`: dois INSERTs simultâneos corrompendo o vetor; um SELECT
lendo o vetor no meio de um DELETE. Sem `mutex_resposta`: duas respostas
intercaladas byte a byte no canal. Sem `mutex_fila`: fila corrompida por push/pop
simultâneos.

---

## 5. Design: as perguntas difíceis

**P: Por que a fila não tem limite de tamanho? (pergunta quase certa)**
R: De propósito — uma fila limitada permite um impasse de 4 pontas no modo
`--paralelo`: o cliente enche o canal de respostas → as threads bloqueiam
escrevendo nele; a fila enche → a thread principal bloqueia enfileirando e para
de drenar requisições; o canal de requisições enche → o cliente bloqueia
escrevendo e nunca lê as respostas que destravariam as threads. Com a fila
ilimitada, a thread principal sempre drena o canal; o cliente termina de enviar,
lê as respostas, e as threads destravam. (Memória não é problema: cada tarefa é
uma struct de ~200 bytes.)

**P: As respostas fora de ordem no modo --paralelo são bug?**
R: Não — é o custo do paralelismo, limitação documentada. Um banco real, sem
transações, também não garante ordem entre sessões. Por isso cada requisição
carrega um `id` repetido na resposta: o cliente casa pergunta e resposta mesmo
fora de ordem. Quando a ordem importa, usa-se o modo sequencial.

**P: Dá pra conectar dois clientes ao mesmo tempo?**
R: Não — uma instância do named pipe atende uma conexão e o servidor encerra na
desconexão. O enunciado pede "um processo cliente" e o paralelismo exigido é o do
pool de threads, que demonstramos. (Múltiplos clientes = múltiplas instâncias do
pipe + um accept loop; fora do escopo.)

**P: Por que o cliente precisa criar o canal de respostas antes de conectar?**
R: O servidor, ao aceitar a conexão de requisições, imediatamente tenta abrir o
canal de respostas. Se o cliente ainda não o tivesse criado, daria erro. A ordem
de setup está comentada no código por esse motivo.

**P: O que acontece se o servidor cair no meio?**
R: O `banco.txt` está sempre em dia — é reescrito a cada INSERT/UPDATE/DELETE,
dentro da seção crítica. Reinicie o servidor e os dados até o último comando
completado estão lá.

---

## 6. Encerramento

**P: Como as threads terminam?**
R: O cliente desconecta → `lerMensagem` falha → o laço da thread principal acaba.
Ela envia um `sem_post` por thread (acorda todas do `sem_wait`); quem acha fila
vazia sai do laço, quem acha trabalho processa e depois sai. `pthread_join` em
todas garante que o `main` não termina com threads vivas. Por fim, estatísticas
de atendimento por thread.

**P: Por que um post por thread, e não um só?**
R: Cada `sem_post` acorda exatamente um bloqueado em `sem_wait`. Com N threads
possivelmente dormindo, são necessários N posts para acordar todas.

---

## 7. Demonstração ao vivo (roteiro pronto)

Preparação: `mingw32-make`, e um `lote.txt` com ~20 comandos variados.

```
servidor 4                        # terminal 1
cliente                           # terminal 2 — modo interativo
  INSERT id=10 nome='Demo'
  SELECT nome WHERE id=10
  UPDATE id=10 nome='Outro'
  LISTAR
  DELETE WHERE id=10
  (Ctrl+Z + Enter)

cliente lote.txt                  # sequencial: #1, #2, #3... em ordem
cliente lote.txt --paralelo       # fora de ordem, cada resposta com [thread N]
```

Falas-chave durante a demo:
1. Sequencial: "um cliente comum — manda, espera, manda. Ordem preservada."
2. Paralelo: "enviei o lote inteiro. A fila do servidor encheu, as threads
   competiram — vejam a #7 chegando antes da #4, atendida pela thread 2."
3. Estatísticas finais: "carga dividida quase igualmente entre as 4 threads:
   é o pool distribuindo trabalho."
4. Ctrl+C / desconexão: "encerramento limpo: um post por thread, join em todas,
   e o banco.txt já estava salvo a cada alteração."

---

## 8. Se o professor perguntar "o que vocês mudariam / melhorariam?"

Respostas honestas (mostram domínio):
- Índice (ex.: unordered_map) em vez de busca linear — mas esconderia a seção
  crítica atrás de estrutura mais complexa, e não é o assunto do trabalho;
- Múltiplos clientes com um laço de aceitação e uma instância de pipe por cliente;
- Transações para agrupar comandos com ordem garantida;
- Portar a camada IPC para FIFO POSIX (já isolada na Parte 1 do banco.h) para
  rodar em Linux/macOS.
