[CmdletBinding()]
param(
    [string]$ClientDirectory = $env:WRATHIDLE_ASSET_CLIENT,
    [switch]$StageOnly,
    [switch]$ConfigureLocalRealm
)
$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$storm = Join-Path $workspace '.tools/stormlib-9.40/x64/StormLib.dll'
if (!(Test-Path -LiteralPath $storm)) {
    throw 'StormLib 9.40 is required under .tools/stormlib-9.40/x64. See the module README.'
}
$arguments = @((Join-Path $PSScriptRoot 'native_ui.py'), '--client', $ClientDirectory,
    '--storm', $storm, '--output', (Join-Path $workspace '.tools/native-ui'))
if (!$StageOnly) { $arguments += '--install' }
& python @arguments
if ($LASTEXITCODE -ne 0) { throw 'Native UI preparation or installation failed.' }
if ($ConfigureLocalRealm -and !$StageOnly) {
    $realmFiles = @(Get-ChildItem -LiteralPath (Join-Path $ClientDirectory 'Data') -Filter realmlist.wtf -Recurse)
    if ($realmFiles.Count -ne 1) { throw 'Expected one locale realmlist.wtf.' }
    $realmFile = $realmFiles[0].FullName
    if (!(Test-Path -LiteralPath "$realmFile.before-wrathidle")) {
        Copy-Item -LiteralPath $realmFile -Destination "$realmFile.before-wrathidle"
    }
    Set-Content -LiteralPath $realmFile -Value 'set realmlist 127.0.0.1' -Encoding ascii
}
