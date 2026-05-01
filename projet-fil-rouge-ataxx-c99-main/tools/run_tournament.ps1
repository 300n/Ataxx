param(
    [string[]]$Agents = @("student", "random", "blackbox"),
    [int]$Rounds = 2
)

$results = @()

foreach ($agent in $Agents) {
    foreach ($opponent in $Agents) {
        if ($agent -eq $opponent) {
            continue
        }

        for ($round = 1; $round -le $Rounds; $round++) {
            $results += [pscustomobject]@{
                Agent = $agent
                Opponent = $opponent
                Round = $round
                Status = "TODO"
            }
        }
    }
}

$results | Format-Table -AutoSize

Write-Host "Adapter ce script au moteur officiel et au protocole du bot noir avant la campagne de notation."