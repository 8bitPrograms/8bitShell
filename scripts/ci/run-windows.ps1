[CmdletBinding()]
param([ValidateSet('x64','arm64','x86')][string]$Architecture='x64',[switch]$SdkOnly,[switch]$BuildOnly)
$ErrorActionPreference='Stop'
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
 & python scripts/ci/build-qt.py --prefix $sdk --jobs 2
 if($LASTEXITCODE){throw 'Qt SDK build failed'}
 if($SdkOnly){return}
 $args=@('scripts/ci/build.py','--qt-root',$sdk,'--jobs','2')
 if($BuildOnly){$args+='--build-only'}
 & python @args
 if($LASTEXITCODE){throw 'Application build/test/package failed'}
}finally{Pop-Location}
