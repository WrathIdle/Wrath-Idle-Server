# Compatibility entrypoint. WrathIdle now installs native FrameXML from an MPQ.
[CmdletBinding()]
param(
    [string]$ClientDirectory = $env:WRATHIDLE_ASSET_CLIENT,
    [switch]$ConfigureLocalRealm
)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'Install-NativeUI.ps1') -ClientDirectory $ClientDirectory `
    -ConfigureLocalRealm:$ConfigureLocalRealm
