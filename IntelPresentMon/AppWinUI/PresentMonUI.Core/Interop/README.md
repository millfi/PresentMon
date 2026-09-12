# Kernel IPC contract

`KernelClient` connects the WinUI application to the existing capture kernel. It
does not implement capture or overlay rendering. Build the UI and kernel from
the same repository revision; historical kernels may reuse action version 1
while having different fields. A malformed or incompatible response closes the
connection and surfaces an exception rather than selecting a guessed schema.

`ConnectAsync(pipeBaseName)` connects both local named pipes and opens the
session using the current UI process ID. The returned `Session` contains the
kernel PID and four build/version strings. Consume `ReadEventsAsync()` once in
the application session and dispatch its events to the WinUI dispatcher. The
bounded event stream starts before `ConnectAsync` returns, preserving startup
failure notifications. Dispose the client during application shutdown.

## Transport

- Requests and their replies use `<base>-in`. Unsolicited kernel events use
  `<base>-out`. Both are duplex byte-mode Windows named pipes.
- A frame starts with a little-endian `uint32` body byte count, excluding the
  prefix. The body is a cereal binary header followed immediately by payload.
- Header order is identifier string, command token (`uint32`), transport status
  (`int32`), execution status (`int32`), packet type (`int32`), header version
  (`uint16`), action version (`uint16`). Both versions are 1.
- Native `MakeResponseHeader` retains the request packet type (0). Events use
  packet type 2 and have no reply.
- Strings have a `uint64` UTF-8 byte count. Vectors have a `uint64` element count.
  Booleans occupy one byte; enums use their native 32-bit underlying types.
  An optional starts with `bool nullopt` (true means absent). A variant starts
  with `int32` index (Graph 0, Readout 1). Fixed graph range arrays have no count.
- Specifications serialize only native fields, preserving the exact order in
  `KernelProcess/kact/PushSpecification.h`. RGB byte values are normalized to
  float components; alpha is already normalized. Native integer fields truncate
  fractional persisted settings to preserve configuration compatibility.
- Introspection follows `KernelProcess/kact/Introspect.h`, including
  `defaultAdapterId` between `systemDeviceId` and `metricAvailabilityReasons`.
  `Unit` currently has no serialized fields.

Requests are serialized with a semaphore to preserve request/reply pairing.
Malformed replies, disconnects and cancellation after an exchange starts close
both pipes. A valid kernel execution error leaves the connection usable. Frame
sizes and collection lengths are bounded, and trailing payload bytes are
rejected. Connection establishment is bounded to 10 seconds and requests to
30 seconds in addition to caller cancellation.

## Operations and events

Requests are `OpenSession`, `Introspect`, `PushSpecification`, `BindHotkey`,
`ClearHotkey`, `SetCapture` and `SetEtlLogging`. Events are `HotkeyFiredAction`,
`TargetLostAction`, `PresentmonInitFailedAction`, `OverlayDiedAction` and
`StalePidAction`.

Hotkey action IDs are 0 toggle capture, 1 toggle overlay,
2 cycle preset and 3 toggle ETL logging. Key codes are the project's `Key::Code`
enumeration, not Windows virtual-key codes. Modifier codes are Alt 1, Ctrl 2,
Shift 4 and Windows 8.

## Verification

`PresentMonUI.Core.Tests/NativeFixtures` contains native cereal fixtures, source
provenance, regeneration scripts and a native verification executable source.
The managed tests compare full outgoing specification bytes against native
output and decode native session/introspection fixtures. Named-pipe tests cover
fragmented reads, both directions, all event types, reply validation, invalid
sizes, cancellation, concurrent requests and shutdown.

Serialization rules were checked against the upstream cereal
[optional](https://github.com/USCiLab/cereal/blob/v1.3.2/include/cereal/types/optional.hpp),
[variant](https://github.com/USCiLab/cereal/blob/v1.3.2/include/cereal/types/variant.hpp),
[array](https://github.com/USCiLab/cereal/blob/v1.3.2/include/cereal/types/array.hpp),
[string](https://github.com/USCiLab/cereal/blob/v1.3.2/include/cereal/types/string.hpp)
and [vector](https://github.com/USCiLab/cereal/blob/v1.3.2/include/cereal/types/vector.hpp)
implementations, then verified with compiled cereal 1.3.2.
