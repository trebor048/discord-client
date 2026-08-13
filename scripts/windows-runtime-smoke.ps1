[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$DeploymentDirectory,

    [string]$ExecutablePath
)

$ErrorActionPreference = 'Stop'

function Fail-Smoke([string]$Message) {
    throw "Runtime smoke validation failed: $Message"
}

try {
    if (-not (Test-Path -LiteralPath $DeploymentDirectory -PathType Container)) {
        Fail-Smoke "Deployment directory does not exist: $DeploymentDirectory"
    }
    $deploymentRoot = (Resolve-Path -LiteralPath $DeploymentDirectory).Path

    if (-not $ExecutablePath) {
        $ExecutablePath = Join-Path $deploymentRoot 'acheron.exe'
    }
    elseif (-not [System.IO.Path]::IsPathRooted($ExecutablePath)) {
        $ExecutablePath = Join-Path $deploymentRoot $ExecutablePath
    }

    if (-not (Test-Path -LiteralPath $ExecutablePath -PathType Leaf)) {
        Fail-Smoke "Application executable does not exist: $ExecutablePath"
    }
    $executable = (Resolve-Path -LiteralPath $ExecutablePath).Path

    $requiredPaths = @(
        (Join-Path $deploymentRoot 'Qt6Multimedia.dll'),
        (Join-Path $deploymentRoot 'platforms\qwindows.dll'),
        (Join-Path $deploymentRoot 'platforms\qoffscreen.dll')
    )
    $missingPaths = $requiredPaths | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) }
    if ($missingPaths) {
        Fail-Smoke "Required deployed runtime dependency is missing: $($missingPaths -join '; ')"
    }

    $isolationRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("acheron-runtime-smoke-" + [guid]::NewGuid().ToString('N'))
    $stdoutPath = Join-Path $isolationRoot 'stdout.log'
    $stderrPath = Join-Path $isolationRoot 'stderr.log'
    New-Item -ItemType Directory -Path $isolationRoot -Force | Out-Null

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $executable
    $startInfo.Arguments = '--runtime-smoke'
    $startInfo.WorkingDirectory = $deploymentRoot
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.CreateNoWindow = $true
    $startInfo.EnvironmentVariables['QT_QPA_PLATFORM'] = 'offscreen'
    $startInfo.EnvironmentVariables['QT_QPA_PLATFORM_PLUGIN_PATH'] = (Join-Path $deploymentRoot 'platforms')
    $startInfo.EnvironmentVariables['QT_PLUGIN_PATH'] = ''
    $startInfo.EnvironmentVariables['APPDATA'] = (Join-Path $isolationRoot 'appdata')
    $startInfo.EnvironmentVariables['LOCALAPPDATA'] = (Join-Path $isolationRoot 'localappdata')
    $startInfo.EnvironmentVariables['XDG_CONFIG_HOME'] = (Join-Path $isolationRoot 'config')
    $startInfo.EnvironmentVariables['XDG_DATA_HOME'] = (Join-Path $isolationRoot 'data')

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        Fail-Smoke "Could not start deployed executable: $executable"
    }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(15000)) {
        try { $process.Kill() } catch { }
        $process.WaitForExit()
        Fail-Smoke "Timed out after 15 seconds: $executable --runtime-smoke. Check deployed Qt DLLs and platform plugins under $deploymentRoot."
    }

    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    [System.IO.File]::WriteAllText($stdoutPath, $stdout)
    [System.IO.File]::WriteAllText($stderrPath, $stderr)

    if ($process.ExitCode -ne 0 -or $stdout -notmatch '(?m)^ACHERON_RUNTIME_SMOKE_READY\s*$') {
        $diagnostics = @()
        if ($process.ExitCode -ne 0) { $diagnostics += "exit code $($process.ExitCode)" }
        if ($stdout -notmatch '(?m)^ACHERON_RUNTIME_SMOKE_READY\s*$') { $diagnostics += 'missing ACHERON_RUNTIME_SMOKE_READY marker' }
        $combinedOutput = ($stdout + [Environment]::NewLine + $stderr).Trim()
        if ($combinedOutput) { $diagnostics += "child output:`n$combinedOutput" }
        else { $diagnostics += 'the child emitted no output; this commonly indicates a Windows loader failure before main() (check Qt DLLs beside the executable and plugins/platforms).' }
        Fail-Smoke ("$($diagnostics -join [Environment]::NewLine)" + [Environment]::NewLine + "Captured logs: $stdoutPath ; $stderrPath")
    }

    Write-Host "Runtime smoke passed: $executable"
    Write-Host "Marker: ACHERON_RUNTIME_SMOKE_READY"
    exit 0
}
catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
finally {
    if ($process) { $process.Dispose() }
}
