# benchmark.ps1 — matriz de medicoes para o relatorio (ver docs/BENCHMARK.md)
# Uso:  powershell -ExecutionPolicy Bypass -File docs\benchmark.ps1
# Nao altera nenhum codigo: gera lotes de comandos, sobe o servidor,
# cronometra o cliente e exporta resultados.csv.

$ErrorActionPreference = "Stop"
$requisicoes = 2000

# ---- 1) Gera os lotes ----
1..$requisicoes | ForEach-Object {
    "SELECT nome WHERE id=$(( $_ % 3) + 1)"
} | Set-Content lote_select.txt -Encoding ascii

1..$requisicoes | ForEach-Object {
    "UPDATE id=$(( $_ % 3) + 1) nome='bench$_'"
} | Set-Content lote_update.txt -Encoding ascii

Write-Host "== Lotes gerados: $requisicoes requisicoes cada =="

# ---- 2) Matriz: modo x operacao x threads ----
$resultados = @()
foreach ($modo in @("sequencial", "paralelo")) {
    foreach ($lote in @(@("lote_select.txt", "SELECT"),
                        @("lote_update.txt", "UPDATE"))) {
        foreach ($threads in @(1, 2, 4, 8)) {

            # sobe o servidor; ele encerra sozinho quando o cliente desconecta
            $log = "servidor_${modo}_$($lote[1])_${threads}.log"
            $servidor = Start-Process -FilePath ".\servidor.exe" `
                -ArgumentList "$threads" -PassThru `
                -RedirectStandardOutput $log
            Start-Sleep -Milliseconds 800

            # cronometra o cliente
            if ($modo -eq "sequencial") {
                $t = Measure-Command { & .\cliente.exe $lote[0] | Out-Null }
            } else {
                $t = Measure-Command { & .\cliente.exe $lote[0] --paralelo | Out-Null }
            }
            $servidor.WaitForExit()

            $linha = [PSCustomObject]@{
                modo     = $modo
                operacao = $lote[1]
                threads  = $threads
                segundos = [math]::Round($t.TotalSeconds, 3)
            }
            $resultados += $linha
            Write-Host ("{0,-11} {1,-7} {2} threads: {3,8:N3} s" -f `
                $linha.modo, $linha.operacao, $linha.threads, $linha.segundos)
        }
    }
}

# ---- 3) Exporta e orienta ----
$resultados | Export-Csv resultados.csv -NoTypeInformation -Encoding ascii
Write-Host ""
Write-Host "== Pronto: resultados.csv gerado =="
Write-Host "Cole os valores nas tabelas da Secao 5 do docs/RELATORIO.md."
Write-Host "Estatisticas por thread: arquivos servidor_<modo>_<op>_<threads>.log"
