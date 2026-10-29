#Requires -Version 5.1
[CmdletBinding()]
param(
 [ValidateSet('Both','8bitShell','8bitPad')][string]$Application='Both',
 [string]$OutputDirectory=''
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
if(!$OutputDirectory){$OutputDirectory=Join-Path $repo ('build/install-verification/run-'+[guid]::NewGuid().ToString('N'))}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$names=if($Application -eq 'Both'){@('8bitShell','8bitPad')}else{@($Application)}
$variables=@('PATH','QT_PLUGIN_PATH','QML2_IMPORT_PATH','QML_IMPORT_PATH','QT_QPA_PLATFORM','QT_QPA_FONTDIR','QT_QUICK_BACKEND','BIT8PAD_PROFILE','BIT8SHELL_PROFILE','TEMP','TMP')
$saved=@{}
foreach($key in $variables){$saved[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
$results=@()
try {
 foreach($key in @('QT_PLUGIN_PATH','QML2_IMPORT_PATH','QML_IMPORT_PATH')){[Environment]::SetEnvironmentVariable($key,$null,'Process')}
 $env:PATH=Join-Path $env:SystemRoot 'System32'
 $env:QT_QPA_PLATFORM='offscreen';$env:QT_QUICK_BACKEND='software'
 $env:QT_QPA_FONTDIR=Join-Path $env:SystemRoot 'Fonts'
 foreach($name in $names){
  $bin=Join-Path $env:LOCALAPPDATA ('Programs/'+$name+'/bin')
  $exe=Join-Path $bin ($name+'.exe')
  foreach($file in @(($name+'.exe'),'Apply-Update.ps1')){
   if(!(Test-Path -LiteralPath (Join-Path $bin $file))){throw "Installed file missing: $name/$file"}
  }
  $segments=@(([Environment]::GetEnvironmentVariable('PATH','User') -split ';') | ForEach-Object {[Environment]::ExpandEnvironmentVariables($_).Trim().TrimEnd('\')})
  if($segments -inotcontains $bin.TrimEnd('\')){throw "$name command directory is missing from your user PATH"}
  $out=Join-Path $OutputDirectory $name
  New-Item -ItemType Directory -Path $out -Force | Out-Null
  $temp=Join-Path $out 'temp';New-Item -ItemType Directory -Path $temp -Force | Out-Null
  $env:TEMP=$temp;$env:TMP=$temp
  if($name -eq '8bitPad'){
   if(!(Test-Path -LiteralPath (Join-Path $bin '8p.exe'))){throw 'Installed 8p.exe is missing'}
   if(!(Test-Path -LiteralPath (Join-Path $bin '8bitPadFileIcons.dll'))){throw 'Installed 8bitPadFileIcons.dll is missing'}
   $env:BIT8PAD_PROFILE=Join-Path $out 'profile'
   $arguments=@('--smoke-test',('"'+$out+'"'))
  }else{
   $env:BIT8SHELL_PROFILE=Join-Path $out 'profile'
   $arguments=@('--screenshot',('"'+(Join-Path $out 'shell.png')+'"'),'--window-size','1280x800')
  }
  $process=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $out -WindowStyle Hidden -PassThru
  if(!$process.WaitForExit(180000)){
   # Only the isolated test process started above is stopped on timeout.
   Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
   throw "$name verification timed out; output: $out"
  }
  if($process.ExitCode -ne 0){
   $status=[BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$process.ExitCode),0).ToString('X8')
   throw "$name verification failed (exit $($process.ExitCode), 0x$status); output: $out"
  }
  if($name -eq '8bitPad'){
   $report=Get-Content -LiteralPath (Join-Path $out 'smoke-results.json') -Raw | ConvertFrom-Json
   $failed=@($report.checks | Where-Object {!$_.passed})
   $handoff=@($report.checks | Where-Object {$_.name -eq 'command-line-relative-folder-and-file' -and $_.passed})
   if(!$report.success -or $failed.Count -or !$handoff.Count){throw "Pad's live 8p handoff was not verified. Run this script from normal Windows PowerShell. Report: $out/smoke-results.json"}
  }elseif(!(Test-Path -LiteralPath (Join-Path $out 'shell.png'))){throw 'Shell did not save its startup screenshot'}
  $results+=[pscustomobject]@{Application=$name;InstalledRuntime=$true;UserPath=$true;LivePadCommandHandoff=($name -eq '8bitPad');Reports=$out}
 }
}finally{
 foreach($key in $variables){[Environment]::SetEnvironmentVariable($key,$saved[$key],'Process')}
}
$results | ConvertTo-Json -Depth 4
Write-Host 'Installed runtime verification passed. Reopen your terminal to pick up PATH changes. Test Grok interactively in the new Shell build.'
