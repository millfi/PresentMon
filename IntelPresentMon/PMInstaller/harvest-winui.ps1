param(
    [Parameter(Mandatory = $true)]
    [string]$OutputRoot,
    [Parameter(Mandatory = $true)]
    [string]$WixFile
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-PayloadFiles {
    param([string]$Directory)

    foreach ($entry in Get-ChildItem -LiteralPath $Directory -Force) {
        if (($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "The WinUI payload must not contain symbolic links or junctions: $($entry.FullName)"
        }
        if ($entry.PSIsContainer) {
            if ($entry.Name -notmatch '^(?:bin|obj|ref|refint|publish|tests?|TestResults|\.git|\.vs)$') {
                Get-PayloadFiles -Directory $entry.FullName
            }
        }
        elseif ($entry.Name -notmatch '\.(?:pdb|dbg|ipdb|iobj|ilk|exp|lib|lastbuildstate)$' -and
                $entry.Name -notmatch '(?:^|\.)Tests?(?:\.|$)|^testhost(?:\.|$)|^Microsoft\.TestPlatform\.|^xunit\.|^nunit\.|^MSTest\.' -and
                $entry.Name -notmatch '\.runtimeconfig\.dev\.json$|\.vshost\.') {
            $entry.FullName
        }
    }
}

function Assert-X64Binary {
    param([string]$Path)

    $stream = [IO.File]::OpenRead($Path)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5a4d) {
            throw "Invalid executable in the WinUI payload: $Path"
        }
        $stream.Position = 0x3c
        $peOffset = $reader.ReadUInt32()
        if ($peOffset -gt $stream.Length - 6) {
            throw "Invalid PE header in the WinUI payload: $Path"
        }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550 -or $reader.ReadUInt16() -ne 0x8664) {
            throw "The WinUI installer requires x64 binaries: $Path"
        }
    }
    finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

