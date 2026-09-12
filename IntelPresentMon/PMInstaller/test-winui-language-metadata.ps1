param(
    [Parameter(Mandatory = $true)]
    [string]$WixDirectory,
    [Parameter(Mandatory = $true)]
    [string]$ExtensionPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -Path (Join-Path $WixDirectory 'wix.dll')
Add-Type -Path $ExtensionPath
$normalizer = [pm_installer.pmi_winui_binder_extension].GetMethod('NormalizeLanguage', [Reflection.BindingFlags]'NonPublic,Static')
$longLanguages = '1033,1041,1031,1036,1040'
$cases = @(
    @('winui_file_C60D09577FD5F55E1E55DFB930622EB8', 'Microsoft.ui.xaml.dll', $longLanguages),
    @('winui_file_C104A8CBE0118ED04391D96F6C25DFB3', 'Microsoft.UI.Xaml.Phone.dll', $longLanguages),
    @('winui_file_F218415B4FE94603ACB3A9D39C214D6E', 'gd-gb\Microsoft.ui.xaml.dll.mui', '1169'),
    @('winui_file_93FA4D3918E9894377430E98473D9396', 'gd-gb\Microsoft.UI.Xaml.Phone.dll.mui', '1169'),
    @('winui_file_88EEE8F28EA7AC266255111EA4DA2F7E', 'mi-NZ\Microsoft.ui.xaml.dll.mui', '1153'),
    @('winui_file_1DC483124C37819A8537A0D5B943186B', 'mi-NZ\Microsoft.UI.Xaml.Phone.dll.mui', '1153'),
    @('winui_file_3BB7F795433A6F85072064FCEB771E7C', 'ug-CN\Microsoft.ui.xaml.dll.mui', '1152'),
    @('winui_file_74AC977EF38584A8EBCE9F59279205BF', 'ug-CN\Microsoft.UI.Xaml.Phone.dll.mui', '1152')
)

function Assert-Language {
    param([string]$Id, [string]$Source, [string]$Language, [string]$Expected)
    $actual = $normalizer.Invoke($null, @($Id, $Source, $Language))
    if ($actual -cne $Expected) {
        throw "Unexpected normalized language for ${Source}: expected '$Expected', got '$actual'."
    }
}

foreach ($case in $cases) {
    $source = 'C:\output\ui\' + $case[1]
    Assert-Language $case[0] $source $case[2] '0'
    Assert-Language $case[0] $source.ToUpperInvariant().Replace('\', '/') $case[2] '0'
    Assert-Language ('other_' + $case[0]) $source $case[2] $case[2]
    Assert-Language $case[0] ('C:\output\unrelated\' + $case[1]) $case[2] $case[2]
    Assert-Language $case[0] ($source + '.other') $case[2] $case[2]
    Assert-Language $case[0] $source '1033' '1033'
}
Assert-Language $cases[0][0] 'C:\output\ui\Microsoft.ui.xaml.dll' '1033,1041' '1033,1041'
Assert-Language $cases[0][0] 'C:\output\ui\Microsoft.ui.xaml.dll' '1033,1041,1031,1036,bad' '1033,1041,1031,1036,bad'
Assert-Language $cases[0][0] 'C:\output\ui\Microsoft.ui.xaml.dll' '1033,1041,1031,1036,70000' '1033,1041,1031,1036,70000'
Assert-Language $cases[2][0] 'C:\output\ui\ja-JP\Microsoft.ui.xaml.dll.mui' '1169' '1169'
Assert-Language $cases[2][0] 'C:\output\ui\gd-gb\Microsoft.ui.xaml.dll.mui' '1169,1033' '1169,1033'

# Verify the hard-coded IDs continue to agree with the harvest path contract.
foreach ($case in $cases) {
    $hasher = [Security.Cryptography.SHA256]::Create()
    try {
        $hash = [BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($case[1].ToLowerInvariant()))).Replace('-', '').Substring(0, 32)
        if ($case[0] -cne ('winui_file_' + $hash)) {
            throw "Language metadata file ID no longer matches the harvested path: $($case[1])"
        }
    }
    finally {
        $hasher.Dispose()
    }
}
Write-Host 'WinUI MSI language metadata tests passed (53 behavior cases and 8 harvest IDs).'
