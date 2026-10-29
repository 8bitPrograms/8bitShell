[CmdletBinding()]
param([ValidateSet('x64','arm64','x86')][string]$Architecture='x64',[switch]$SdkOnly,[switch]$BuildOnly)
$ErrorActionPreference='Stop'
function Get-Bit8Python {
    if($env:BIT8_PYTHON){
        if(Test-Path -LiteralPath $env:BIT8_PYTHON){ return (Resolve-Path -LiteralPath $env:BIT8_PYTHON).Path }
        throw "BIT8_PYTHON does not exist: $($env:BIT8_PYTHON)"
    }
    $commands=@(Get-Command python.exe,python3.exe,python -All -ErrorAction SilentlyContinue)
    foreach($command in $commands){
        if($command.Source -and $command.Source -notmatch '\\WindowsApps\\'){ return $command.Source }
    }
    throw 'Python 3 was not found on PATH. Set BIT8_PYTHON to python.exe.'
}
$pythonExe=Get-Bit8Python
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$vswhere=Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -property installationPath
if(!$vs){throw 'Visual Studio C++ Build Tools are required on the build runner'}
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
$hostArch=if($env:PROCESSOR_ARCHITECTURE -eq 'ARM64'){'arm64'}else{'x64'}
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=$Architecture -host_arch=$hostArch"
$cmake=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
$ninja=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja'
$env:PATH="$cmake;$ninja;$env:PATH"
$env:RELEASE_ARCH=$Architecture
$env:VSINSTALLDIR=$vs
$sdk=Join-Path $repo "tooling/qt-sdk-$Architecture"
Push-Location $repo
try{
 & $pythonExe scripts/ci/build-qt.py --prefix $sdk --jobs 2
 if($LASTEXITCODE){throw 'Qt SDK build failed'}
 if($SdkOnly){return}
 $args=@('scripts/ci/build.py','--qt-root',$sdk,'--jobs','2')
 if($BuildOnly){$args+='--build-only'}
 & $pythonExe @args
 if($LASTEXITCODE){throw 'Application build/test/package failed'}
}finally{Pop-Location}
