# Fluent PresentMon MSIX distribution

`Tools/build-installer.ps1` builds the current x64 installer with MakeAppx.
Choose `-EtwRegistration None` (default) for a standalone MSIX without external ETW tool integration.
Use `-EtwRegistration Msix` for declarative ETW registration with an approved SCCD,
or `-EtwRegistration Msi` for MSIX plus the original WiX ETW provider components.
It does not install packages, trust certificates or change service state.
Historical WiX source remains for legacy MSI/merge-module consumers, but
those projects are no longer part of `PresentMon.sln` or the current build.

## Build and sign

```powershell
.\Tools\build-installer.ps1 -UnsignedRelease -SkipRestore
.\Tools\build-installer.ps1 -SkipRestore -EtwRegistration Msix -Publisher 'CN=Your Publisher' -CertificateThumbprint YOUR_THUMBPRINT -SccdPath .\approved.sccd
.\Tools\build-installer.ps1 -SkipRestore -EtwRegistration Msi -Publisher 'CN=Your Publisher' -CertificateThumbprint YOUR_THUMBPRINT -WixBin .\build\tools\wix3.14.1\bin
```

Unsigned builds can be inspected but cannot be installed normally. Signed
builds require an existing code-signing certificate whose subject exactly
matches `Publisher`. The default store is CurrentUser\My; use
`-CertificateStore` and `-MachineCertificateStore` for another store.
`-TimestampUrl` enables SHA-256 RFC 3161 timestamping. Use a stable publisher
and increment `PresentMonFileVersion` in `Version.props` for upgrades.
No certificate is created or trusted automatically.
For a local test certificate trusted in TrustedPeople, `-DisableUiAccess`
keeps the kernel at normal integrity while still signing the MSIX and MSI.

The staged production kernel has UIAccess enabled and is signed with the
same certificate; the development output is left with UIAccess disabled.
Unsigned packages omit the UIAccess capability. Production signing and
packaged UIAccess behavior must be verified on the target Windows version.

Each run uses a fresh directory under `build\Release\MSIX`. The file
`latest-package.txt` is updated only after package validation succeeds.
Distribute the MSIX, `Dependencies`, `Install.ps1` and `Msix.ps1` together.
In MSI provider mode also include `PresentMon_ETW_x64.msi`; it is signed with
the same certificate as the MSIX. The per-mode pointers are
`latest-msix-etw-package.txt` and `latest-msi-etw-package.txt`.
The `Layout` directory and build reports are not installed.

## ETW registration and the original implementation

The default `None` mode omits the provider DLL/manifest, the eventTracing
extension, custom capabilities and the companion MSI. No SCCD is required.
This does not disable ETW capture. The upstream `Provider/README.md` explicitly
states that installing its manifest is not required to send events to PresentMon.
PresentMon decodes frame-type and application-timing events using built-in
structures. External generic tools such as WPA/GPUView need the manifest metadata
separately to decode these Intel-PresentMon events. The unused experimental
MeasuredInput/MeasuredScreenChange handlers still use TDH and are not enabled by
the production service. Runtime tests must use the real installed package/service.

`Distribution.json` inside the signed package records the selected ETW mode.
The installer reads this to distinguish `None` from `Msi`; a missing MSI cannot
silently turn an MSI-provider build into a registration-free installation.

Commit `f57eb474` installs `provider_group` with WiX `util:EventManifest`.
It also includes standalone `Provider/install_provider.cmd` and
`uninstall_provider.cmd`. Neither the service nor the capture engine needs
new registration code in either distribution mode.

The opt-in Msix ETW mode uses `desktop8:EventTracing` with `desktop7:Scope="machine"` and
`desktop7:CompatMode="classic"`. Windows requires the custom capabilities
`Microsoft.classicAppCompat_8wekyb3d8bbwe` and
`Microsoft.classicAppCompatElevated_8wekyb3d8bbwe`, authorized by an issued
SCCD. An ordinary code-signing certificate alone does not authorize them.
These capabilities are restricted to approved apps; approval is not guaranteed.
Prepare a request using the public certificate that will identify the publisher:

```powershell
.\Tools\new-msix-sccd-request.ps1 -CertificateFile .\Publisher.cer -OutputDirectory .\build\sccd-request
```

The resulting `request.json`, `Publisher.cer`, and `PresentMon.request.sccd`
are request materials, not an authorization. The signature hash is obtained
through Windows CryptoAPI and is different from a certificate thumbprint.
Confirm the production publisher/PFN and certificate before requesting approval
through Microsoft support or your TAM. Obtain the issued SCCD for both capabilities,
then supply it with `-SccdPath`. The build checks the schema, capability names,
package family and selected certificate signature hash and rejects request and
development placeholders. It copies the issued descriptor unchanged. Windows
performs the authoritative catalog signature and authorization checks at deployment.
Unsigned structural builds are available while approval is pending. Developer
mode is never enabled automatically, and unsigned SCCDs are not used as a fallback.

