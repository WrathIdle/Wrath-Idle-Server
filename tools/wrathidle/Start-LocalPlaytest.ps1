[CmdletBinding()]
param(
    [string]$Workspace = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path,
    [string]$ClientDirectory = $env:WRATHIDLE_ASSET_CLIENT,
    [switch]$ServerOnly,
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
$workspacePath = (Resolve-Path -LiteralPath $Workspace).Path
$mysqlDirectory = Join-Path $workspacePath '.tools/mysql-8.4.13-winx64'
$serverDirectory = Join-Path $workspacePath 'Wrath-Idle-Server/build/bin/Release'
$configDirectory = Join-Path $workspacePath 'runtime/server/etc'
$logDirectory = Join-Path $workspacePath 'runtime/logs'
$clientExecutable = Join-Path $ClientDirectory 'Wow.exe'
$required = @(
    "$mysqlDirectory/bin/mysqld.exe",
    "$serverDirectory/authserver.exe",
    "$serverDirectory/worldserver.exe",
    "$serverDirectory/libmysql.dll",
    "$serverDirectory/libssl-3-x64.dll",
    "$serverDirectory/libcrypto-3-x64.dll",
    "$configDirectory/authserver.conf",
    "$configDirectory/worldserver.conf",
    "$configDirectory/configs/modules/wrathidle.conf",
    "$configDirectory/configs/modules/playerbots.conf",
    "$workspacePath/runtime/mysql-data/mysql",
    "$workspacePath/runtime/server/ac-data-v20/mmaps",
    $clientExecutable,
    "$ClientDirectory/Data/enUS/patch-enUS-W.MPQ"
)
foreach ($path in $required) {
    if (!(Test-Path -LiteralPath $path)) { throw "Missing playtest prerequisite: $path" }
}

function Get-ServiceListener([int]$Port, [string]$Executable) {
    $listener = Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (!$listener) { return $false }
    $owner = Get-Process -Id $listener.OwningProcess -ErrorAction Stop
    if ($owner.Path -ne [IO.Path]::GetFullPath($Executable)) {
        throw "Port $Port is already used by another program. No existing process was changed."
    }
    return $true
}

function Start-ServiceIfNeeded([string]$Name, [string]$Executable, [string]$WorkingDirectory,
    [string[]]$Arguments, [int]$Port) {
    if (Get-ServiceListener $Port $Executable) {
        Write-Host "$Name is already running."
        return
    }
    $existing = Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($Executable)) -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -eq [IO.Path]::GetFullPath($Executable) } | Select-Object -First 1
    if ($existing) {
        $started = $existing
        Write-Host "Waiting for the existing $Name process..."
    } else {
        Write-Host "Starting $Name..."
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
        $started = Start-Process -FilePath $Executable -WorkingDirectory $WorkingDirectory -ArgumentList $Arguments `
            -WindowStyle Hidden -PassThru -RedirectStandardOutput "$logDirectory/$Name-$stamp.out.log" `
            -RedirectStandardError "$logDirectory/$Name-$stamp.error.log"
    }
    $deadline = (Get-Date).AddMinutes(3)
    while ((Get-Date) -lt $deadline) {
        $started.Refresh()
        if ($started.HasExited) { throw "$Name exited. See $logDirectory for details." }
        if (Get-ServiceListener $Port $Executable) { return }
        Start-Sleep -Milliseconds 500
    }
    throw "$Name is still starting. See $logDirectory; run this launcher again once it is ready."
}

if ($CheckOnly) {
    Write-Host 'All required local playtest files are present. No programs were started.'
    return
}
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
Start-ServiceIfNeeded 'database' "$mysqlDirectory/bin/mysqld.exe" $workspacePath @(
    '--no-defaults', "--basedir=`"$mysqlDirectory`"", "--datadir=`"$workspacePath/runtime/mysql-data`"",
    '--port=3308', '--bind-address=127.0.0.1', '--mysqlx=0', '--console'
) 3308
# Auth marks all realms offline at startup. Start it first, then let the world
# server advertise readiness; reversing this order hides a healthy local realm.
Start-ServiceIfNeeded 'login' "$serverDirectory/authserver.exe" $configDirectory @('-c','authserver.conf') 3724
Start-ServiceIfNeeded 'world' "$serverDirectory/worldserver.exe" $configDirectory @('-c','worldserver.conf') 8086
if ($ServerOnly) { Write-Host 'Local WrathIdle servers are ready.'; return }
Write-Host 'Opening the original Wrath client. Log in and select your character; the WrathIdle command center opens in game.'
Write-Host 'The local servers keep running when you close the client.'
$existingClient = Get-Process -Name Wow -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $clientExecutable }
if ($existingClient) { Write-Host 'The original client is already running.'; return }
Start-Process -FilePath $clientExecutable -WorkingDirectory $ClientDirectory -WindowStyle Hidden
