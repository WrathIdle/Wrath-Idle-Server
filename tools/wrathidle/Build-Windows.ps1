[CmdletBinding()]
param(
    [string]$Workspace = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path,
    [string]$VisualStudio = ( & "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -property installationPath ),
    [switch]$InstallDependencies,
    [ValidateRange(1, 1)]
    [int]$Jobs = 1
)
$ErrorActionPreference = 'Stop'
$server = Join-Path $Workspace 'Wrath-Idle-Server'
& (Join-Path $PSScriptRoot 'Setup-Playerbots.ps1') -Server $server
$cmake = Join-Path $Workspace '.tools/cmake-4.4.4-windows-x86_64/bin/cmake.exe'
$vcpkg = Join-Path $Workspace '.tools/vcpkg'
$mysql = Join-Path $Workspace '.tools/mysql-8.4.13-winx64'
function Invoke-Checked([string]$File, [string[]]$Arguments) {
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$File failed ($LASTEXITCODE)" }
}
if ($InstallDependencies) {
    Invoke-Checked "$vcpkg/vcpkg.exe" @('install','--triplet','x64-windows-static-md',
        'openssl','zlib',
        'boost-asio','boost-filesystem','boost-program-options','boost-iostreams',
        'boost-regex','boost-thread','boost-process','boost-dll','boost-heap','boost-stacktrace')
}
Import-Module "$VisualStudio/Common7/Tools/Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $VisualStudio -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$common = @("-DCMAKE_TOOLCHAIN_FILE=$vcpkg/scripts/buildsystems/vcpkg.cmake",
    '-DVCPKG_TARGET_TRIPLET=x64-windows-static-md','-DVCPKG_MANIFEST_MODE=OFF')
Invoke-Checked $cmake (@('-S',$server,'-B',"$server/build",'-G','Visual Studio 17 2022','-A','x64') + $common + @(
    '-DUSE_MYSQL_SOURCES=OFF',"-DMYSQL_ROOT_DIR=$mysql",'-DTOOLS_BUILD=all','-DSCRIPTS=static','-DMODULES=static','-DBUILD_TESTING=OFF'))
& (Join-Path $PSScriptRoot 'Build-Incremental.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Server build failed.' }
Copy-Item "$mysql/lib/libmysql.dll" "$server/build/bin/Release/libmysql.dll"
# libmysql.dll dynamically imports the OpenSSL DLLs from the same MySQL distribution.
foreach ($dependency in @('libssl-3-x64.dll', 'libcrypto-3-x64.dll')) {
    Copy-Item "$mysql/bin/$dependency" "$server/build/bin/Release/$dependency"
}
Invoke-Checked $cmake @('-S',"$server/tools/wrathidle/tests",'-B',"$server/build-policy",'-G','Visual Studio 17 2022','-A','x64')
Invoke-Checked $cmake @('--build',"$server/build-policy",'--config','Release','--parallel','1','--',
    '/m:1','/p:CL_MPCount=1','/p:BuildInParallel=false',
    "/p:ForceImportBeforeCppTargets=$PSScriptRoot/LowResourceBuild.targets")
Invoke-Checked (Join-Path (Split-Path $cmake) 'ctest.exe') @('--test-dir',"$server/build-policy",'-C','Release','--output-on-failure')
