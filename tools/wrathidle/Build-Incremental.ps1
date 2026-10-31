[CmdletBinding()]
param(
    [ValidateSet('Server', 'Client')][string]$Project = 'Server',
    [string]$Target = ''
)
$ErrorActionPreference = 'Stop'
$server = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$workspace = Split-Path $server
$cmake = Join-Path $workspace '.tools/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
$limits = Join-Path $PSScriptRoot 'LowResourceBuild.targets'
$buildDirectory = Join-Path $server 'build'
if ($Project -eq 'Client') { $buildDirectory = Join-Path $workspace 'Wrath-Idle-Client/build/x64' }
if (!$Target) { $Target = if ($Project -eq 'Client') { 'WowClient' } else { 'worldserver' } }
if ($Target -notmatch '^[A-Za-z0-9_-]+$') { throw 'Invalid build target.' }
if (!(Test-Path -LiteralPath $limits)) { throw 'Resource-limit targets file is missing.' }
$lock = $null
$build = $null
$parentProcess = Get-Process -Id $PID
$previousPriority = $parentProcess.PriorityClass
$previousAffinity = $parentProcess.ProcessorAffinity
try {
    $lock = [IO.File]::Open((Join-Path $workspace '.tools/build.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    if (Get-Process -Name MSBuild, cl, cmake, link -ErrorAction SilentlyContinue) {
        throw 'Another build process exists. Wait for it to finish.'
    }
    if ((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory -lt 8GB / 1KB) {
        throw 'Less than 8 GiB of physical memory is available. Build postponed.'
    }
    $logicalProcessors = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
    $processorLimit = [math]::Max(1, [math]::Min(63, [math]::Floor($logicalProcessors / 2)))
    $freeGiB = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1MB
    $memoryWorkers = [math]::Max(1, [math]::Floor(($freeGiB - 6) / 2))
    $workers = [int][math]::Min($processorLimit, $memoryWorkers)
    # Child processes inherit this affinity, including compiler and linker threads.
    $parentProcess.ProcessorAffinity = [IntPtr]([long][math]::Pow(2, $processorLimit) - 1)
    $parentProcess.PriorityClass = 'BelowNormal'
    Write-Host "Building $Project with $workers compiler workers on at most $processorLimit logical processors."

    $log = Join-Path $workspace ".tools/$($Project.ToLower())-bounded-build.log"
    $errorLog = Join-Path $workspace ".tools/$($Project.ToLower())-bounded-build.error.log"
    $arguments = @('--build', "`"$buildDirectory`"", '--config', 'Release', '--target', $Target,
        '--parallel', '1', '--', '/m:1', '/nr:false', '/p:BuildInParallel=false',
        "/p:CL_MPCount=$workers", '/p:UseMultiToolTask=false', "`"/p:ForceImportBeforeCppTargets=$limits`"")
    $stage = Join-Path $workspace '.tools/gameplay-stage'
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    if ($Project -eq 'Server') {
        $arguments += "`"/p:WrathIdleStagedExecutable=$stage/worldserver.exe`""
    } else {
        $arguments += "`"/p:WrathIdleStagedClient=$stage/WowClient64.exe`""
    }
    $build = Start-Process -FilePath $cmake -ArgumentList $arguments -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput $log -RedirectStandardError $errorLog
    $build.PriorityClass = 'BelowNormal'
    while (!$build.WaitForExit(1000)) {
        if ((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory -lt 4GB / 1KB) {
            $build.Kill($true)
            $build.WaitForExit()
            throw 'Build stopped because available physical memory fell below 4 GiB.'
        }
        if (@(Get-Process -Name cl -ErrorAction SilentlyContinue).Count -gt ($workers + 1)) {
            $build.Kill($true)
            $build.WaitForExit()
            throw 'Build stopped because the configured compiler worker limit was exceeded.'
        }
    }
    $build.WaitForExit()
    if ($build.ExitCode -ne 0) { throw "Build failed ($($build.ExitCode)). Read $log and $errorLog." }
    Write-Host "Build complete. Output saved to $log"
} finally {
    if ($build -and !$build.HasExited) { $build.Kill($true); $build.WaitForExit() }
    if ($lock) { $lock.Dispose() }
    $parentProcess.PriorityClass = $previousPriority
    $parentProcess.ProcessorAffinity = $previousAffinity
}
