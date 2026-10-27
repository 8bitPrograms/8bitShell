[CmdletBinding()]
param(
    [string]$BuildDir = 'build/windows-release',
    [string]$OutputDir = 'dist/windows',
    [string]$PythonExecutable = 'python',
    [switch]$MsiOnly
)
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'Windows packaging requires Windows x64.' }
$repo = Split-Path -Parent $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $repo 'PROJECT_VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+(-(alpha|beta|rc)\.\d+)?$') { throw 'Invalid PROJECT_VERSION.' }
Push-Location $repo
try {
    $cache = Join-Path $BuildDir 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cache -PathType Leaf)) { throw 'Configure and build 8bitShell first.' }
    if (-not (Select-String -LiteralPath $cache -Pattern '^BIT8SHELL_BUNDLE_RUNTIME:BOOL=ON$' -Quiet)) {
        throw 'Windows packages require BIT8SHELL_BUNDLE_RUNTIME=ON; reconfigure and rebuild first.'
    }
    Get-Command $PythonExecutable -ErrorAction Stop | Out-Null
    Get-Command cpack -ErrorAction Stop | Out-Null
    if (-not $MsiOnly) {
        $nsis = Get-Command makensis.exe -ErrorAction SilentlyContinue
        $compiler = if ($nsis) { $nsis.Source } else { Join-Path ${env:ProgramFiles(x86)} 'NSIS\makensis.exe' }
        if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) { throw 'NSIS 3 is required for the EXE bootstrap; use -MsiOnly for MSI and ZIP packages.' }
    }
    $configuredPuttygen = Select-String -LiteralPath $cache -Pattern '^BIT8SHELL_PUTTYGEN_EXECUTABLE:FILEPATH=(.+)$' |
        Select-Object -First 1
    if (-not $configuredPuttygen -or -not (Test-Path -LiteralPath $configuredPuttygen.Matches[0].Groups[1].Value -PathType Leaf)) {
        throw 'Windows packages require the verified command-line PuTTYgen converter; rebuild first.'
    }
    $configuredPuttygenPath = $configuredPuttygen.Matches[0].Groups[1].Value
    & $PythonExecutable (Join-Path $repo 'scripts/ci/build-puttygen.py') --verify $configuredPuttygenPath
    if ($LASTEXITCODE) { throw 'The configured PuTTYgen provenance check failed.' }
    $configuredPuttygenHash = (Get-FileHash -LiteralPath $configuredPuttygenPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $builtPuttygen = Join-Path $BuildDir 'puttygen.exe'
    if (-not (Test-Path -LiteralPath $builtPuttygen -PathType Leaf)) {
        throw 'The verified PuTTYgen converter was not copied beside bitshell.exe; rebuild before packaging.'
    }
    $builtPuttygenHash = (Get-FileHash -LiteralPath $builtPuttygen -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($builtPuttygenHash -ne $configuredPuttygenHash) {
        throw 'The staged PuTTYgen converter no longer matches the verified build.'
    }
    $out = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDir)
    if (Test-Path -LiteralPath $out) { throw 'Use a new output directory; existing packages are preserved.' }
    New-Item -ItemType Directory -Path $out | Out-Null
    $config = Join-Path $BuildDir 'CPackConfig.cmake'
    foreach ($generator in @('ZIP', 'WIX')) {
        & cpack --config $config -G $generator -B $out
        if ($LASTEXITCODE) { throw "$generator packaging failed." }
    }
    if (-not $MsiOnly) {
        $msi = Join-Path $out "bitshell-$version-windows-x64.msi"
        $exe = Join-Path $out "bitshell-$version-windows-x64-setup.exe"
        $fileVersion = ($version -split '-', 2)[0] + '.0'
        $icon = Join-Path $repo 'assets/bitshell.ico'
        & $compiler "/DINPUT_MSI=$msi" "/DOUTPUT_EXE=$exe" "/DAPP_VERSION=$version" "/DFILE_VERSION=$fileVersion" "/DAPP_ICON=$icon" (Join-Path $repo 'packaging/windows-bootstrap.nsi')
        if ($LASTEXITCODE) { throw 'EXE bootstrap packaging failed.' }
    }
    $hashes = Get-ChildItem -LiteralPath $out -File | Where-Object Extension -In '.msi', '.exe', '.zip' |
        Get-FileHash -Algorithm SHA256
    $hashes | ForEach-Object { '{0}  {1}' -f $_.Hash.ToLowerInvariant(), (Split-Path -Leaf $_.Path) } |
        Set-Content -LiteralPath (Join-Path $out 'SHA256SUMS.txt') -Encoding ascii
    $hashes
    Write-Host 'These packages are unsigned evaluation builds. Do not bypass Windows security warnings.'
} finally { Pop-Location }
