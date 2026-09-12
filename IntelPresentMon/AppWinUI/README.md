# PresentMon WinUI interface

The capture application starts this native WinUI 3 interface. Start
`build\Debug\PresentMon.exe` for a connected capture session. See
[BUILDING.md](../../BUILDING.md) for
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
- `PresentMonUI.Core/AutomaticTargeting.cs`: periodic selection among candidates
  with changing raw GPU Busy samples, with revision checks for scans superseded
  by user input.
- `PresentMonUI.Core`: JSON models/migrations, specification construction,
  atomic persistence, and the binary named-pipe client.
- `Assets/Presets`: the four built-in overlay loadouts.
- `Assets/BlockLists/TargetBlockList.txt`: the default capture target filter.

The native kernel still owns overlay rendering, telemetry, frame capture, and
global hotkeys. The managed client speaks its existing cereal binary protocol.
The kernel build stages the presets and blocklist from this project's assets
beside the application for runtime use and installer packaging.

## Automatic targeting

While enabled, automatic targeting scans approximately once per second, even
when a target is already selected. It compares each eligible application's 3D
GPU running-time increase over the same 100 ms interval. Candidates still use
the visible-window and optional target-blocklist filters. The highest positive
GPU load wins; equal loads are ordered by PID.

Scans run sequentially without overlapping, and GPU sampling runs off the UI
thread. Only candidates with finite, positive GPU load start raw frame tracking;
idle candidates release their previous probe trackers. No active candidates or disabling
automatic targeting releases the entire probe session. Periodic discovery skips
window titles and display sorting. Returning candidates must warm up their raw
sample window again before they qualify.

The kernel's `ProbeGpuBusy` action maintains separate PresentMon tracking for
the candidates. It consumes raw per-frame `PM_METRIC_GPU_BUSY` values without
averaging or rounding and keeps each process's latest ten samples. A candidate
qualifies only when all ten samples are finite and at least one differs from
another. Constant values (including all zero), missing/invalid samples, and
fewer than ten samples do not qualify. This replaces the previous Presented FPS
check. No minimum size of the raw value change is imposed.

The latest Present timestamp must also be within the last two seconds. A gap of
more than two seconds resets the sample window, so an inactive process cannot
qualify using an old change. New or resumed candidates need ten samples before
selection; the delay depends on their frame rate and ETW delivery.

If no candidate qualifies, the current selection and any active recording are
preserved, even when the current target also fails the raw-sample check. An
empty selection stays empty. Later scans can still switch to an eligible
candidate. Process exit is handled separately and still clears the target.
Enabling automatic targeting clears the existing selection; disabling it keeps
the current target and releases the probe session. Manual selections while enabled can be replaced
on a subsequent scan. Any target change uses the usual capture-stop path, so an
active recording ends before switching. An OFF/ON toggle, manual selection, or
settings change during a scan invalidates that scan's result.

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

The title bar uses a Grid, icon, and caption with `Window.SetTitleBar`, following
the [custom title bar guidance](https://learn.microsoft.com/en-us/windows/apps/develop/title-bar?tabs=winui3).
The Windows App SDK `TitleBar` control crashed with `E_INVALIDARG` when the native
launch tests minimized the window immediately after startup. Replacing that
control passed both existing foreground/restore tests with their original timing
and assertions. Removing only its icon did not fix the crash; both application
icon assets remain in use.

## Regression checks

Run a successful application build before the managed regression checks:

```powershell
.\Tools\build-capture-app.ps1 -Configuration Debug
dotnet build IntelPresentMon\AppWinUI\PresentMonUI.Core.Tests\PresentMonUI.Core.Tests.csproj
dotnet run --project IntelPresentMon\AppWinUI\PresentMonUI.Core.Tests\PresentMonUI.Core.Tests.csproj --no-build
```

The managed suite has 30 checks covering all four built-in presets, migrations,
metric/device resolution, nested change tracking, atomic persistence, native
launch arguments and unsupported options, framing, cancellation, error responses,
actual native cereal fixtures, and automatic target selection/reselection. The
automatic targeting checks cover unmeasurable candidates, game exit/restart,
switching between running games, and superseded scans. The fixture generator and provenance are in
`PresentMonUI.Core.Tests/NativeFixtures`.
`-RunNativeTests` on the build helper additionally builds and runs the C++ suite.
`GpuBusySampleWindowTests` covers ten-sample warmup, constant values, a change at
any sample position, sliding-window expiry, invalid samples, and raw precision.
`RealtimeGpuBusyProbeFiltersAndReacquiresTargets` in `PresentMonAPI2Tests`
exercises raw GPU Busy eligibility with real rendering, non-presenting
processes, process restart, stale data expiry, and independent overlay tracking.

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
The documented build helper also passed end-to-end. Before the legacy-code and
unused-asset cleanup, the Debug MSI passed
all WiX ICE checks without suppression; extraction verified the sizes and SHA256
hashes of its 540 packaged files (520 UI files) against the build outputs at that
time. These counts describe that earlier package.

The earlier unsigned Release smoke test verified process selection, capture
start/stop, 1,006 CSV frame records with a summary CSV, and the live FPS/frame-time
overlay. Use `-Configuration Release -UnsignedRelease` on the build
helper to reproduce the application build without a signing certificate. This
opt-in build uses `uiAccess=false`; the default signed Release settings remain.

After the legacy-code and unused-asset cleanup and custom title bar fix, Debug
and Release application/core builds completed with zero warnings and errors.
Each configuration passed all 25 managed checks, 270 native tests, and 19 selected
IPC/action/UI integration tests without skips. The integration tests include the
original duplicate-launch and minimized-window foreground/restore assertions.
Only `AppIcon.ico` and `AppIcon.png` remain in the deployed UI assets directory;
presets and the blocklist are staged by the native application.

The final Release desktop smoke test selected the isolated D3D11 target and used
Ctrl+Shift+K to start and stop capture, verifying the recording and idle states.
The resulting CSV contained 813 frames with its summary CSV, and the live overlay
showed 32.3 FPS and 31.0 ms. When the target exited normally, the UI returned to no
application selected and idle. Manual minimize/restore and maximize/restore also
passed with the custom title bar. Evidence is in
`build/winui-verification/cleanup-{capture,overlay,target-lost,restored}.jpg` and
`build/Release/Captures/pmcap-PresentMonValidationTarget.exe-260912-204442.csv`.

The final unsigned Release MSI passed all WiX ICE checks without suppression.
All 532 packaged files (512 UI files) matched the Release outputs by size and
SHA256. The MSI is 90,401,546 bytes with SHA256
`0A842C81F26566577485DC1AEE36C6A72E70AB6D0E81E280B2E5BD3B76D5E90F`.

Remaining manual verification: the file picker opened, but the automation tool
could not target its separate Windows PickerHost process to finish the dialog;
loadout serialization itself is covered by the regression suite. A single
automated drag gesture did not reorder a card, so drag-and-drop remains manually
unverified; native reorder configuration was checked against WinUI source and
the accessible move buttons passed the desktop test. Independent Window created
a `PresentMonDataDisplay` window, but screenshot approval timed out and the test
target exited before its contents and placement could be verified. That option
was restored to off. Full Release signing and installation on a separate clean
machine were not exercised.
