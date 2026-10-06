$ripes = ".\Ripes.exe"
$sizes = 4096, 2097152, 4194304, 8388608, 16777216
"N,run,peak_ws_bytes" | Set-Content result.csv
foreach ($n in $sizes) {
  (Get-Content fill.s) -replace 'SIZE', $n | Set-Content f.s -Encoding ascii
  foreach ($r in 1..3) {
    $p = Start-Process $ripes -PassThru -NoNewWindow `
         -ArgumentList '--mode cli --src f.s -t asm --proc RV32_ISS' `
         -RedirectStandardOutput out.txt
    $peak = 0
    while (-not $p.HasExited) {
      $p.Refresh()
      try { $peak = [math]::Max($peak, $p.PeakWorkingSet64) } catch {}
      Start-Sleep -Milliseconds 20
    }
    "$n,$r,$peak" | Add-Content result.csv
    "$n run $r : $peak"
  }
}