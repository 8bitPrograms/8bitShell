[CmdletBinding()]
param(
    [string]$QtRoot = 'C:\Qt\6.11.2\msvc2022_64',
    [string]$PuttygenPath = $env:BIT8SHELL_PUTTYGEN,
    [int]$Jobs = 4,
    [switch]$Package
)
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'Run this script on Windows x64.' }
$resolvedQtRoot = [System.IO.Path]::GetFullPath($QtRoot)
if (-not (Test-Path (Join-Path $resolvedQtRoot 'bin\qmake.exe'))) { throw 'Set -QtRoot to the Qt MSVC x64 installation.' }
$repo = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'Install Visual Studio 2022 Build Tools with C++ and CMake first.' }
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'MSVC x64 tools were not found.' }
Import-Module (Join-Path $vsRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$vsCMakeBin = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$vsNinjaBin = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
if ((Test-Path -LiteralPath $vsCMakeBin) -and (Test-Path -LiteralPath $vsNinjaBin)) {
    $env:PATH = "$vsCMakeBin;$vsNinjaBin;$env:PATH"
}
$env:BIT8SHELL_QT_ROOT = $resolvedQtRoot
$env:PATH = (Join-Path $resolvedQtRoot 'bin') + ';' + $env:PATH
$env:QT_PLUGIN_PATH = Join-Path $resolvedQtRoot 'plugins'
Push-Location $repo
try {
    if (-not $PuttygenPath) {
        $PuttygenPath = Join-Path $repo 'build\tooling\puttygen-cli.exe'
        if (-not (Test-Path -LiteralPath $PuttygenPath -PathType Leaf)) {
            & python 'scripts/ci/build-puttygen.py' --output $PuttygenPath
            if ($LASTEXITCODE) { throw 'Command-line PuTTYgen build failed.' }
        }
    }
    $configureArguments = @('--preset', 'windows-release')
    $resolvedPuttygen = [System.IO.Path]::GetFullPath($PuttygenPath)
    if (-not (Test-Path -LiteralPath $resolvedPuttygen -PathType Leaf)) {
        throw "PuTTYgen was not found at $resolvedPuttygen."
    }
    & python 'scripts/ci/build-puttygen.py' --verify $resolvedPuttygen
    if ($LASTEXITCODE) { throw 'PuTTYgen provenance verification failed.' }
    $configureArguments += "-DBIT8SHELL_PUTTYGEN_EXECUTABLE=$resolvedPuttygen"
    & cmake @configureArguments
    if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
    & cmake --build --preset windows-release --parallel $Jobs
    if ($LASTEXITCODE) { throw 'Build failed.' }
    & ctest --preset windows-release
    if ($LASTEXITCODE) { throw 'Tests failed; a package was not produced.' }
    if ($Package) {
        & (Join-Path $PSScriptRoot 'package-windows.ps1')
    }
} finally { Pop-Location }
