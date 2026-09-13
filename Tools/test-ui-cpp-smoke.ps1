[CmdletBinding(DefaultParameterSetName = 'Run')]
param(
    [Parameter(ParameterSetName = 'Run')]
    [string]$Executable,

    [Parameter(ParameterSetName = 'Run')]
    [string]$ReportDirectory,

    [Parameter(ParameterSetName = 'Run')]
    [ValidateRange(1, 45)]
    [int]$TimeoutSeconds = 45,

    [Parameter(Mandatory = $true, ParameterSetName = 'Validate')]
    [string]$ReportPath,

    [Parameter(Mandatory = $true, ParameterSetName = 'Validate')]
    [string]$RunId,

    [Parameter(Mandatory = $true, ParameterSetName = 'Validate')]
    [DateTime]$NotBeforeUtc
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-ExpectedSmokeSteps {
    $steps = [Collections.Generic.List[string]]::new()
    $steps.Add('activate')
    for ($pass = 0; $pass -lt 3; ++$pass) {
        foreach ($step in @('theme-light', 'theme-dark', 'theme-default', 'theme-light', 'theme-dark', 'minimize', 'restore', 'reconnect')) {
            $steps.Add($step)
        }
    }
    $steps.Add('close')
    return @($steps)
}

function Assert-SmokeReport {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$ExpectedRunId,

        [Parameter(Mandatory = $true)]
        [DateTime]$FreshAfterUtc
    )

    $resolvedPath = [IO.Path]::GetFullPath($Path)
    if (-not (Test-Path -LiteralPath $resolvedPath -PathType Leaf)) {
        throw "Native UI smoke report is missing: $resolvedPath"
    }
    $reportFile = Get-Item -LiteralPath $resolvedPath
    if ($reportFile.LastWriteTimeUtc -lt $FreshAfterUtc) {
        throw "Native UI smoke report is stale: $resolvedPath"
    }
    try {
        $report = Get-Content -LiteralPath $resolvedPath -Raw | ConvertFrom-Json
    }
    catch {
        throw "Native UI smoke report is malformed JSON: $resolvedPath"
    }

    $expectedProperties = @('runId', 'complete', 'steps', 'failure')
    $actualProperties = @($report.PSObject.Properties.Name)
    if ($actualProperties.Count -ne $expectedProperties.Count -or (Compare-Object $expectedProperties $actualProperties)) {
        throw "Native UI smoke report has an unexpected schema: $resolvedPath"
    }
    if ($report.runId -isnot [string] -or $report.runId -cne $ExpectedRunId) {
        throw "Native UI smoke report run ID does not match: $resolvedPath"
    }
    if ($report.complete -isnot [bool] -or -not $report.complete) {
        throw "Native UI smoke report is incomplete: $resolvedPath"
    }
    if ($report.failure -isnot [string] -or $report.failure.Length -ne 0) {
        throw "Native UI smoke report reported a failure: $resolvedPath"
    }

    $actualSteps = @($report.steps)
    $expectedSteps = Get-ExpectedSmokeSteps
    if ($actualSteps.Count -ne $expectedSteps.Count) {
        throw "Native UI smoke report has missing or duplicate steps: $resolvedPath"
    }
    for ($index = 0; $index -lt $expectedSteps.Count; ++$index) {
        if ($actualSteps[$index] -isnot [string] -or $actualSteps[$index] -cne $expectedSteps[$index]) {
            throw "Native UI smoke report has unexpected, missing, or duplicate steps: $resolvedPath"
        }
    }
}

if ($PSCmdlet.ParameterSetName -eq 'Validate') {
    Assert-SmokeReport -Path $ReportPath -ExpectedRunId $RunId -FreshAfterUtc $NotBeforeUtc.ToUniversalTime()
    Write-Host 'Native UI smoke report passed validation.'
    return
}

$repoRoot = Split-Path $PSScriptRoot -Parent
if (-not $Executable) {
    $Executable = Join-Path $repoRoot 'build\Debug\ui\PresentMonUI.exe'
}
if (-not $ReportDirectory) {
    $ReportDirectory = Join-Path $repoRoot 'build\Debug\ui-smoke'
}
$resolvedExecutable = [IO.Path]::GetFullPath($Executable)
$resolvedReportDirectory = [IO.Path]::GetFullPath($ReportDirectory)
if (-not (Test-Path -LiteralPath $resolvedExecutable -PathType Leaf)) {
    throw "Native UI executable is missing: $resolvedExecutable"
}
$null = [IO.Directory]::CreateDirectory($resolvedReportDirectory)

$runId = [Guid]::NewGuid().ToString('N')
$reportFile = Join-Path $resolvedReportDirectory "native-ui-smoke-$runId.json"
if (Test-Path -LiteralPath $reportFile) {
    throw "Refusing to reuse an existing smoke report path: $reportFile"
}
$startedUtc = [DateTime]::UtcNow
$arguments = @('--native-smoke-test', '--smoke-report', ('"{0}"' -f $reportFile), '--smoke-run-id', $runId)
$process = Start-Process -FilePath $resolvedExecutable -ArgumentList $arguments -WorkingDirectory (Split-Path $resolvedExecutable -Parent) -WindowStyle Hidden -PassThru
if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit(5000) | Out-Null
    }
    throw "Native UI smoke test timed out after $TimeoutSeconds seconds."
}
if ($process.ExitCode -ne 0) {
    throw "Native UI smoke test exited with code $($process.ExitCode)."
}

Assert-SmokeReport -Path $reportFile -ExpectedRunId $runId -FreshAfterUtc $startedUtc
Write-Host "Native UI smoke test passed: $reportFile"
