# Sistema de Processamento Paralelo de Requisições a um Banco de Dados

Trabalho M1 – Sistemas Operacionais (UNIVALI)
Tema: **IPC, Threads e Paralelismo**

Simulação do núcleo de um SGBD: um processo **cliente** envia requisições
(`INSERT`, `SELECT`, `UPDATE`, `DELETE`) por **IPC real**, e um processo
**servidor** as processa em paralelo com um **pool de threads**, protegendo a
tabela compartilhada com **mutex e semáforos**.

O enunciado completo está em [`docs/`](docs/).

## Estrutura

```
include/    cabeçalhos (protocolo, canal IPC, banco, fila, log)
src/        implementação (servidor, cliente, camada IPC POSIX e Windows)
scripts/    scripts de execução e de carga para os experimentos
docs/       enunciado e relatório
dados/      estado persistido do banco simulado
log/        log de execução do servidor
```

## Status

Em desenvolvimento.
