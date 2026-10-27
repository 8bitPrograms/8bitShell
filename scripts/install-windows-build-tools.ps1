[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'Run this setup on Windows.' }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
try {
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Open PowerShell as Administrator and run this script. Microsoft Build Tools initial installation requires administrator rights.'
    }
} finally { $identity.Dispose() }

$repoRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$setupRoot = Join-Path $repoRoot 'build\tooling\windows-setup'
New-Item -ItemType Directory -Path $setupRoot -Force | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$components = @(
    'Microsoft.VisualStudio.Workload.VCTools',
    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
    'Microsoft.VisualStudio.Component.Windows11SDK.26100',
    'Microsoft.VisualStudio.Component.VC.CMake.Project',
    'Microsoft.VisualStudio.Component.VC.ASAN'
)

function Find-BuildTools {
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) { return $null }
    $query = @('-latest', '-products', '*', '-version', '[17.0,18.0)', '-requires')
    $query += $components | Where-Object { $_ -ne 'Microsoft.VisualStudio.Workload.VCTools' }
    $query += @('-property', 'installationPath')
    $result = & $vswhere @query
    if ($LASTEXITCODE -ne 0) { throw 'Visual Studio installation discovery failed.' }
    return $result
}

$vsRoot = Find-BuildTools
if (-not $vsRoot) {
    $bootstrapper = Join-Path $setupRoot 'vs_BuildTools.exe'
    Write-Output 'Downloading the official Microsoft Visual Studio 2022 Build Tools installer.'
    Invoke-WebRequest -Uri 'https://aka.ms/vs/17/release/vs_BuildTools.exe' -OutFile $bootstrapper -TimeoutSec 120 -UseBasicParsing
    $signature = Get-AuthenticodeSignature -LiteralPath $bootstrapper
    if ($signature.Status -ne 'Valid' -or -not $signature.SignerCertificate -or
        $signature.SignerCertificate.Subject -notmatch '(^|,\s*)O=Microsoft Corporation(,|$)') {
        throw 'The downloaded installer does not have a valid Microsoft Corporation signature; it will not be executed.'
    }
    $arguments = @('--quiet', '--wait', '--norestart', '--nocache', '--locale', 'en-US')
    foreach ($component in $components) { $arguments += @('--add', $component) }
    Write-Output 'Installing MSVC v143, Windows SDK, CMake/Ninja, and AddressSanitizer.'
    $installer = Start-Process -FilePath $bootstrapper -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    if ($installer.ExitCode -notin @(0, 3010)) {
        throw "Microsoft Build Tools installation failed (exit $($installer.ExitCode)). See the dd_*setup* logs in your TEMP directory."
    }
    if ($installer.ExitCode -eq 3010) {
        Write-Warning 'Microsoft reports that a restart is required. This script will not restart Windows.'
    }
    $vsRoot = Find-BuildTools
    if (-not $vsRoot) { throw 'The requested Visual Studio 2022 C++ components were not found after installation.' }
}

$toolchain = @{
    'CMake' = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    'Ninja' = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
}
foreach ($name in $toolchain.Keys) {
    if (-not (Test-Path -LiteralPath $toolchain[$name] -PathType Leaf)) { throw "$name is missing from the C++ CMake tools installation." }
    & $toolchain[$name] --version
    if ($LASTEXITCODE -ne 0) { throw "$name could not run." }
}
$versionFile = Join-Path $vsRoot 'VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt'
if (-not (Test-Path -LiteralPath $versionFile -PathType Leaf)) { throw 'The MSVC default toolset version file is missing.' }
$toolsetVersion = (Get-Content -LiteralPath $versionFile -Raw).Trim()
$compiler = Join-Path $vsRoot "VC\Tools\MSVC\$toolsetVersion\bin\Hostx64\x64\cl.exe"
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) { throw 'The MSVC x64 compiler is missing.' }
Write-Output "Build tools ready: $vsRoot (MSVC $toolsetVersion)."
Write-Output 'Return to a normal, non-administrator PowerShell for the 8bitShell build and runtime tests.'
