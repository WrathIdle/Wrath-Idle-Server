[CmdletBinding()]
param([string]$Workspace = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path)
$ErrorActionPreference = 'Stop'
$archive = Join-Path $Workspace '.tools/ac-data-v20.zip'
$destination = Join-Path $Workspace 'runtime/server/ac-data-v20'
if (Test-Path -LiteralPath $destination) { throw "Destination already exists: $destination" }
New-Item -ItemType Directory (Split-Path $archive) -Force | Out-Null
if (!(Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest 'https://github.com/wowgaming/client-data/releases/download/v20.0/data.zip' -OutFile $archive
}
Expand-Archive -LiteralPath $archive -DestinationPath $destination
foreach ($folder in @('dbc','maps','vmaps','mmaps')) {
    if (!(Test-Path -LiteralPath (Join-Path $destination $folder))) { throw "Data package missing $folder" }
}
Write-Host "Set DataDir to $destination in worldserver.conf. Keep these files outside Git."
