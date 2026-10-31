[CmdletBinding()]
param([string]$PreviousClient = '')
$ErrorActionPreference = 'Stop'
$server = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$workspace = Split-Path $server
$clientDirectory = Join-Path $workspace 'Wrath-Idle-Client/bin/Win'
$installed = Join-Path $server 'build/bin/Release/worldserver.exe'
$clientExe = Join-Path $clientDirectory 'WowClient64.exe'
$stagedClient = Join-Path $workspace '.tools/gameplay-stage/WowClient64.exe'
$staged = Join-Path $workspace '.tools/gameplay-stage/worldserver.exe'
foreach ($path in @($clientExe, $stagedClient, $staged, "$clientDirectory/Data")) {
    if (!(Test-Path -LiteralPath $path)) { throw "Missing deployment prerequisite: $path" }
}
if (Get-Process -Name cmake, MSBuild, cl, link -ErrorAction SilentlyContinue) {
    throw 'A build is active. Finish building both artifacts before deploying.'
}
$serverWire = Join-Path $server 'modules/mod-wrathidle/src/IdleWire.h'
$clientWire = Join-Path $workspace 'Wrath-Idle-Client/WoW/Source/Ui/WrathIdleWire.h'
if ((Get-FileHash -LiteralPath $serverWire).Hash -ne (Get-FileHash -LiteralPath $clientWire).Hash) {
    throw 'Client and server packet contracts differ. Update both before deploying.'
}
$backup = Join-Path $workspace ('.tools/native-packets-deployment-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $backup | Out-Null
Copy-Item -LiteralPath $installed -Destination (Join-Path $backup 'worldserver.exe')
if (!$PreviousClient) { $PreviousClient = $clientExe }
Copy-Item -LiteralPath $PreviousClient -Destination (Join-Path $backup 'WowClient64.exe')
$processes = @(Get-Process -Name WowClient64, worldserver -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -in @($clientExe, $installed) })
$restartClient = @($processes | Where-Object { $_.Path -eq $clientExe }).Count -gt 0
foreach ($process in $processes) {
    $null = $process.CloseMainWindow()
    if (!$process.WaitForExit(15000)) { Stop-Process -Id $process.Id; $process.WaitForExit() }
}
# Data can be a junction to an external asset installation. Retire only the managed archive.
$archive = Join-Path $clientDirectory 'Data/enUS/patch-enUS-W.MPQ'
if (Test-Path -LiteralPath $archive) {
    Move-Item -LiteralPath $archive -Destination (Join-Path $backup 'patch-enUS-W.MPQ')
}
try {
    Copy-Item -LiteralPath $staged -Destination $installed -Force
    Copy-Item -LiteralPath $stagedClient -Destination $clientExe -Force
    if ((Get-FileHash -LiteralPath $installed).Hash -ne (Get-FileHash -LiteralPath $staged).Hash) {
        throw 'Server installation hash mismatch.'
    }
    if ((Get-FileHash -LiteralPath $clientExe).Hash -ne (Get-FileHash -LiteralPath $stagedClient).Hash) {
        throw 'Client installation hash mismatch.'
    }
    $record = @{
        serverCommit = (& git -C $server rev-parse HEAD)
        clientCommit = (& git -C (Join-Path $workspace 'Wrath-Idle-Client') rev-parse HEAD)
        serverLocalModifications = @(& git -C $server status --porcelain)
        clientLocalModifications = @(& git -C (Join-Path $workspace 'Wrath-Idle-Client') status --porcelain)
        clientBuildIdentity = @(Get-Content -LiteralPath (Join-Path $workspace 'Wrath-Idle-Client/build/x64/generated/WaystoneVersion.h'))
        serverBuildIdentity = @(Get-Content -LiteralPath (Join-Path $server 'build/revision.h'))
        packetContractHash = (Get-FileHash -LiteralPath $serverWire).Hash
        serverHash = (Get-FileHash -LiteralPath $installed).Hash
        clientHash = (Get-FileHash -LiteralPath $clientExe).Hash
        stoppedProcessIds = @($processes.Id)
    }
    $record | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'deployment.json')
    & (Join-Path $PSScriptRoot 'Start-LocalPlaytest.ps1') -Workspace $workspace -ServerOnly
    if ($restartClient) { Start-Process -FilePath $clientExe -WorkingDirectory $clientDirectory -WindowStyle Hidden }
    Write-Host "Matching game versions deployed. Record saved under $backup"
} catch {
    Get-Process -Name worldserver -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -eq $installed } | Stop-Process
    Get-Process -Name WowClient64 -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -eq $clientExe } | Stop-Process
    Copy-Item -LiteralPath (Join-Path $backup 'worldserver.exe') -Destination $installed -Force
    if (Test-Path -LiteralPath (Join-Path $backup 'WowClient64.exe')) {
        Copy-Item -LiteralPath (Join-Path $backup 'WowClient64.exe') -Destination $clientExe -Force
    }
    if (Test-Path -LiteralPath (Join-Path $backup 'patch-enUS-W.MPQ')) {
        Move-Item -LiteralPath (Join-Path $backup 'patch-enUS-W.MPQ') -Destination $archive
    }
    throw
}