function Get-StableId {
    param([string]$Prefix, [string]$RelativePath)

    $hash = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($RelativePath.Replace('/', '\').ToLowerInvariant())
        $hex = [BitConverter]::ToString($hash.ComputeHash($bytes)).Replace('-', '').Substring(0, 32)
        return "${Prefix}_$hex"
    }
    finally {
        $hash.Dispose()
    }
}

$root = [IO.Path]::GetFullPath($OutputRoot).TrimEnd('\', '/')
if (-not (Test-Path -LiteralPath $root -PathType Container)) {
    throw "WinUI output is missing: $root. Build PresentMonUI for x64 with SelfContained=true and WindowsAppSDKSelfContained=true."
}
if (((Get-Item -LiteralPath $root).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "The WinUI output directory must not be a symbolic link or junction: $root"
}

$requiredFiles = @(
    'PresentMonUI.exe',
    'PresentMonUI.dll',
    'PresentMonUI.deps.json',
    'PresentMonUI.runtimeconfig.json',
    'PresentMonUI.pri',
    'coreclr.dll',
    'hostfxr.dll',
    'hostpolicy.dll',
    'System.Private.CoreLib.dll',
    'Microsoft.UI.Xaml.dll'
)
foreach ($name in $requiredFiles) {
    $path = Join-Path $root $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) {
        throw "Required WinUI runtime file is missing or empty: $path. Build a self-contained x64 output before building the installer."
    }
}
foreach ($name in @('PresentMonUI.exe', 'coreclr.dll', 'Microsoft.UI.Xaml.dll')) {
    Assert-X64Binary -Path (Join-Path $root $name)
}

$runtimeConfig = Get-Content -LiteralPath (Join-Path $root 'PresentMonUI.runtimeconfig.json') -Raw | ConvertFrom-Json
$runtimeOptions = $runtimeConfig.runtimeOptions
if ($null -ne $runtimeOptions.PSObject.Properties['framework'] -or
    $null -ne $runtimeOptions.PSObject.Properties['frameworks'] -or
    $null -eq $runtimeOptions.PSObject.Properties['includedFrameworks'] -or
    @($runtimeOptions.includedFrameworks).Count -eq 0) {
    throw 'PresentMonUI.runtimeconfig.json must describe a self-contained .NET application with includedFrameworks.'
}
$dependencies = Get-Content -LiteralPath (Join-Path $root 'PresentMonUI.deps.json') -Raw | ConvertFrom-Json
if ($dependencies.runtimeTarget.name -notmatch '/win-x64$') {
    throw 'PresentMonUI.deps.json must target the win-x64 runtime.'
}

[string[]]$files = @(Get-PayloadFiles -Directory $root)
[Array]::Sort($files, [StringComparer]::OrdinalIgnoreCase)
$rootPrefix = $root + [IO.Path]::DirectorySeparatorChar
$directories = @{}
$relativeFiles = [Collections.Generic.List[string]]::new()
foreach ($file in $files) {
    if (-not $file.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "WinUI payload file is outside the output directory: $file"
    }
    $relativePath = $file.Substring($rootPrefix.Length)
    $relativeFiles.Add($relativePath)
    $parent = [IO.Path]::GetDirectoryName($relativePath)
    while ($parent) {
        $directories[$parent] = Get-StableId -Prefix 'winui_dir' -RelativePath $parent
        $parent = [IO.Path]::GetDirectoryName($parent)
    }
}

$namespace = 'http://schemas.microsoft.com/wix/2006/wi'
$document = [Xml.XmlDocument]::new()
$null = $document.AppendChild($document.CreateXmlDeclaration('1.0', 'utf-8', $null))
$wix = $document.CreateElement('Wix', $namespace)
$null = $document.AppendChild($wix)
$null = $wix.AppendChild($document.CreateComment(' Generated from the self-contained WinUI output. Do not edit. '))
$fragment = $document.CreateElement('Fragment', $namespace)
$null = $wix.AppendChild($fragment)
$directoryRef = $document.CreateElement('DirectoryRef', $namespace)
$directoryRef.SetAttribute('Id', 'pm_app_ui_folder')
$null = $fragment.AppendChild($directoryRef)
$directoryNodes = @{ '' = $directoryRef }
[string[]]$directoryPaths = @($directories.Keys)
[Array]::Sort($directoryPaths, [StringComparer]::OrdinalIgnoreCase)
foreach ($relativePath in $directoryPaths) {
    $node = $document.CreateElement('Directory', $namespace)
    $node.SetAttribute('Id', $directories[$relativePath])
    $node.SetAttribute('Name', [IO.Path]::GetFileName($relativePath))
    $parent = [IO.Path]::GetDirectoryName($relativePath)
    $null = $directoryNodes[$parent].AppendChild($node)
    $directoryNodes[$relativePath] = $node
}

$componentGroup = $document.CreateElement('ComponentGroup', $namespace)
$componentGroup.SetAttribute('Id', 'WinUIFiles')
$null = $fragment.AppendChild($componentGroup)
foreach ($relativePath in $relativeFiles) {
    $component = $document.CreateElement('Component', $namespace)
    $component.SetAttribute('Id', (Get-StableId -Prefix 'winui_cmp' -RelativePath $relativePath))
    $component.SetAttribute('Guid', '*')
    $component.SetAttribute('Win64', 'yes')
    $parent = [IO.Path]::GetDirectoryName($relativePath)
    $directoryId = if ($parent) { $directories[$parent] } else { 'pm_app_ui_folder' }
    $component.SetAttribute('Directory', $directoryId)
    $fileNode = $document.CreateElement('File', $namespace)
    $fileNode.SetAttribute('Id', (Get-StableId -Prefix 'winui_file' -RelativePath $relativePath))
    $fileNode.SetAttribute('Name', [IO.Path]::GetFileName($relativePath))
    $fileNode.SetAttribute('Source', '$(var.WinUIOutputDir)\' + $relativePath)
    $fileNode.SetAttribute('KeyPath', 'yes')
    $null = $component.AppendChild($fileNode)
    $null = $componentGroup.AppendChild($component)
}

$destination = [IO.Path]::GetFullPath($WixFile)
if ($destination.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The generated WiX file must be outside the WinUI runtime output directory.'
}
$null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
$settings = [Xml.XmlWriterSettings]::new()
$settings.Indent = $true
$settings.Encoding = [Text.UTF8Encoding]::new($false)
$stream = [IO.MemoryStream]::new()
$writer = [Xml.XmlWriter]::Create($stream, $settings)
try {
    $document.Save($writer)
    $writer.Flush()
    $content = [Text.Encoding]::UTF8.GetString($stream.ToArray())
}
finally {
    $writer.Dispose()
    $stream.Dispose()
}
if (-not (Test-Path -LiteralPath $destination) -or [IO.File]::ReadAllText($destination) -cne $content) {
    [IO.File]::WriteAllText($destination, $content, [Text.UTF8Encoding]::new($false))
}
Write-Host "Validated and harvested $($relativeFiles.Count) WinUI runtime files into $destination"
