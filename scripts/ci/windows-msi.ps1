# Query Windows Installer directly: per-user products need not appear in the
# one uninstall registry location formerly checked by the local build helper.
function Get-Bit8MsiProductState {
    [CmdletBinding()]
    param([Parameter(Mandatory=$true)][Guid]$ProductCode)
    if (-not ('EightBitProgramsMsiState' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class EightBitProgramsMsiState {
    [DllImport("msi.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    public static extern int MsiQueryProductStateW(string productCode);
}
'@
    }
    return [EightBitProgramsMsiState]::MsiQueryProductStateW($ProductCode.ToString('B').ToUpperInvariant())
}

function Get-Bit8MsiInstallPlan {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$Package,
        [Parameter(Mandatory=$true)][Guid]$ProductCode,
        [Parameter(Mandatory=$true)][string]$Log,
        [switch]$Quiet,
        [scriptblock]$QueryProductState = { param($Code) Get-Bit8MsiProductState -ProductCode $Code }
    )
    $state = & $QueryProductState $ProductCode
    if ($state -notin @(-1,1,2,5)) {
        throw "Windows Installer returned an invalid or damaged product state ($state) for $ProductCode."
    }
    $ui = if ($Quiet) { '/qn' } else { '/passive' }
    $arguments = @('/i', ('"' + $Package + '"'), $ui, '/norestart', '/L*v', ('"' + $Log + '"'))
    $action = 'Install'
    if ($state -eq 5) {
        # Small updates keep ProductCode/version but change PackageCode. Recache
        # the new MSI and replace same-version local binaries and shortcuts.
        $arguments += @('REINSTALL=ALL', 'REINSTALLMODE=vamus')
        $action = 'Reinstall'
    }
    return [pscustomobject]@{
        Action = $action
        ProductState = [int]$state
        ProductCode = $ProductCode.ToString('B').ToUpperInvariant()
        LogPath = $Log
        Arguments = $arguments
    }
}

function Invoke-Bit8MsiInstallPlan {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)]$Plan,
        [scriptblock]$RunProcess = {
            param($ArgumentList)
            $process = Start-Process -FilePath (Join-Path $env:SystemRoot 'System32/msiexec.exe') -ArgumentList $ArgumentList -Wait -PassThru -WindowStyle Hidden
            return $process.ExitCode
        }
    )
    $exitCode = & $RunProcess $Plan.Arguments
    $log = $Plan.LogPath
    $action = $Plan.Action
    $retried = $false
    # The service may see a registered product that is hidden from this client's
    # registration view. A single 1638 retry uses the same verified MSI in the
    # documented in-place reinstall mode; it never uninstalls the existing app.
    if ($exitCode -eq 1638 -and $action -eq 'Install') {
        $retryArguments = @($Plan.Arguments)
        $log = [IO.Path]::ChangeExtension($log, 'reinstall.log')
        $logIndex = [Array]::IndexOf([string[]]$retryArguments, '/L*v')
        if ($logIndex -lt 0) { throw 'MSI install plan has no log argument.' }
        $retryArguments[$logIndex + 1] = '"' + $log + '"'
        $retryArguments += @('REINSTALL=ALL', 'REINSTALLMODE=vamus')
        Write-Host 'Windows Installer detected an existing copy; updating it in place...'
        $exitCode = & $RunProcess $retryArguments
        $retried = $true
        $action = 'Reinstall'
    }
    return [pscustomobject]@{
        ExitCode = [int]$exitCode
        Action = $action
        Retried = $retried
        LogPath = $log
    }
}

function Wait-Bit8MsiApplicationExit {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$Application,
        [Parameter(Mandatory=$true)][string]$Executable,
        [ValidateRange(0,600)][int]$TimeoutSeconds = 180,
        [scriptblock]$FindProcesses = {
            param($Name)
            Get-Process -Name $Name -ErrorAction SilentlyContinue
        },
        [scriptblock]$Pause = { Start-Sleep -Seconds 1 }
    )
    $expected = [IO.Path]::GetFullPath($Executable)
    $elapsed = [Diagnostics.Stopwatch]::StartNew()
    $reported = $false
    while ($true) {
        $running = @(& $FindProcesses $Application | Where-Object {
            # A separate portable copy does not lock the installed executable.
            # Treat an unreadable same-name process path conservatively.
            $path = $null
            try { $path = $_.Path } catch {}
            !$path -or [string]::Equals(
                [IO.Path]::GetFullPath($path), $expected,
                [StringComparison]::OrdinalIgnoreCase)
        })
        if (!$running.Count) { return }
        $identities = ($running | ForEach-Object { $_.Id }) -join ', '
        if (!$reported) {
            Write-Host "$Application is running (PID $identities). Save your work and exit the app; this update will continue automatically."
            $reported = $true
        }
        if ($elapsed.Elapsed.TotalSeconds -ge $TimeoutSeconds) {
            throw "$Application is still running (PID $identities). Close it normally, then rerun the install command."
        }
        & $Pause | Out-Null
    }
}
