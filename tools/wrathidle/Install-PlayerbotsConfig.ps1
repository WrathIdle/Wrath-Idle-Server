[CmdletBinding()]
param(
    [string]$Workspace = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path,
    [string]$DatabaseName = 'acore_playerbots',
    [switch]$ProvisionDatabase
)
$ErrorActionPreference = 'Stop'
if ($DatabaseName -notmatch '^[a-zA-Z0-9_]+$') { throw 'Invalid Playerbots database name.' }
$server = Join-Path $Workspace 'Wrath-Idle-Server'
$configDirectory = Join-Path $Workspace 'runtime/server/etc'
$destination = Join-Path $configDirectory 'configs/modules/playerbots.conf'
$worldConfig = Get-Content (Join-Path $configDirectory 'worldserver.conf') -Raw
$connectionMatch = [regex]::Match($worldConfig, '(?m)^CharacterDatabaseInfo\s*=\s*"([^"]+)"')
if (!$connectionMatch.Success) { throw 'Cannot read the existing character database connection.' }
$connection = $connectionMatch.Groups[1].Value.Split(';')
if ($connection.Length -lt 5) { throw 'Unexpected database connection format.' }
$connection[4] = $DatabaseName
if (Test-Path -LiteralPath $destination) {
    $content = Get-Content -LiteralPath $destination -Raw
    Copy-Item -LiteralPath $destination -Destination "$destination.before-wrathidle-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
} else {
    $content = Get-Content (Join-Path $server 'modules/mod-playerbots/conf/playerbots.conf.dist') -Raw
}
$options = [ordered]@{
    'Playerbots.Enabled' = '1'
    'Playerbots.RandomBotAutologin' = '0'
    'Playerbots.BotAutologin' = '0'
    'Playerbots.MinRandomBots' = '0'
    'Playerbots.MaxRandomBots' = '0'
    'Playerbots.RandomBotAccountCount' = '0'
    'Playerbots.AddClassAccountPoolSize' = '0'
    'Playerbots.RandomBotGuildCount' = '0'
    'Playerbots.RandomBotArenaTeam2v2Count' = '0'
    'Playerbots.RandomBotArenaTeam3v3Count' = '0'
    'Playerbots.RandomBotArenaTeam5v5Count' = '0'
    'Playerbots.DeleteRandomBotAccounts' = '0'
    'Playerbots.DeleteRandomBotGuilds' = '0'
    'Playerbots.DeleteRandomBotArenaTeams' = '0'
    'Playerbots.SelfBotLevel' = '0'
    'Playerbots.BotCheats' = '""'
    'Playerbots.AutoDoQuests' = '0'
    'Playerbots.AutoLearnQuestSpells' = '0'
    'PlayerbotsDatabaseInfo' = '"' + ($connection -join ';') + '"'
}
foreach ($key in $options.Keys) {
    $pattern = '(?m)^' + [regex]::Escape($key) + '\s*=.*$'
    $line = $key + ' = ' + $options[$key]
    if ([regex]::IsMatch($content, $pattern)) {
        $content = [regex]::Replace($content, $pattern, [System.Text.RegularExpressions.MatchEvaluator]{ param($match) $line })
    } else {
        $content += "`n$line`n"
    }
}
New-Item -ItemType Directory -Force (Split-Path $destination) | Out-Null
[IO.File]::WriteAllText($destination, $content.Replace("`r`n", "`n"), [Text.UTF8Encoding]::new($false))
if ($ProvisionDatabase) {
    $admin = Join-Path $Workspace 'runtime/mysql-admin.cnf'
    if (!(Test-Path -LiteralPath $admin)) { throw 'The local database administrator options file is required.' }
    if ($connection[0] -notin @('127.0.0.1', 'localhost') -or $connection[2] -notmatch '^[a-zA-Z0-9_]+$') {
        throw 'Automatic database provisioning is restricted to the prepared local realm.'
    }
    $mysql = Join-Path $Workspace '.tools/mysql-8.4.13-winx64/bin/mysql.exe'
    $databaseUser = $connection[2]
    $sql = "CREATE DATABASE IF NOT EXISTS $DatabaseName CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci; " +
        "GRANT ALL PRIVILEGES ON $DatabaseName.* TO '$databaseUser'@'localhost';"
    & $mysql "--defaults-extra-file=$admin" "--host=$($connection[0])" "--port=$($connection[1])" "--execute=$sql"
    if ($LASTEXITCODE -ne 0) { throw 'Could not provision the local Playerbots database.' }
    Write-Host 'Playerbots database created and access granted to the existing local realm account.'
}
Write-Host 'Playerbots configured for WrathIdle-managed characters only. No services were started.'