MSI provider mode extracts `provider_group` directly from `PresentMon.wxs`
and links it into `EtwProvider.wxs` with WiX v3.14.1. Thus WiX owns installation,
rollback, upgrade and removal of the two original provider files and their ETW
registration. The MSIX omits those files and the SCCD-dependent declarations.
`Install.ps1` checks both signatures, installs or reuses the companion MSI,
then installs the MSIX. No new `wevtutil` registration code is added.
The provider has its own upgrade identity and product version (initially 1.0.0);
increment its version in `EtwProvider.wxs` when changing provider content.
It appears separately as **Intel PresentMon ETW Provider** in installed apps.
Removing the MSIX leaves that component available to other ETW consumers;
remove the provider MSI separately when it is no longer needed.
Use one ETW ownership mode on a PC. Switching modes requires removing the old
owner first so its uninstall cannot unregister the new owner's provider.

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
bytes, the optional provider MSI payload, and the total for a fresh machine.
Compatible frameworks and an existing compatible provider MSI are reused. Actual
disk allocation also depends on metadata, filesystem clusters, servicing
versions, and MSIX single-instance storage of identical files across packages.
Installer download size is not the optimization target.

## Install, migrate and remove

The MSIX requires Windows 11 (build 22000) or newer. The opt-in Msix ETW mode registers ETW
declaratively after SCCD approval; MSI provider mode retains WiX registration.
The package also declares the LocalSystem `FluentPresentMonService`, Start
menu entry, and `fluent-presentmon.exe` / `fluent-presentmon-console.exe` execution
aliases. The capture kernel continues to launch `ui\PresentMonUI.exe`.
`PresentMonAPI2Loader.dll` is installed beside the capture kernel because
the kernel imports it at startup. The SDK copy alone cannot satisfy that import.

Run `Install.ps1` from Windows PowerShell 5.1 as the intended user with
administrator privileges, required for the packaged LocalSystem service.
Windows validates signature trust. The script reuses compatible installed
frameworks, deploys missing dependencies, installs the app and starts the
service. It stops only a service owned by the installed MSIX during an update,
and restarts it after success or failure. It never removes shared frameworks.

Fluent PresentMon uses package identity `Fluent.PresentMon` and a separate
`FluentPresentMonService`. Its control/log pipes, ETW session name, shared-memory
prefix, service discovery registry key and UI instance mutex are separate from
upstream PresentMon. Settings, loadouts and captures use Documents\FluentPresentMon;
logs and the blocklist use `%LOCALAPPDATA%\FluentPresentMon`.
The upstream `PresentMon.Capture` MSIX and `PresentMonSharedService` may remain
installed and running for comparisons. Do not uninstall or stop them to install
Fluent PresentMon. The installer only updates a service owned by its own package.
MSIX updates and removal are managed by Windows. Remove the app in Settings
or with `Remove-AppxPackage`; do not manually delete WindowsApps files or
remove its shared dependencies. AppData write virtualization is disabled
so existing settings remain at their original path and survive removal.
Captures and user settings are never stored under the read-only package root.

## Verification

For local testing, create and trust a non-CA code-signing certificate:

```powershell
# Run as the ordinary user who will build and use the app:
$cert = .\Tools\new-msix-test-certificate.ps1
# In an administrator Windows PowerShell, use the CertificateFile from above:
.\Tools\trust-msix-test-certificate.ps1 -CertificateFile PATH_TO_CER
# Back in the original user's PowerShell:
.\Tools\build-installer.ps1 -SkipRestore -CertificateThumbprint $cert.Thumbprint -DisableUiAccess
# In administrator Windows PowerShell, run Install.ps1 beside the resulting MSIX.
# Then verify the installed service and overlay as the ordinary user:
.\Tools\test-msix-installed.ps1
```

The private key stays in CurrentUser\My and is not exportable. Only the public
certificate is trusted in LocalMachine\TrustedPeople, never Trusted Root.
`-DisableUiAccess` allows this local certificate setup; overlay tests target a
normal-integrity process and do not establish overlay support over elevated apps.
Keep certificate.json to reuse the same signer for subsequent test builds.
Remove the app with `Get-AppxPackage Fluent.PresentMon | Remove-AppxPackage`.
The explicit test certificate trust remains until separately removed.

Every installer build runs MakeAppx schema/payload validation, then
`Tools/test-msix-package.ps1` checks the artifact contents, required native
files, preset/shader payloads, framework identities and versions, service,
ETW provider, execution aliases, and absence of app-local framework copies.
Mode-specific corrupt-package cases verify rejection of
missing API/root loader/resources, bundled DirectML, unsatisfied framework
versions, incorrect service account, and missing or incorrect ETW registration,
including its required scope, compatibility mode and custom capabilities.
`Tools/test-msix-sccd.ps1 -CertificateFile .\Publisher.cer` checks request
generation, the Windows certificate hash, and rejection of incorrect descriptors.
The companion MSI is also checked for its two provider files, scheduled WiX
registration/unregistration actions and stable upgrade identity. These checks
do not substitute for an approved SCCD or deployment testing.
`-RunNativeTests` additionally runs the native application test suite.
An unpackaged smoke test is not a packaged launch test. A signed deployment
must also be tested for service startup, UI launch, capture/overlay behavior,
update/removal and framework reuse on a clean Windows 11 test machine.

References: [framework deployment](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/deploy-packaged-apps),
[packaged services](https://learn.microsoft.com/en-us/uwp/schemas/appxpackage/uapmanifestschema/element-desktop6-service),
[ETW registration](https://learn.microsoft.com/en-us/uwp/schemas/appxpackage/uapmanifestschema/element-desktop8-eventtracing),
[custom capabilities](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/app-capability-declarations#custom-capabilities),
[SCCD request fields](https://learn.microsoft.com/en-us/windows-hardware/drivers/devapps/hardware-support-app--hsa--steps-for-app-developers),
[MSIX storage](https://learn.microsoft.com/en-us/windows/msix/desktop/desktop-to-uwp-behind-the-scenes).
