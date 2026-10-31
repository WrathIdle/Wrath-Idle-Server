[CmdletBinding()]
param([switch]$CompileOnly)
$ErrorActionPreference = 'Stop'
$arguments = @{ Project = 'Server' }
if ($CompileOnly) { $arguments.Target = 'modules' }
& (Join-Path $PSScriptRoot 'Build-Incremental.ps1') @arguments
