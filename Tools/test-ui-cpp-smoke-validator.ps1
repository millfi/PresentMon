Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$smokeScript = Join-Path $PSScriptRoot 'test-ui-cpp-smoke.ps1'
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('presentmon-native-ui-smoke-' + [Guid]::NewGuid().ToString('N'))
$runId = [Guid]::NewGuid().ToString('N')
$freshAfterUtc = [DateTime]::UtcNow

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

function Write-Report {
    param([string]$Path, [object]$Report)

    [IO.File]::WriteAllText($Path, ($Report | ConvertTo-Json -Compress), [Text.UTF8Encoding]::new($false))
}

function Assert-Rejected {
    param([string]$Name, [string]$Path, [DateTime]$NotBeforeUtc)

    $rejected = $false
    try {
        & $smokeScript -ReportPath $Path -RunId $runId -NotBeforeUtc $NotBeforeUtc
    }
    catch {
        $rejected = $true
    }
    if (-not $rejected) {
        throw "Validator accepted the $Name report."
    }
}

try {
    $null = [IO.Directory]::CreateDirectory($scratch)
    $validPath = Join-Path $scratch 'valid.json'
    $valid = [ordered]@{
        runId = $runId
        complete = $true
        steps = @(Get-ExpectedSmokeSteps)
        failure = ''
    }
    Write-Report -Path $validPath -Report $valid
    & $smokeScript -ReportPath $validPath -RunId $runId -NotBeforeUtc $freshAfterUtc.AddSeconds(-1)

    Assert-Rejected -Name 'missing' -Path (Join-Path $scratch 'missing.json') -NotBeforeUtc $freshAfterUtc

    $stalePath = Join-Path $scratch 'stale.json'
    Write-Report -Path $stalePath -Report $valid
    (Get-Item -LiteralPath $stalePath).LastWriteTimeUtc = $freshAfterUtc.AddMinutes(-2)
    Assert-Rejected -Name 'stale' -Path $stalePath -NotBeforeUtc $freshAfterUtc

    $malformedPath = Join-Path $scratch 'malformed.json'
    [IO.File]::WriteAllText($malformedPath, '{', [Text.UTF8Encoding]::new($false))
    Assert-Rejected -Name 'malformed' -Path $malformedPath -NotBeforeUtc $freshAfterUtc.AddSeconds(-1)

    $incompletePath = Join-Path $scratch 'incomplete.json'
    $incomplete = [ordered]@{ runId = $runId; complete = $false; steps = @(Get-ExpectedSmokeSteps); failure = '' }
    Write-Report -Path $incompletePath -Report $incomplete
    Assert-Rejected -Name 'incomplete' -Path $incompletePath -NotBeforeUtc $freshAfterUtc.AddSeconds(-1)

    $missingStepPath = Join-Path $scratch 'missing-step.json'
    $missingSteps = @(Get-ExpectedSmokeSteps)
    $missingSteps = @($missingSteps | Select-Object -Skip 1)
    $missingStep = [ordered]@{ runId = $runId; complete = $true; steps = $missingSteps; failure = '' }
    Write-Report -Path $missingStepPath -Report $missingStep
    Assert-Rejected -Name 'missing-step' -Path $missingStepPath -NotBeforeUtc $freshAfterUtc.AddSeconds(-1)

    $duplicatePath = Join-Path $scratch 'duplicate.json'
    $duplicateSteps = [Collections.Generic.List[string]]::new()
    $duplicateSteps.AddRange([string[]](Get-ExpectedSmokeSteps))
    $duplicateSteps.Insert(1, 'activate')
    $duplicate = [ordered]@{ runId = $runId; complete = $true; steps = @($duplicateSteps); failure = '' }
    Write-Report -Path $duplicatePath -Report $duplicate
    Assert-Rejected -Name 'duplicate-step' -Path $duplicatePath -NotBeforeUtc $freshAfterUtc.AddSeconds(-1)

    $failedPath = Join-Path $scratch 'failed.json'
    $failed = [ordered]@{ runId = $runId; complete = $true; steps = @(Get-ExpectedSmokeSteps); failure = 'native failure' }
    Write-Report -Path $failedPath -Report $failed
    Assert-Rejected -Name 'failed' -Path $failedPath -NotBeforeUtc $freshAfterUtc.AddSeconds(-1)

    Write-Host 'Native UI smoke report validator regression tests passed.'
}
finally {
    $resolvedScratch = [IO.Path]::GetFullPath($scratch)
    $expectedPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar + 'presentmon-native-ui-smoke-'
    if (-not $resolvedScratch.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a test directory outside the expected temporary location: $resolvedScratch"
    }
    if (Test-Path -LiteralPath $resolvedScratch) {
        Remove-Item -LiteralPath $resolvedScratch -Recurse -Force
    }
}
