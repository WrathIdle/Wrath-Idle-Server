[CmdletBinding()]
param([string]$Server = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path)
$ErrorActionPreference = 'Stop'
$dependency = Get-Content (Join-Path $PSScriptRoot 'playerbots-dependency.json') -Raw | ConvertFrom-Json
$module = Join-Path $Server 'modules/mod-playerbots'
function Invoke-Git([string[]]$Arguments) {
    & git @Arguments
    if ($LASTEXITCODE -ne 0) { throw 'Playerbots dependency preparation failed.' }
}
Invoke-Git @('-C', $Server, 'merge-base', '--is-ancestor', $dependency.coreRevision, 'HEAD')
if (!(Test-Path -LiteralPath "$module/.git")) {
    if (Test-Path -LiteralPath $module) { throw 'Existing mod-playerbots is not a Git checkout; preserved.' }
    Invoke-Git @('clone', $dependency.moduleRepository, $module)
    Invoke-Git @('-C', $module, 'checkout', '--detach', $dependency.moduleRevision)
}
$actual = (& git -C $module rev-parse HEAD).Trim()
if ($actual -ne $dependency.moduleRevision) { throw 'Playerbots revision differs from the pinned dependency; preserved.' }
$patch = Join-Path $PSScriptRoot $dependency.patch
& git -C $module apply --reverse --check $patch 2>$null
if ($LASTEXITCODE -ne 0) {
    Invoke-Git @('-C', $module, 'apply', '--check', $patch)
    Invoke-Git @('-C', $module, 'apply', $patch)
}
Write-Host "Playerbots $actual and the managed-execution patch are ready."
