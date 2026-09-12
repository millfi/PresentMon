# PresentMon WinUI interface

The capture application now starts this native WinUI 3 interface instead of the
Vue/CEF application. Start `build\Debug\PresentMon.exe`, not the UI executable
alone, for a connected capture session. See [BUILDING.md](../../BUILDING.md) for
the complete build, service, and installer instructions.

## Structure

- `MainWindow.xaml` and `MainWindow.xaml.cs`: adaptive navigation, process search,
  presets, capture controls, theme selection, and native file pickers.
- `Views/LoadoutView.cs`: graph/readout editing, searchable metric selection,
  statistics, devices, colors, axes, labels, and widget ordering.
- `Views/SettingsView.cs`: overlay, sampling, capture, diagnostics, preferences,
  and build information. `Views/HotkeyEditor.cs` edits global shortcuts.
- `Services/AppSession.cs`: session lifetime, native events, debounced persistence,
  target selection, capture timers, and coordination of UI changes.
- `Services/WindowsServices.cs`: native window/process discovery and PDH-based
  automatic GPU targeting.
- `PresentMonUI.Core`: JSON models/migrations, specification construction,
  atomic persistence, and the binary named-pipe client.

The native kernel still owns overlay rendering, telemetry, frame capture, and
global hotkeys. The managed client speaks its existing cereal binary protocol;
there is no embedded browser or JavaScript bridge. The retained `AppCef` source
contains shared native action declarations and the four original preset files.

## Compatibility and deployment

The UI targets x64, .NET 10, and Windows App SDK 2.0.1, with Windows 10 build 19041
as its minimum OS version. Its unpackaged `ui` output includes the .NET and Windows
App SDK runtimes. Deploy the whole directory beside the matching kernel, service,
API DLL, shaders, presets, and blocklist; the MSI handles this layout.

Existing `preferences.json` and `Loadouts/custom-auto.json` formats and supported
older schema migrations are preserved. Unknown JSON fields survive round trips.
Writes preserve a previous valid backup, and a failed/corrupt original is retained
as a recovery file before replacement. Built-in presets remain read-only; opening
the loadout editor selects the custom preset. ETL recording remains disabled, as
in the existing GUI. Theme choice currently applies to the current UI session.

Development runs with `--files-working` use the working directory for settings
and captures. The UI displays the capture directory actually used by the native
kernel. Launching the UI directly provides disconnected layout inspection;
capture and editing controls require a connected kernel session.

## Regression checks

Run a successful application build before the managed regression checks:

```powershell
.\Tools\build-capture-app.ps1 -Configuration Debug
dotnet build IntelPresentMon\AppWinUI\PresentMonUI.Core.Tests\PresentMonUI.Core.Tests.csproj
dotnet run --project IntelPresentMon\AppWinUI\PresentMonUI.Core.Tests\PresentMonUI.Core.Tests.csproj --no-build
```

The managed suite has 24 checks covering all four original presets, migrations,
metric/device resolution, nested change tracking, atomic persistence, framing,
cancellation, error responses, and actual native cereal fixtures. The fixture
generator and provenance are in `PresentMonUI.Core.Tests/NativeFixtures`.
`-RunNativeTests` on the build helper additionally builds and runs the C++ suite.

## Desktop verification on 2026-09-12

The application was launched on the actual Windows desktop and exercised through
native mouse/keyboard automation, alongside an isolated D3D11 rendering target.
The existing installed application and its settings were left in place.

Verified behavior:

- Connected process search, target selection, and real overlay rendering.
- Adding a graph and readout, adding a second series, searching for GPU Busy,
  selecting the 99th percentile statistic, and reordering with move buttons.
- Automatic custom-loadout persistence across restarts.
- Manual capture start/stop, a CSV with 4,967 frame records, and its summary CSV.
- Capture duration limited to two seconds, with recording automatically returning
  to idle and both CSV outputs finalized.
- Overlay scaling control dependencies and the native color picker.
- All navigation pages, light/dark themes, and polling-rate changes reflected
  immediately in the status display.
- Native shortcut editor, cancel behavior, and canceling preference reset without
  changing the edited polling rate.
- Duplicate launch with `--duplicate-ui-response yes`, which foregrounded the
  existing WinUI window and returned exit code 2 without creating another window.

Desktop testing exposed and fixed two failures: missing optional DirectX debug
layers now fall back to normal rendering, and ListView reordering uses data items
instead of reparenting live XAML visuals. The C++ suite passed 270 tests afterward.
The documented build helper also passed end-to-end. The final Debug MSI passed
all WiX ICE checks without suppression; extraction verified the sizes and SHA256
hashes of all 540 packaged files (520 UI files) against the current build outputs.

The unsigned Release build also passed all 24 managed checks and 270 native tests.
Its desktop smoke test verified process selection, capture start/stop, 1,006 CSV
frame records with a summary CSV, and the live FPS/frame-time overlay. The Release
MSI passed all WiX ICE checks; all 540 packaged files matched the Release outputs
by size and SHA256. Use `-Configuration Release -UnsignedRelease` on the build
helper to reproduce the application build without a signing certificate. This
opt-in build uses `uiAccess=false`; the default signed Release settings remain.

Remaining manual verification: the file picker opened, but the automation tool
could not target its separate Windows PickerHost process to finish the dialog;
loadout serialization itself is covered by the regression suite. A single
automated drag gesture did not reorder a card, so drag-and-drop remains manually
unverified; native reorder configuration was checked against WinUI source and
the accessible move buttons passed the desktop test. Full Release signing and
installation on a separate clean machine were not exercised.
