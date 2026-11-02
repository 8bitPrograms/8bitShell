[CmdletBinding()]
param(
 [string]$PackageDirectory='',
 [switch]$Publish
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$version=(Get-Content -LiteralPath (Join-Path $repo 'PROJECT_VERSION') -Raw).Trim()
if(!$PackageDirectory){$PackageDirectory=Join-Path $repo "dist/release-$version-windows-x64"}
$PackageDirectory=(Resolve-Path -LiteralPath $PackageDirectory).Path
$info=Get-Content -LiteralPath (Join-Path $PackageDirectory 'build-info.json') -Raw | ConvertFrom-Json
$head=(& git -C $repo rev-parse HEAD).Trim()
$branch=(& git -C $repo branch --show-current).Trim()
if($LASTEXITCODE -or !$branch -or $info.commit -ne $head -or $info.version -ne $version){
 throw 'Checkout the exact source commit recorded in the package build-info.json before uploading.'
}
$assets=@("8bitShell-$version-windows-x64-setup.exe","8bitShell-$version-windows-x64.msi","8bitShell-$version-windows-x64.zip")
$hashes=@{}
foreach($line in Get-Content -LiteralPath (Join-Path $PackageDirectory 'SHA256SUMS.txt')){
 if($line -match '^([0-9a-f]{64})  (.+)$'){
  if($hashes.ContainsKey($Matches[2])){throw 'Duplicate checksum entry.'}
  $hashes[$Matches[2]]=$Matches[1]
 }
}
$uploads=@()
foreach($name in $assets){
 $path=Join-Path $PackageDirectory $name
 if(!(Test-Path -LiteralPath $path -PathType Leaf) -or !$hashes.ContainsKey($name)){throw "Missing package/checksum: $name"}
 if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hashes[$name]){throw "Package changed: $name"}
 $uploads+=$path
}
$notes=Join-Path $PackageDirectory 'RELEASE-NOTES.md'
if(!(Test-Path -LiteralPath $notes)){throw 'Release notes are missing.'}
$gh=Get-Command gh.exe -ErrorAction SilentlyContinue
$ghPath=if($gh){$gh.Source}else{Join-Path $env:ProgramFiles 'GitHub CLI/gh.exe'}
if(!(Test-Path -LiteralPath $ghPath)){throw 'GitHub CLI is required.'}
& $ghPath auth status
if($LASTEXITCODE){throw 'Run this helper from your authenticated desktop PowerShell session.'}
& git -C $repo push -u origin $branch
if($LASTEXITCODE){throw 'Source push failed; no release was created.'}
$tag="v$version-preview."+$head.Substring(0,7)
$arguments=@('release','create',$tag)+$uploads+@(
 (Join-Path $PackageDirectory 'SHA256SUMS.txt'),
 '--repo','8bitPrograms/8bitShell','--target',$head,
 '--title',"8bitShell $version - Windows preview",'--notes-file',$notes,'--prerelease')
if(!$Publish){$arguments+='--draft'}
& $ghPath @arguments
if($LASTEXITCODE){throw 'Release upload failed; existing releases were preserved.'}
