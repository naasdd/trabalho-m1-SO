# benchmark 3 rodadas — 2000 req por config, 16 configs x 3 = 48 execucoes
$ErrorActionPreference = "Stop"
$requisicoes = 2000
$rodadas = 3

# garante estado inicial do banco antes de gerar/rodar
"1;Joao Silva", "2;Maria Souza", "3;Ana Paula" | Set-Content banco.txt -Encoding ascii

1..$requisicoes | ForEach-Object { "SELECT nome WHERE id=$(( $_ % 3) + 1)" } | Set-Content lote_select.txt -Encoding ascii
1..$requisicoes | ForEach-Object { "UPDATE id=$(( $_ % 3) + 1) nome='bench$_'" } | Set-Content lote_update.txt -Encoding ascii
Write-Host "== Lotes gerados: $requisicoes requisicoes cada =="

# mata servidor orfao de rodada anterior, se houver
Get-Process servidor -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

$resultados = @()
for ($r = 1; $r -le $rodadas; $r++) {
  Write-Host ""
  Write-Host "===== RODADA $r/$rodadas ====="
  foreach ($modo in @("sequencial", "paralelo")) {
    foreach ($lote in @(@("lote_select.txt", "SELECT"), @("lote_update.txt", "UPDATE"))) {
      foreach ($threads in @(1, 2, 4, 8)) {
        # restaura banco para UPDATE comecar sempre do mesmo estado
        "1;Joao Silva", "2;Maria Souza", "3;Ana Paula" | Set-Content banco.txt -Encoding ascii
        Start-Sleep -Milliseconds 200

        $log = "servidor_${modo}_$($lote[1])_${threads}_r${r}.log"
        $servidor = Start-Process -FilePath ".\servidor.exe" -ArgumentList "$threads" -PassThru -RedirectStandardOutput $log
        Start-Sleep -Milliseconds 1000
        if ($servidor.HasExited) {
          Write-Host "ERRO: servidor saiu antes do cliente (ver $log)"
          Get-Content $log | Write-Host
          throw "servidor falhou"
        }

        try {
          if ($modo -eq "sequencial") {
            $t = Measure-Command { & .\cliente.exe $lote[0] | Out-Null }
          } else {
            $t = Measure-Command { & .\cliente.exe $lote[0] --paralelo | Out-Null }
          }
        } finally {
          if (-not $servidor.WaitForExit(15000)) {
            Write-Host "WARN: servidor nao encerrou, matando..."
            $servidor | Stop-Process -Force
          }
        }
        $seg = [math]::Round($t.TotalSeconds, 3)
        $resultados += [PSCustomObject]@{ rodada = $r; modo = $modo; operacao = $lote[1]; threads = $threads; segundos = $seg }
        Write-Host ("R{0} {1,-11} {2,-7} {3} threads: {4,8:N3} s" -f $r, $modo, $lote[1], $threads, $seg)
      }
    }
  }
}

$resultados | Export-Csv resultados_long.csv -NoTypeInformation -Encoding ascii
# pivota para CSV final com media
$final = $resultados | Group-Object modo, operacao, threads | ForEach-Object {
  $g = $_.Group | Sort-Object rodada
  $vals = @($g | Select-Object -ExpandProperty segundos)
  $media = [math]::Round(($vals | Measure-Object -Average).Average, 3)
  [PSCustomObject]@{
    modo = $g[0].modo; operacao = $g[0].operacao; threads = $g[0].threads
    rodada1 = $vals[0]; rodada2 = $vals[1]; rodada3 = $vals[2]; media = $media
  }
} | Sort-Object modo, operacao, threads
$final | Export-Csv resultados.csv -NoTypeInformation -Encoding ascii
$final | Format-Table -AutoSize | Out-String | Write-Host
Write-Host "== Pronto: resultados.csv + resultados_long.csv =="
