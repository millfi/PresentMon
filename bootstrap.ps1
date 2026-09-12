[CmdletBinding()]
param(
    [switch]$SkipAuxiliaryData
)

$ErrorActionPreference = "Stop"

$repoRoot = $PSScriptRoot
$originalLocation = Get-Location

function Invoke-BootstrapStep {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [scriptblock]$Action
    )

    Write-Host ""
    Write-Host "==> $Name"
    $global:LASTEXITCODE = 0
    & $Action

    if ($LASTEXITCODE -ne $null -and $LASTEXITCODE -ne 0) {
        throw "$Name failed with exit code $LASTEXITCODE."
    }
}

try {
    if (-not $SkipAuxiliaryData) {
        Invoke-BootstrapStep "Pull auxiliary test data" {
            & (Join-Path $repoRoot "Tests\pull-aux.ps1")
        }
    }

    Invoke-BootstrapStep "Restore WinUI application" {
        dotnet restore (Join-Path $repoRoot "IntelPresentMon\AppWinUI\PresentMonUI.csproj") -p:Platform=x64 -p:RuntimeIdentifier=win-x64
    }
} finally {
    Set-Location $originalLocation
}
