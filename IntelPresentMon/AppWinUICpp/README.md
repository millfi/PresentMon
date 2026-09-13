# PresentMon native WinUI interface

The capture application starts this C++/WinRT WinUI 3 control panel from
`build\Debug\ui\PresentMonUI.exe`. Launch `build\Debug\PresentMon.exe` for a
connected capture session, or start the UI executable alone to inspect layout.
See [`BUILDING.md`](../../BUILDING.md) for the complete build, service, and
installer workflow.

`PresentMonUI.vcxproj` targets x64 and uses C++/WinRT, Windows App SDK 2.0.1,
Windows C++/WinRT 2.0.240405.15, and Windows SDK BuildTools 10.0.26100.4948.
The native package references require Visual Studio 2026 18.7 or newer with the
v145 toolset; 18.9 is the validated toolchain. It creates an unpackaged,
self-contained Windows App SDK deployment. The complete `ui` directory must
remain beside `PresentMon.exe`, and it does not deploy or require the .NET
runtime.

## Build and test

```powershell
.\Tools\build-ui-cpp.ps1 -Configuration Debug -RunTests
```

The core checks are built as
`build\Debug\ui-tests\PresentMonUI.Core.Tests.exe`. `-RunTests` runs the native
core and protocol checks, backdrop smoke, shell smoke, and installer-harvest
validator checks. `build-capture-app.ps1 -RunNativeTests` additionally runs the
real overlay host after building the staged kernel and API. It uses a generated
child-service session, isolated UI data and logs, and a PresentBench target;
the Basic overlay must remain visible for five seconds. The project also
supports `-RestoreOnly` for bootstrap and `-SkipRestore` for repeat local
builds.

## Structure

- `MainWindow.xaml` and `MainWindow.xaml.cpp`: adaptive navigation, process
  search, presets, capture controls, theme selection, and native file pickers.
- `Views`: graph and readout editing, searchable metrics, statistics, devices,
  colors, axes, labels, widget ordering, settings, and global shortcuts.
- `Services/AppSession`: session lifetime, native events, debounced persistence,
  target selection, capture timers, and coordination of UI changes.
- `Services/WindowsServices`: native window and process discovery plus PDH-based
  automatic GPU targeting.
- `Core`: JSON models and migrations, specification construction, atomic
  persistence, and target-selection logic.
- `Interop`: the cereal binary named-pipe client shared with the native kernel.
- `Assets/Presets`: the four built-in overlay loadouts.
- `Assets/BlockLists/TargetBlockList.txt`: the default capture target filter.

The kernel owns overlay rendering, telemetry, frame capture, and global hotkeys.
The UI uses the existing cereal binary protocol. The kernel stages presets and
the blocklist beside its executable for runtime use and installer packaging.

## Automatic targeting

While enabled, automatic targeting scans about once per second, including when a
target is already selected. It compares each eligible application's 3D GPU
running-time increase over the same 100 ms interval. Candidates use the
visible-window and optional target-blocklist filters. The highest positive GPU
load wins; equal loads are ordered by PID.

Scans run sequentially and GPU sampling runs away from the UI thread. Only
candidates with finite, positive GPU load start raw frame tracking. Idle
candidates release their prior trackers. No active candidates, or disabling
automatic targeting, releases the complete probe session. Returning candidates
must warm up their sample window again before they qualify.

The kernel `ProbeGpuBusy` action uses raw per-frame `PM_METRIC_GPU_BUSY` values
without averaging or rounding. A candidate qualifies only after ten finite
samples, with at least one sample different from another. Constant values,
including all zero, invalid or missing samples, and fewer than ten samples do
not qualify. The latest Present timestamp must be within two seconds; a longer
gap resets the sample window.

If no candidate qualifies, the current selection and an active recording remain
unchanged. Process exit still clears the target. Enabling automatic targeting
clears the existing selection; disabling it retains the current target and
releases the probe session. Manual selections can be replaced by a later scan.
Any target change uses the usual capture-stop path. A toggle, manual selection,
or settings change during a scan invalidates that scan result.

## Compatibility and deployment

Existing `preferences.json` and `Loadouts/custom-auto.json` formats and their
supported migrations are preserved. Unknown JSON fields survive round trips.
Writes retain the prior valid backup; a corrupt original is saved for recovery
before replacement. Built-in presets remain read-only, and opening the loadout
editor selects the custom preset. ETL recording remains disabled. Theme choice
applies to the current UI session.

Development runs with `--files-working` use the working directory for settings
and captures. The UI displays the capture directory selected by the native
kernel. Launching the UI directly provides disconnected layout inspection;
capture and editing controls require a connected kernel and available service.
A normal kernel launch uses the installed service, while `--svc-as-child`
launches a staged child service for development or isolated smoke testing.

The title bar uses a Grid, icon, caption, and `Window.SetTitleBar`. The native
backdrop smoke test covers theme changes, minimize and restore, backdrop
replacement, and close behavior on a desktop with acrylic support.
