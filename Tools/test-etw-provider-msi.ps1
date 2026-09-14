[CmdletBinding()]
param([Parameter(Mandatory)][string]$Package)
$ErrorActionPreference = 'Stop'
$installer = New-Object -ComObject WindowsInstaller.Installer
$database = $installer.OpenDatabase([IO.Path]::GetFullPath($Package), 0)
function Read-MsiColumn([string]$Query) {
    $view = $database.OpenView($Query)
    try {
        [void]$view.Execute()
        while ($record = $view.Fetch()) {
            try { $record.StringData(1) }
            finally { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($record) }
        }
    }
    finally {
        [void]$view.Close()
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($view)
    }
}
try {
$files = @(Read-MsiColumn 'SELECT `FileName` FROM `File`')
if ($files.Count -ne 2 -or -not ($files -match 'Intel-PresentMon.dll$') -or -not ($files -match 'Intel-PresentMon.man$')) {
    throw 'The companion MSI must contain only the original ETW provider DLL and manifest.'
}
$actions = @(Read-MsiColumn 'SELECT `Action` FROM `InstallExecuteSequence`')
if (-not ($actions -match 'EventManifestRegister') -or -not ($actions -match 'EventManifestUnregister')) {
    throw 'The original WiX EventManifest registration and unregistration actions must be scheduled.'
}
$upgrade = @(Read-MsiColumn "SELECT ``Value`` FROM ``Property`` WHERE ``Property`` = 'UpgradeCode'")
if ($upgrade.Count -ne 1 -or $upgrade[0] -ne '{1EE15DAC-9BC3-45D4-91FA-8F56163FCD28}') { throw 'Unexpected provider MSI upgrade identity.' }
Write-Host 'ETW MSI validation passed: exactly two provider files, WiX registration/unregistration and stable upgrade identity.'
}
finally {
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($database)
    [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($installer)
}
