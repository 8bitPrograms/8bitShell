#Requires -Version 5.1
<#
.SYNOPSIS
  Replace a portable 8bitShell folder with a newer portable folder.

.DESCRIPTION
  Copies bin, plugins, qml, share, translations and the launchers.
  Leaves Destination\profile untouched. Does not delete extra old files.
  Refuses to run while 8bitShell.exe from Destination is still open.

.EXAMPLE
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Upgrade-Portable.ps1 `
    -Source .\dist\8bitShell-windows-x64-20261029-140441 `
    -Destination .\dist\8bitShell-windows-x64-20261029-103423
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Destination
)
$ErrorActionPreference = 'Stop'

function Test-PortableRoot([string]$Path) {
    $exe = Join-Path $Path 'bin\8bitShell.exe'
    $launcher = Join-Path $Path 'Start-Portable.cmd'
    if (-not (Test-Path -LiteralPath $exe) -or -not (Test-Path -LiteralPath $launcher)) {
        throw "Not a portable 8bitShell folder: $Path"
    }
}

$Source = [IO.Path]::GetFullPath($Source).TrimEnd('\')
$Destination = [IO.Path]::GetFullPath($Destination).TrimEnd('\')
if ($Source -ieq $Destination) { throw 'Source and Destination are the same folder.' }
Test-PortableRoot $Source
Test-PortableRoot $Destination

$targetExe = Join-Path $Destination 'bin\8bitShell.exe'
$running = @(Get-Process -Name '8bitShell' -ErrorAction SilentlyContinue | Where-Object {
    try { [IO.Path]::GetFullPath($_.Path) -ieq $targetExe } catch { $false }
})
if ($running.Count) { throw 'Close 8bitShell from the destination folder, then run this again.' }

& robocopy.exe $Source $Destination /E /XD profile /NFL /NDL /NJH /NJS /NP | Out-Host
$code = $LASTEXITCODE
if ($code -ge 8) { throw "Copy failed (robocopy exit $code)." }
Write-Host "Portable files updated. Profile was left in place: $Destination\profile"
exit 0
