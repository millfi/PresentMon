Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$harvestScript = Join-Path $PSScriptRoot 'harvest-winui.ps1'
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('presentmon-winui-harvest-' + [Guid]::NewGuid().ToString('N'))
$payload = Join-Path $scratch 'ui'
$wixFile = Join-Path $scratch 'WinUIFiles.wxs'

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw $Message
    }
}

function Assert-Rejected {
    param([string]$ExpectedMessage)
    $rejected = $false
    try {
        & $harvestScript -OutputRoot $payload -WixFile $wixFile
    }
    catch {
        Assert-True -Condition ($_.Exception.Message -like $ExpectedMessage) -Message "Unexpected validation error: $($_.Exception.Message)"
        $rejected = $true
    }
    Assert-True -Condition $rejected -Message "Expected payload validation to reject: $ExpectedMessage"
}

try {
    $null = [IO.Directory]::CreateDirectory($payload)
    $required = @(
        'PresentMonUI.exe', 'Microsoft.UI.Xaml.dll'
    )
    $binary = [byte[]]::new(128)
    $binary[0] = 0x4d
    $binary[1] = 0x5a
    $binary[0x3c] = 0x40
    $binary[0x40] = 0x50
    $binary[0x41] = 0x45
    $binary[0x44] = 0x64
    $binary[0x45] = 0x86
    foreach ($name in $required) {
        [IO.File]::WriteAllBytes((Join-Path $payload $name), $binary)
    }
    $priPath = Join-Path $payload 'PresentMonUI.pri'
    [IO.File]::WriteAllBytes($priPath, [byte[]](1, 2, 3))

    foreach ($relative in @(
        'Assets\nested\icon.png', 'ja-JP\Microsoft.UI.Xaml.resources.dll',
        'PresentMonUI.pdb', 'PresentMonUI.Core.Tests.dll', 'testhost.exe',
        'obj\temporary.dll', 'ref\PresentMonUI.dll', 'publish\PresentMonUI.exe'
    )) {
        $path = Join-Path $payload $relative
        $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
        [IO.File]::WriteAllBytes($path, $binary)
    }

    & $harvestScript -OutputRoot $payload -WixFile $wixFile
    [xml]$xml = Get-Content -LiteralPath $wixFile -Raw
    $namespaces = [Xml.XmlNamespaceManager]::new($xml.NameTable)
    $namespaces.AddNamespace('w', 'http://schemas.microsoft.com/wix/2006/wi')
    $files = $xml.SelectNodes('//w:File', $namespaces)
    Assert-True -Condition ($files.Count -eq 5) -Message "Expected 5 native runtime files, got $($files.Count)."
    Assert-True -Condition ($null -ne $xml.SelectSingleNode('//w:File[@Name="PresentMonUI.pri"]', $namespaces)) -Message 'The native application PRI must be packaged.'
    $nestedFile = $xml.SelectSingleNode('//w:File[@Name="icon.png"]', $namespaces)
    Assert-True -Condition ($null -ne $nestedFile) -Message 'Nested content must be harvested.'
    Assert-True -Condition ($nestedFile.Source -eq '$(var.WinUIOutputDir)\Assets\nested\icon.png') -Message 'Payload source paths must be relative to the WiX output constant.'
    Assert-True -Condition ($xml.SelectNodes('//w:Component[@Win64="yes" and @Guid="*"]', $namespaces).Count -eq 5) -Message 'Every payload file must have an x64 component and stable auto-generated GUID.'
    Assert-True -Condition ($xml.SelectNodes('//w:Directory', $namespaces).Count -eq 3) -Message 'Only runtime content directories should be harvested.'
    $initialContent = [IO.File]::ReadAllText($wixFile)
    $initialTimestamp = (Get-Item -LiteralPath $wixFile).LastWriteTimeUtc
    & $harvestScript -OutputRoot $payload -WixFile $wixFile
    Assert-True -Condition ([IO.File]::ReadAllText($wixFile) -ceq $initialContent) -Message 'Harvest output must be deterministic.'
    Assert-True -Condition ((Get-Item -LiteralPath $wixFile).LastWriteTimeUtc -eq $initialTimestamp) -Message 'An unchanged harvest should preserve its timestamp.'

    Remove-Item -LiteralPath $priPath -Force
    [IO.File]::WriteAllBytes((Join-Path $payload 'resources.pri'), [byte[]](1, 2, 3))
    Assert-Rejected -ExpectedMessage '*missing or empty:*PresentMonUI.pri*'
    Remove-Item -LiteralPath (Join-Path $payload 'resources.pri') -Force
    [IO.File]::WriteAllBytes($priPath, [byte[]]@())
    Assert-Rejected -ExpectedMessage '*missing or empty:*PresentMonUI.pri*'
    [IO.File]::WriteAllBytes($priPath, [byte[]](1, 2, 3))
    Assert-True -Condition ([IO.File]::ReadAllText($wixFile) -ceq $initialContent) -Message 'A missing or empty PRI must leave the previous harvest untouched.'

    [IO.File]::WriteAllBytes((Join-Path $payload 'coreclr.dll'), $binary)
    Assert-Rejected -ExpectedMessage '*must not contain managed .NET runtime files:*coreclr.dll*'
    Remove-Item -LiteralPath (Join-Path $payload 'coreclr.dll') -Force

    $binary[0x44] = 0x4c
    $binary[0x45] = 0x01
    [IO.File]::WriteAllBytes((Join-Path $payload 'PresentMonUI.exe'), $binary)
    Assert-Rejected -ExpectedMessage '*requires x64 binaries*'
    Assert-True -Condition ([IO.File]::ReadAllText($wixFile) -ceq $initialContent) -Message 'A failed validation must leave the previous harvest untouched.'

    Write-Host 'WinUI installer harvest smoke tests passed.'
}
finally {
    $resolvedScratch = [IO.Path]::GetFullPath($scratch)
    $expectedPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar + 'presentmon-winui-harvest-'
    if (-not $resolvedScratch.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a test directory outside the expected temporary location: $resolvedScratch"
    }
    if (Test-Path -LiteralPath $resolvedScratch) {
        Remove-Item -LiteralPath $resolvedScratch -Recurse -Force
    }
}
