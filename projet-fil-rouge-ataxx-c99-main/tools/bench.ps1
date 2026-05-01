# tools/bench.ps1 — Minimax complexity benchmark (Windows / PowerShell)
#
# Mesure le nombre de noeuds visités et le temps écoulé en fonction
# de la profondeur de recherche. Produit bench_results.csv exploitable
# dans Python / Excel pour la section "analyse de complexité" du rapport.
#
# Usage :
#   powershell -ExecutionPolicy Bypass -File tools\bench.ps1
#   powershell -ExecutionPolicy Bypass -File tools\bench.ps1 -MinDepth 1 -MaxDepth 6
#
# Prérequis : MSYS2 / MinGW dans le PATH (make + gcc disponibles).

param(
    [int]$MinDepth  = 1,
    [int]$MaxDepth  = 7,
    [int]$BoardSize = 5,
    [int]$MoveLimit = 50
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Continue'

# ── Racine du projet ───────────────────────────────────────────────────
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root      = Split-Path -Parent $ScriptDir
Push-Location $Root

try {

# ── Build ──────────────────────────────────────────────────────────────
Write-Host '=== Building ===' -ForegroundColor Cyan
& make ataxx_tournament
& make student_plugin SRC=agent_minimax.c NAME=minimax
& make agent_random_plugin 2>$null   # optionnel
Write-Host 'Build OK' -ForegroundColor Green
Write-Host ''

# ── CSV ────────────────────────────────────────────────────────────────
$CSV = 'bench_results.csv'
'depth,moves,total_nodes,avg_nodes,max_nodes,min_nodes,time_s,nodes_per_s' |
    Set-Content -Encoding UTF8 $CSV

# ── En-têtes ───────────────────────────────────────────────────────────
Write-Host ("Ataxx Minimax Benchmark  |  plateau {0}x{0}  |  limite {1} coups" `
    -f $BoardSize, $MoveLimit)
Write-Host ("Profondeurs {0}..{1}" -f $MinDepth, $MaxDepth)
Write-Host ''

$Fmt = '{0,-7} {1,8} {2,16} {3,14} {4,14} {5,10} {6,14}'
Write-Host ($Fmt -f 'Prof.','Coups','TotalNoeuds','MoyNoeuds','MaxNoeuds','Temps(s)','Noeuds/s')
Write-Host ($Fmt -f '-----','------','-----------','---------','---------','--------','--------')

# ── Lancement du tournoi avec capture de stderr ─────────────────────────
function Invoke-Tournament {
    param([int]$Depth, [string]$ReportPath)

    $pinfo = [System.Diagnostics.ProcessStartInfo]::new()
    $pinfo.FileName               = '.\ataxx_tournament.exe'
    $pinfo.Arguments              = "--size $BoardSize --limit $MoveLimit --depth $Depth --output `"$ReportPath`""
    $pinfo.RedirectStandardOutput = $true
    $pinfo.RedirectStandardError  = $true
    $pinfo.UseShellExecute        = $false
    $pinfo.CreateNoWindow         = $true

    $proc = [System.Diagnostics.Process]::new()
    $proc.StartInfo = $pinfo

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    [void]$proc.Start()

    # Lire stdout et stderr de façon asynchrone pour éviter les deadlocks
    $stdoutTask = $proc.StandardOutput.ReadToEndAsync()
    $stderrTask = $proc.StandardError.ReadToEndAsync()
    $proc.WaitForExit()
    $sw.Stop()

    return @{
        Stderr = $stderrTask.Result
        Ms     = $sw.ElapsedMilliseconds
    }
}

# ── Boucle principale ──────────────────────────────────────────────────
for ($d = $MinDepth; $d -le $MaxDepth; $d++) {

    # Fichier temporaire pour le rapport HTML (ignoré)
    $TmpRep = Join-Path $env:TEMP "ataxx_bench_d${d}.html"

    $result    = Invoke-Tournament -Depth $d -ReportPath $TmpRep
    Remove-Item $TmpRep -Force -ErrorAction SilentlyContinue

    $ElapsedMs = $result.Ms
    $ElapsedS  = [math]::Round($ElapsedMs / 1000.0, 3)
    $Stderr    = $result.Stderr

    # Extraire les compteurs de noeuds depuis les lignes [minimax]
    # Format attendu : [minimax] depth=N nodes=M score=S tt_entries=K
    $NodesList = [System.Collections.Generic.List[long]]::new()
    foreach ($line in ($Stderr -split "`n")) {
        if ($line -match 'nodes=(\d+)') {
            $NodesList.Add([long]$Matches[1])
        }
    }

    if ($NodesList.Count -eq 0) {
        Write-Host ("{0,-7}  aucune sortie minimax — plugins\agent_minimax.dll est-il compilé ?" `
            -f $d) -ForegroundColor Yellow
        continue
    }

    $Moves = $NodesList.Count
    $Total = ($NodesList | Measure-Object -Sum).Sum
    $Avg   = [math]::Round($Total / $Moves)
    $Max   = ($NodesList | Measure-Object -Maximum).Maximum
    $Min   = ($NodesList | Measure-Object -Minimum).Minimum
    $NPS   = if ($ElapsedMs -gt 0) { [long]($Total * 1000 / $ElapsedMs) } else { '-' }

    Write-Host ($Fmt -f $d, $Moves, $Total, $Avg, $Max, "${ElapsedS}s", $NPS)

    ("{0},{1},{2},{3},{4},{5},{6},{7}" -f $d,$Moves,$Total,$Avg,$Max,$Min,$ElapsedS,$NPS) |
        Add-Content -Encoding UTF8 $CSV
}

# ── Résumé ─────────────────────────────────────────────────────────────
Write-Host ''
Write-Host ("Résultats écrits dans $CSV") -ForegroundColor Green
Write-Host ''
Write-Host 'Analyse de complexité :'
Write-Host '  - Minimax pur           : O(b^d)'
Write-Host '  - Alpha-beta (cas moyen): O(b^(3d/4))'
Write-Host '  - Alpha-beta (meilleur) : O(b^(d/2))'
Write-Host '  où b = facteur de branchement moyen, d = profondeur.'
Write-Host ''
Write-Host "Conseil : tracer avg_nodes en fonction de depth sur échelle logarithmique"
Write-Host "  => la pente donne log(b_eff). Comparer à la borne théorique."

} finally {
    Pop-Location
}
