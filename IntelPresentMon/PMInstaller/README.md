# PresentMon MSIX distribution

`Tools/build-installer.ps1` builds the current x64 installer with MakeAppx.
It does not install packages, trust certificates or change service state.
Historical WiX source remains for legacy MSI/merge-module consumers, but
those projects are no longer part of `PresentMon.sln` or the current build.

## Build and sign

```powershell
.\Tools\build-installer.ps1 -UnsignedRelease -SkipRestore
.\Tools\build-installer.ps1 -SkipRestore -Publisher 'CN=Your Publisher' -CertificateThumbprint YOUR_THUMBPRINT
```

Unsigned builds can be inspected but cannot be installed normally. Signed
builds require an existing code-signing certificate whose subject exactly
matches `Publisher`. The default store is CurrentUser\My; use
`-CertificateStore` and `-MachineCertificateStore` for another store.
`-TimestampUrl` enables SHA-256 RFC 3161 timestamping. Use a stable publisher
and increment `PresentMonFileVersion` in `Version.props` for upgrades.
No certificate is created or trusted automatically.

The staged production kernel has UIAccess enabled and is signed with the
same certificate; the development output is left with UIAccess disabled.
Unsigned packages omit the UIAccess capability. Production signing and
packaged UIAccess behavior must be verified on the target Windows version.

Each run uses a fresh directory under `build\Release\MSIX`. The file
`latest-package.txt` is updated only after package validation succeeds.
Distribute the MSIX, `Dependencies`, `Install.ps1` and `Msix.ps1` together.
The `Layout` directory and build reports are not installed.

## Dependencies and installed size

The app depends on the official `Microsoft.WindowsAppRuntime.2` framework
resolved by the UI build and on `Microsoft.VCLibs.140.00.UWPDesktop`.
Identity, publisher and minimum version are read from the signed packages,
not guessed from NuGet version strings. Dependencies are distributed beside
the MSIX, never nested in it. Windows owns their sharing and lifetime.

DirectML, ONNX Runtime and other Windows App SDK components are absent from
the app payload. They remain in Microsoft's framework. This intentionally
preserves sharing and Microsoft servicing instead of maintaining a custom
trimmed framework. The optional UCI runtime remains app-specific.

`size-report.json` records logical uncompressed app bytes, shared framework
bytes, and the total for a machine without the frameworks. With compatible
frameworks already installed, only the app payload is additional. Actual
disk allocation also depends on metadata, filesystem clusters, servicing
versions, and MSIX single-instance storage of identical files across packages.
Installer download size is not the optimization target.

## Install, migrate and remove

The MSIX requires Windows 11 (build 22000) or newer. `desktop8:EventTracing`
replaces MSI's provider-registration custom action and requires that OS.
The package also declares the LocalSystem `PresentMonSharedService`, Start
menu entry, and `presentmon-cli.exe` / `presentmon-console.exe` execution
aliases. The capture kernel continues to launch `ui\PresentMonUI.exe`.

Run `Install.ps1` from Windows PowerShell 5.1 as the intended user with
administrator privileges, required for the packaged LocalSystem service.
Windows validates signature trust. The script reuses compatible installed
frameworks, deploys missing dependencies, installs the app and starts the
service. It never removes or downgrades shared frameworks.

MSI-to-MSIX is not an in-place upgrade. Before removing the MSI, back up
`%LOCALAPPDATA%\Intel\PresentMon` and captures/loadouts under Documents.
The old MSI deletes the AppData directory on uninstall. Uninstall the MSI
through Settings, restore the settings backup, then run `Install.ps1`.
The script refuses migration while that MSI is registered. It does not
silently remove a service shared with other products. If another product
owns `PresentMonSharedService`, resolve that ownership before deployment.

MSIX updates and removal are managed by Windows. Remove the app in Settings
or with `Remove-AppxPackage`; do not manually delete WindowsApps files or
remove its shared dependencies. AppData write virtualization is disabled
so existing settings remain at their original path and survive removal.
Captures and user settings are never stored under the read-only package root.

## Verification

Every installer build runs MakeAppx schema/payload validation, then
`Tools/test-msix-package.ps1` checks the artifact contents, required native
files, preset/shader payloads, framework identities and versions, service,
ETW provider, execution aliases, and absence of app-local framework copies.
Six corrupt-package regression cases verify that the validator rejects
missing API/resources, bundled DirectML, unsatisfied framework versions,
incorrect service account and incorrect ETW identity.
`-RunNativeTests` additionally runs the native application test suite.
An unpackaged smoke test is not a packaged launch test. A signed deployment
must also be tested for service startup, UI launch, capture/overlay behavior,
update/removal and framework reuse on a clean Windows 11 test machine.

References: [framework deployment](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/deploy-packaged-apps),
[packaged services](https://learn.microsoft.com/en-us/uwp/schemas/appxpackage/uapmanifestschema/element-desktop6-service),
[ETW registration](https://learn.microsoft.com/en-us/uwp/schemas/appxpackage/uapmanifestschema/element-desktop8-eventtracing),
[MSIX storage](https://learn.microsoft.com/en-us/windows/msix/desktop/desktop-to-uwp-behind-the-scenes).
