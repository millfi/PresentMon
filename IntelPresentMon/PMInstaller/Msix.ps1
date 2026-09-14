Set-StrictMode -Version Latest
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Read-MsixManifest {
    param([Parameter(Mandatory)][string]$Path)
    $archive = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $entry = $archive.GetEntry('AppxManifest.xml')
        if (-not $entry) { throw "Missing AppxManifest.xml: $Path" }
        $reader = [IO.StreamReader]::new($entry.Open())
        try { return [xml]$reader.ReadToEnd() } finally { $reader.Dispose() }
    }
    finally { $archive.Dispose() }
}

function Read-MsixDistribution {
    param([Parameter(Mandatory)][string]$Path)
    $archive = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $entry = $archive.GetEntry('Distribution.json')
        if (-not $entry) { throw "Missing Distribution.json: $Path" }
        $reader = [IO.StreamReader]::new($entry.Open())
        try { $distribution = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
        if ($distribution.EtwRegistration -notin @('None', 'Msix', 'Msi')) { throw 'Invalid ETW distribution mode.' }
        return $distribution
    }
    finally { $archive.Dispose() }
}

function Invoke-MsixTool {
    param([string]$Executable, [string[]]$Arguments)
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed with exit code $LASTEXITCODE." }
}

function Get-MsixPayloadBytes {
    param([string]$Path)
    $archive = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        return [long]($archive.Entries | Where-Object {
            $_.FullName -notmatch '^(AppxMetadata/|AppxBlockMap.xml$|AppxSignature.p7x$|\[Content_Types\].xml$)'
        } | Measure-Object Length -Sum).Sum
    }
    finally { $archive.Dispose() }
}
