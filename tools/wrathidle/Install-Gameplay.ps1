[CmdletBinding()]
param([string]$ClientDirectory = $env:WRATHIDLE_ASSET_CLIENT, [switch]$ServerOnlyUpdate)
$ErrorActionPreference = 'Stop'
$server = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$workspace = Split-Path $server
$staged = Join-Path $workspace '.tools/gameplay-stage/worldserver.exe'
$installed = Join-Path $server 'build/bin/Release/worldserver.exe'
$clientExe = Join-Path $ClientDirectory 'Wow.exe'
$config = Join-Path $workspace 'runtime/server/etc'
if (!(Test-Path -LiteralPath $staged)) { throw 'Build and verify the staged server first.' }
$processSnapshot = @(Get-CimInstance Win32_Process)
$processIds = @{}
foreach ($entry in $processSnapshot) { $processIds[$entry.ProcessId] = $true }
$activeBuilds = @($processSnapshot | Where-Object {
    if ($_.Name -notin @('MSBuild.exe', 'cl.exe', 'cmake.exe', 'link.exe')) { return $false }
    # Reused MSBuild workers can outlive a completed build. Compiler/linker children still block above.
    if ($_.Name -eq 'MSBuild.exe' -and $_.CommandLine -match '/nodemode:' -and
        !$processIds.ContainsKey($_.ParentProcessId)) { return $false }
    return $true
})
if ($activeBuilds.Count) {
    throw 'A build is running; installation was not started.'
}
if ($ServerOnlyUpdate) {
    $ui = Get-Content -LiteralPath "$workspace/.tools/native-ui/manifest.json" -Raw | ConvertFrom-Json
    if (!$ui.installed -or (Get-FileHash -LiteralPath $ui.archive).Hash -ne $ui.archive_sha256) {
        throw 'The installed interface is not verified; use a matching full installation.'
    }
    foreach ($source in Get-ChildItem -LiteralPath "$server/modules/mod-wrathidle/native/Interface/FrameXML/WrathIdle" -Filter '*.lua') {
        $resource = 'Interface\FrameXML\WrathIdle\' + $source.Name
        if ($ui.resources.PSObject.Properties[$resource].Value -ne (Get-FileHash -LiteralPath $source.FullName).Hash) {
            throw 'Native interface sources changed; a full matching installation is required.'
        }
    }
}
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$backup = Join-Path $workspace ".tools/gameplay-deployment-$stamp"
New-Item -ItemType Directory -Path $backup | Out-Null
Copy-Item -LiteralPath $installed -Destination (Join-Path $backup 'worldserver.exe')
$oldHash = (Get-FileHash -LiteralPath $installed).Hash
$newHash = (Get-FileHash -LiteralPath $staged).Hash
$record = @{ server = $installed; previousHash = $oldHash; installedHash = $newHash;
    backup = $backup; phase = 'prepared'; uiManifest = "$workspace/.tools/native-ui/manifest.json";
    serverOnlyUpdate = $ServerOnlyUpdate.IsPresent }
if (Test-Path -LiteralPath "$staged.map") {
    $installedMap = Join-Path $backup 'installed-worldserver.exe.map'
    Copy-Item -LiteralPath "$staged.map" -Destination $installedMap
    $record.installedMap = $installedMap
}
$record | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'deployment.json') -Encoding utf8
# Only touch processes belonging to this workspace/client installation.
$clients = @()
if (!$ServerOnlyUpdate) {
    $clients = @(Get-Process -Name Wow -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $clientExe })
}
foreach ($client in $clients) {
    $null = $client.CloseMainWindow()
    if (!$client.WaitForExit(15000)) { Stop-Process -Id $client.Id }
}
$worlds = @(Get-Process -Name worldserver -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -eq $installed })
foreach ($world in $worlds) {
    Stop-Process -Id $world.Id
    $world.WaitForExit()
}
$uiInstalled = $false
try {
    if (!$ServerOnlyUpdate) {
        & (Join-Path $PSScriptRoot 'Install-NativeUI.ps1') -ClientDirectory $ClientDirectory
        $uiInstalled = $true
    }
    Copy-Item -LiteralPath $staged -Destination $installed -Force
    if ((Get-FileHash -LiteralPath $installed).Hash -ne $newHash) { throw 'Server installation hash mismatch.' }
    $record.phase = 'installed'
    $record | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'deployment.json') -Encoding utf8
    & (Join-Path $PSScriptRoot 'Start-LocalPlaytest.ps1') -ClientDirectory $ClientDirectory -ServerOnly
    $record.phase = 'server-ready'
    $record | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'deployment.json') -Encoding utf8
    Write-Host "Matching native UI and server installed. Rollback files: $backup"
} catch {
    $failure = $_
    Get-Process -Name worldserver -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -eq $installed } | Stop-Process
    Copy-Item -LiteralPath (Join-Path $backup 'worldserver.exe') -Destination $installed -Force
    if ($uiInstalled) {
        $ui = Get-Content -LiteralPath $record.uiManifest -Raw | ConvertFrom-Json
        Copy-Item -LiteralPath (Join-Path $ui.backup 'Wow.exe') -Destination $clientExe -Force
        $previousMpq = Join-Path $ui.backup ([IO.Path]::GetFileName($ui.archive))
        if (Test-Path -LiteralPath $previousMpq) {
            Copy-Item -LiteralPath $previousMpq -Destination $ui.archive -Force
        } elseif (Test-Path -LiteralPath $ui.archive) {
            Remove-Item -LiteralPath $ui.archive
        }
        $addon = Join-Path $ui.backup 'WrathIdle-addon'
        if (Test-Path -LiteralPath $addon) {
            Move-Item -LiteralPath $addon -Destination (Join-Path $ClientDirectory 'Interface/AddOns/WrathIdle')
        }
    }
    $record.phase = 'rolled-back'
    $record | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'deployment.json') -Encoding utf8
    if ($worlds.Count) {
        Start-Process -FilePath $installed -ArgumentList @('-c','worldserver.conf') -WorkingDirectory $config `
            -WindowStyle Hidden -RedirectStandardOutput "$backup/rollback-server.out.log" `
            -RedirectStandardError "$backup/rollback-server.error.log"
    }
    throw $failure
}
