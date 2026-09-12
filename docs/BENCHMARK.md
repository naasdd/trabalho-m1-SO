# Benchmark — Como Medir e Preencher o Relatório

Guia para rodar as medições no **Windows** (desktop) e preencher as tabelas da
Seção 5 do `docs/RELATORIO.md`. Não é necessário alterar **nenhuma linha do
código**: o cliente já aceita arquivos de comandos, e o tempo é medido por fora
com `Measure-Command` do PowerShell.

---

## 1. Preparação

```powershell
cd <pasta do projeto>
mingw32-make          # gera servidor.exe e cliente.exe
```

Confira que o `banco.txt` está no estado inicial:

```
1;Joao Silva
2;Maria Souza
3;Ana Paula
```

(Se estiver diferente após os UPDATEs do benchmark, restaure com
`git checkout -- banco.txt`.)

## 2. Script pronto

Tudo já está no arquivo **`docs/benchmark.ps1`**. Ele:

1. gera dois lotes com 2000 requisições cada (um de SELECT, um de UPDATE);
2. roda a matriz completa: {sequencial, paralelo} × {SELECT, UPDATE} × {1, 2, 4, 8} threads;
3. sobe o servidor antes de cada medição e espera ele encerrar sozinho
   (ele termina quando o cliente desconecta);
4. imprime cada resultado na tela e no final gera **`resultados.csv`**;
5. salva o log de cada servidor (com as estatísticas por thread) em arquivos
   `servidor_<modo>_<op>_<threads>.log`.

Execução (o parâmetro de política é para liberar script no Windows):

```powershell
powershell -ExecutionPolicy Bypass -File docs\benchmark.ps1
```

Duração: poucos minutos (2000 requisições por configuração).

**Importante:** rode a matriz **2 ou 3 vezes** e use a média (ou a segunda
rodada) — a primeira costuma sair mais lenta por cache frio de disco. Os valores
entre rodadas variam um pouco; é normal.

## 3. Medição manual (se quiser conferir um caso isolado)

```powershell
# gera um lote de 2000 SELECTs sobre os ids 1..3
1..2000 | ForEach-Object { "SELECT nome WHERE id=$(( $_ % 3) + 1)" } |
    Set-Content lote_select.txt -Encoding ascii

# sobe o servidor com 4 threads em janela separada
Start-Process .\servidor.exe -ArgumentList 4

# cronometra o cliente no modo paralelo
Measure-Command { .\cliente.exe lote_select.txt --paralelo | Out-Null }
```

## 4. Preenchendo o RELATORIO.md

Abra o `resultados.csv` (abre no Excel/Sheets) e copie os valores:

| Tabela do relatório | Linhas do CSV |
|---|---|
| Tabela 1 (SELECT paralelo) | modo=paralelo, operacao=SELECT |
| Tabela 2 (UPDATE paralelo) | modo=paralelo, operacao=UPDATE |
| Tabela 3 (SELECT sequencial) | modo=sequencial, operacao=SELECT (usar 1 e 4 threads) |
| Seção 5.4 (distribuição por thread) | arquivos `servidor_paralelo_*_*.log` |

### Gráficos no Google Sheets

1. Importe o `resultados.csv` (ou cole os valores);
2. Filtre só as linhas `modo=paralelo`;
3. Selecione as colunas `threads` e `segundos` de SELECT e UPDATE;
4. Inserir → Gráfico → **Gráfico de linhas**: eixo X = threads, eixo Y = segundos.
   Duas linhas (SELECT e UPDATE) no mesmo gráfico;
5. Opcional: segundo gráfico comparando sequencial 1 vs 4 threads
   (mostra que sem rajada não há paralelismo a explorar).

## 5. O que esperar (e como analisar honestamente)

- **Modo sequencial:** tempo praticamente igual para 1, 2, 4 ou 8 threads —
  o cliente entrega uma tarefa por vez (ping-pong), então não há trabalho
  simultâneo a dividir. O tempo é dominado pela **latência** do canal;
- **Modo paralelo:** ganho crescente com o número de threads até estabilizar.
  SELECT tende a escalar melhor que UPDATE, porque cada UPDATE regrava o
  `banco.txt` **dentro da seção crítica** — as escritas se serializam no mutex
  (e no disco), e isso é um ponto excelente para a análise do relatório;
- **Distribuição por thread:** nos logs, as requisições devem aparecer divididas
  quase igualmente entre as threads — evidência de que o pool distribui a carga.

Se os números não saírem exatamente assim, reporte o que mediu — dados reais com
variação analisada honestamente valem mais do que números "perfeitos".
