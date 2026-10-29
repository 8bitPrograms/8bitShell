[CmdletBinding()]
param(
    [string]$QtRoot = '',
    [string]$PuttygenPath = $env:BIT8SHELL_PUTTYGEN,
    [string]$PythonExecutable = 'python',
    [ValidateRange(1,128)][int]$Jobs = 4,
    [switch]$Diagnose,
    [switch]$Portable,
    [string]$PortableDirectory = '',
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'Run this local helper on Windows x64.' }
$repo = Split-Path -Parent $PSScriptRoot
if (!$QtRoot) { $QtRoot = $env:BIT8SHELL_QT_ROOT }
if (!$QtRoot) {
    foreach ($candidate in @((Join-Path $repo 'build/tooling/qt-sdk'), (Join-Path $repo 'tooling/qt-sdk-x64'), 'C:/Qt/6.11.2/msvc2022_64')) {
        if (Test-Path -LiteralPath (Join-Path $candidate 'lib/cmake/Qt6/Qt6Config.cmake')) { $QtRoot = $candidate; break }
    }
}
if (!$QtRoot) { throw 'Set -QtRoot to a Qt MSVC x64 development SDK (see docs/BUILDING.md).' }
$resolvedQtRoot = [IO.Path]::GetFullPath($QtRoot)
if (!(Test-Path -LiteralPath (Join-Path $resolvedQtRoot 'lib/cmake/Qt6/Qt6Config.cmake'))) { throw 'Qt6Config.cmake was not found under -QtRoot.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio C++ Build Tools with the Windows SDK and CMake/Ninja.' }
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsRoot) { throw 'MSVC x64 tools were not found.' }
$savedEnvironment = @{}
foreach ($name in @('PATH','QT_PLUGIN_PATH','QT_QPA_PLATFORM','QT_QUICK_BACKEND','QT_QPA_FONTDIR','TEMP','TMP','TMPDIR','BIT8SHELL_QT_ROOT','VSINSTALLDIR')) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
Push-Location $repo
try {
    Import-Module (Join-Path $vsRoot 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vsRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
    $vsCMakeBin = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
    $vsNinjaBin = Join-Path $vsRoot 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja'
    $env:PATH = (Join-Path $resolvedQtRoot 'bin') + ";$vsCMakeBin;$vsNinjaBin;" + $env:PATH
    $env:BIT8SHELL_QT_ROOT = $resolvedQtRoot
    $env:VSINSTALLDIR = $vsRoot
    $env:QT_PLUGIN_PATH = Join-Path $resolvedQtRoot 'plugins'
    if ($Diagnose) {
        [pscustomobject]@{ QtRoot=$resolvedQtRoot; VisualStudio=$vsRoot; PythonExecutable=$PythonExecutable; BuildDirectory=(Join-Path $repo 'build/windows-release') } | Format-List | Out-Host
        & cmake --version
        if ($LASTEXITCODE) { throw 'CMake could not start.' }
        & ninja --version
        if ($LASTEXITCODE) { throw 'Ninja could not start.' }
        & $PythonExecutable --version
        if ($LASTEXITCODE) { throw 'A working Python 3.12+ executable is required.' }
        return
    }
    if (!$PuttygenPath) {
        $PuttygenPath = Join-Path $repo 'build/tooling/puttygen-cli.exe'
        if (!(Test-Path -LiteralPath $PuttygenPath -PathType Leaf)) {
            & $PythonExecutable scripts/ci/build-puttygen.py --output $PuttygenPath
            if ($LASTEXITCODE) { throw 'Command-line PuTTYgen build failed.' }
        }
    }
    $resolvedPuttygen = [IO.Path]::GetFullPath($PuttygenPath)
    & $PythonExecutable scripts/ci/build-puttygen.py --verify $resolvedPuttygen
    if ($LASTEXITCODE) { throw 'PuTTYgen provenance verification failed.' }
    $configure = @('--preset','windows-release','-DCMAKE_TRY_COMPILE_CONFIGURATION=Release', "-DBIT8SHELL_PUTTYGEN_EXECUTABLE=$resolvedPuttygen")
    foreach ($item in @(@('BITSHELL_VTERM_SOURCE','libvterm'), @('YAML-CPP','yaml-cpp'))) {
        $source = Join-Path $repo ("tooling/source-cache/" + $item[1])
        if (Test-Path -LiteralPath (Join-Path $source 'CMakeLists.txt')) { $configure += "-DFETCHCONTENT_SOURCE_DIR_$($item[0])=$source" }
        elseif ($item[1] -eq 'libvterm' -and (Test-Path -LiteralPath (Join-Path $source 'src/parser.c'))) { $configure += "-DFETCHCONTENT_SOURCE_DIR_$($item[0])=$source" }
    }
    & cmake @configure
    if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
    & cmake --build --preset windows-release --parallel $Jobs
    if ($LASTEXITCODE) { throw 'Build failed.' }
    # Native tests use the same locked SSH client inventory as the portable app.
    & $PythonExecutable -c "import sys; from pathlib import Path; sys.path.insert(0,'scripts/ci'); import build; build.openssh_windows(Path('build/windows-release'),'x64',build_layout=True)"
    if ($LASTEXITCODE) { throw 'Bundled OpenSSH setup failed.' }
    $testTemp = Join-Path $repo 'build/windows-release/test-temp'
    New-Item -ItemType Directory -Force -Path $testTemp | Out-Null
    $env:TEMP = $testTemp; $env:TMP = $testTemp; $env:TMPDIR = $testTemp
    $env:QT_QPA_PLATFORM = 'offscreen'; $env:QT_QUICK_BACKEND = 'software'
    $env:QT_QPA_FONTDIR = Join-Path $env:SystemRoot 'Fonts'
    & ctest --preset windows-release --no-tests=error
    if ($LASTEXITCODE) { throw 'Tests failed; a package was not produced.' }
    if ($Portable) {
        if (!$PortableDirectory) { $PortableDirectory = 'dist/8bitShell-windows-x64-' + (Get-Date -Format 'yyyyMMdd-HHmmss') }
        & $PythonExecutable scripts/ci/local-windows-portable.py --output $PortableDirectory
        if ($LASTEXITCODE) { throw 'Portable deployment or startup test failed.' }
    }
    if ($Package) {
        & (Join-Path $PSScriptRoot 'package-windows.ps1') -PythonExecutable $PythonExecutable
    }
    Write-Host '8bitShell local build and tests succeeded.'
} finally {
    Pop-Location
    foreach ($name in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
}
