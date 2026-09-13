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
    throw "WinUI output is missing: $root. Build the native PresentMonUI project for x64 with Windows App SDK self-contained deployment enabled."
}
if (((Get-Item -LiteralPath $root).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "The WinUI output directory must not be a symbolic link or junction: $root"
}

$requiredFiles = @(
    'PresentMonUI.exe',
    'Microsoft.UI.Xaml.dll',
    'resources.pri'
)
foreach ($name in $requiredFiles) {
    $path = Join-Path $root $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) {
        throw "Required native WinUI runtime file is missing or empty: $path. Build the self-contained x64 output before building the installer."
    }
}
foreach ($name in @('PresentMonUI.exe', 'Microsoft.UI.Xaml.dll')) {
    Assert-X64Binary -Path (Join-Path $root $name)
}

$managedRuntimeFiles = @(
    'PresentMonUI.dll',
    'PresentMonUI.deps.json',
    'PresentMonUI.runtimeconfig.json',
    'coreclr.dll',
    'hostfxr.dll',
    'hostpolicy.dll',
    'System.Private.CoreLib.dll'
)
foreach ($name in $managedRuntimeFiles) {
    $path = Join-Path $root $name
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        throw "The native WinUI payload must not contain managed .NET runtime files: $path"
    }
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
