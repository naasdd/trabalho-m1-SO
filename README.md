# Trabalho M1 — Sistemas Operacionais
## Sistema de Processamento Paralelo de Requisições a um Banco de Dados

Sistema cliente-servidor em **C++17** demonstrando **IPC com FIFOs nomeados**,
**pool de threads** e **sincronização com mutex / shared_mutex / condition
variable**, conforme enunciado da disciplina de Sistemas Operacionais (Univali).

---

## Arquitetura

```
cliente 1 ─┐
cliente 2 ─┼──> /tmp/db_req (FIFO) ──> servidor ──> fila ──> pool de workers ──> banco
cliente N ─┘     (pedidos)               │                                    (shared_mutex)
                                          │
   <── /tmp/db_resp_<pid> (FIFO privado por cliente)
```

- **Cliente**: envia requisições (`INSERT/SELECT/UPDATE/DELETE`) pelo FIFO
  global de pedidos e lê respostas no seu FIFO privado (`/tmp/db_resp_<pid>`).
- **Servidor**: thread principal lê o FIFO de pedidos e despacha cada pedido
  para uma **fila produtor-consumidor**; um **pool de N worker threads**
  processa em paralelo e responde no FIFO do cliente.
- **Banco**: vetor de `Registro{id, nome}` protegido por `std::shared_mutex`
  (leitores-escritores): `SELECT`s em paralelo, escritas exclusivas.
  Persistência em `banco.txt` (carrega no início, salva no encerramento).

## Estrutura

```
src/
  protocolo.hpp   # formato das mensagens IPC (pid|OP|id|nome)
  banco.hpp/.cpp  # tabela + CRUD + shared_mutex + persistência
  pool.hpp/.cpp   # pool de threads + fila mutex/condition_variable
  servidor.cpp    # processo servidor (IPC, pool, despacho)
  cliente.cpp     # processo cliente (interativo + benchmark)
Makefile          # compila servidor e cliente
banco.txt         # dados persistidos (id;nome por linha)
```

## Compilação e execução

```bash
make                  # gera ./servidor e ./cliente

# Terminal 1 — sobe o servidor (padrão: 4 threads; ajuste opcional)
./servidor 4

# Terminal 2 — cliente interativo
./cliente
> INSERT 10 Joao
> SELECT 10
> UPDATE 10 Maria
> DELETE 10
> sair

# Benchmark sequencial (latência ping-pong)
./cliente -n 1000 SELECT 1

# Benchmark "burst" (throughput; dispara tudo de uma vez com
# thread leitora separada — evita deadlock de FIFO cheio)
./cliente -b 4000 SELECT 1

# Para medir o efeito do número de threads: rode o servidor com
# 1, 2, 4 e 8 threads e compare o tempo do benchmark.
```

## Conceitos de SO demonstrados

- **IPC real**: `mkfifo`, `open`, `read`, `write` (FIFOs nomeados POSIX) —
  não variáveis globais nem arquivos comuns com polling.
- **Threads**: `std::thread` (pool fixo, sem criar/destruir por requisição).
- **Exclusão mútua**: `std::mutex` protegendo a fila; `std::shared_mutex`
  protegendo a tabela (padrão leitores-escritores).
- **Condition variable**: workers dormem quando a fila está vazia
  (sem busy waiting).
- **Tratamento de sinais**: `SIGINT`/`SIGTERM` para encerramento limpo
  (salva `banco.txt`, faz `join` nas threads).
