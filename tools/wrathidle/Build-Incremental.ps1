[CmdletBinding()]
param([string]$Target = 'worldserver')
$ErrorActionPreference = 'Stop'
$server = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$workspace = Split-Path $server
$cmake = Join-Path $workspace '.tools/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
$limits = Join-Path $PSScriptRoot 'LowResourceBuild.targets'
if (Get-Process -Name MSBuild,cl,cmake -ErrorAction SilentlyContinue) {
    throw 'Another build is running. Wait for it to finish before starting this build.'
}
$free = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory
if ($free -lt 6GB / 1KB) { throw 'Less than 6 GB of memory is available. Build postponed.' }
$process = Get-Process -Id $PID
$priority = $process.PriorityClass
try {
    $process.PriorityClass = 'BelowNormal'
    & $cmake --build (Join-Path $server 'build') --config Release --target $Target --parallel 1 -- `
        /m:1 /p:BuildInParallel=false /p:CL_MPCount=1 /p:UseMultiToolTask=false `
        "/p:ForceImportBeforeCppTargets=$limits"
    if ($LASTEXITCODE -ne 0) { throw "Incremental build failed ($LASTEXITCODE)." }
} finally {
    $process.PriorityClass = $priority
}
