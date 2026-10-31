[CmdletBinding()]
param([switch]$CompileOnly)
$ErrorActionPreference = 'Stop'
$server = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$workspace = Split-Path $server
$msbuild = (Join-Path (& "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -property installationPath) "MSBuild/Current/Bin/MSBuild.exe")
$limits = Join-Path $PSScriptRoot 'LowResourceBuild.targets'
$stage = Join-Path $workspace '.tools/gameplay-stage'
$buildSnapshot = @(Get-CimInstance Win32_Process)
$buildProcessIds = @{}
foreach ($entry in $buildSnapshot) { $buildProcessIds[$entry.ProcessId] = $true }
$activeBuilds = @($buildSnapshot | Where-Object {
    if ($_.Name -notin @('MSBuild.exe', 'cl.exe', 'cmake.exe', 'link.exe')) { return $false }
    if ($_.Name -eq 'MSBuild.exe' -and $_.CommandLine -match '/nodemode:' -and
        !$buildProcessIds.ContainsKey($_.ParentProcessId)) { return $false }
    return $true
})
if ($activeBuilds.Count) {
    throw 'Another build is running; no additional build was started.'
}
if ((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory -lt 6GB / 1KB) {
    throw 'Less than 6 GB is available; build postponed.'
}
$process = Get-Process -Id $PID
$priority = $process.PriorityClass
$common = @('/p:Configuration=Release', '/p:Platform=x64', '/p:BuildProjectReferences=false', '/m:1',
    '/p:BuildInParallel=false', '/p:CL_MPCount=1', '/p:UseMultiToolTask=false',
    "/p:ForceImportBeforeCppTargets=$limits", '/verbosity:minimal')
try {
    $process.PriorityClass = 'BelowNormal'
    foreach ($name in @('IdleQuest.cpp','IdleUpkeep.cpp','WrathIdle.cpp','PlayerbotsExecutor.cpp')) {
        $source = Join-Path $server "modules/mod-wrathidle/src/$name"
        & $msbuild "$server/build/modules/modules.vcxproj" @common /t:ClCompile `
            "/p:SelectedFiles=$source" /p:SelectedFilesBuildPCH=false
        if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $name" }
    }
    if ($CompileOnly) { return }
    & $msbuild "$server/build/modules/modules.vcxproj" @common /t:WrathIdleArchive
    if ($LASTEXITCODE -ne 0) { throw 'Module library build failed.' }
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    & $msbuild "$server/build/src/server/apps/worldserver.vcxproj" @common /t:WrathIdleLink `
        "/p:WrathIdleStagedExecutable=$stage/worldserver.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Server link failed.' }
    if (!(Test-Path -LiteralPath "$stage/worldserver.exe")) { throw 'Link did not produce a staged executable.' }
    Write-Host "Staged server: $stage/worldserver.exe"
} finally {
    $process.PriorityClass = $priority
}
