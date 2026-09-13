[CmdletBinding()]
param(
    [string]$HostExecutable,

    [string]$UiExecutable,

    [string]$ReportDirectory,

    [ValidateRange(1, 300)]
    [int]$TimeoutSeconds = 60
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-ShellSmokeReport {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$ExpectedRunId
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Native shell smoke report is missing: $Path"
    }
    try {
        $report = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    }
    catch {
        throw "Native shell smoke report is malformed JSON: $Path"
    }
    if ($report.runId -isnot [string] -or $report.runId -cne $ExpectedRunId) {
        throw "Native shell smoke report run ID does not match: $Path"
    }
    if ($report.complete -isnot [bool] -or -not $report.complete) {
        throw "Native shell smoke report is incomplete: $Path"
    }
    if ($report.failure -isnot [string] -or $report.failure.Length -ne 0) {
        throw "Native shell smoke report reported a failure: $Path"
    }
    $expectedSteps = @('shell-navigation-edit-autosave', 'shell-close')
    $actualSteps = @($report.steps)
    if ($actualSteps.Count -ne $expectedSteps.Count) {
        throw "Native shell smoke report has unexpected steps: $Path"
    }
    for ($index = 0; $index -lt $expectedSteps.Count; ++$index) {
        if ($actualSteps[$index] -isnot [string] -or $actualSteps[$index] -cne $expectedSteps[$index]) {
            throw "Native shell smoke report has unexpected steps: $Path"
        }
    }
}

$repoRoot = Split-Path $PSScriptRoot -Parent
if (-not $HostExecutable) {
    $HostExecutable = Join-Path $repoRoot 'build\Debug\ui-tests\PresentMonUI.ShellSmokeHost.exe'
}
if (-not $UiExecutable) {
    $UiExecutable = Join-Path $repoRoot 'build\Debug\ui\PresentMonUI.exe'
}
if (-not $ReportDirectory) {
    $ReportDirectory = Join-Path $repoRoot 'build\Debug\ui-shell-smoke'
}
$resolvedHost = [IO.Path]::GetFullPath($HostExecutable)
$resolvedUi = [IO.Path]::GetFullPath($UiExecutable)
$resolvedReportDirectory = [IO.Path]::GetFullPath($ReportDirectory)
if (-not (Test-Path -LiteralPath $resolvedHost -PathType Leaf)) {
    throw "Native shell smoke host is missing: $resolvedHost"
}
if (-not (Test-Path -LiteralPath $resolvedUi -PathType Leaf)) {
    throw "Native UI executable is missing: $resolvedUi"
}
$null = [IO.Directory]::CreateDirectory($resolvedReportDirectory)

$runId = [Guid]::NewGuid().ToString('N')
$reportPath = Join-Path $resolvedReportDirectory "native-ui-shell-smoke-$runId.json"
$dataDirectory = Join-Path $resolvedReportDirectory "data-$runId"
if (Test-Path -LiteralPath $reportPath) {
    throw "Refusing to reuse an existing shell smoke report: $reportPath"
}

try {
    & $resolvedHost $resolvedUi $reportPath $dataDirectory $runId $TimeoutSeconds
    if ($LASTEXITCODE -ne 0) {
        throw "Native shell smoke host exited with code $LASTEXITCODE."
    }
    Assert-ShellSmokeReport -Path $reportPath -ExpectedRunId $runId
    Write-Host "Native shell smoke test passed: $reportPath"
}
finally {
    # Keep isolated persisted settings beside the report for diagnosis.
}
