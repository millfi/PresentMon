// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;
using PresentMon.UI.Core;

namespace PresentMon.UI.Services;

public sealed record ProcessEntry(int Pid, string Name, string WindowName)
{
    public string DisplayName => string.IsNullOrWhiteSpace(WindowName)
        ? $"{Name} ({Pid})" : $"{Name} ({Pid}) - {WindowName}";

    public override string ToString() => DisplayName;
}

public static class WindowsServices
{
    private const string WindowIdentityProperty = "IntelPresentMon.UiMutexSuffixAtom";
    private const uint PdhMoreData = 0x800007D2;
    private const uint PdhNoData = 0x800007D5;
    private const uint PdhNoInstance = 0x800007D1;
    private const uint PdhFormatDouble = 0x00000200;

    public static Task<IReadOnlyList<ProcessEntry>> EnumerateProcessesAsync(
        IReadOnlySet<string>? blocklist = null,
        CancellationToken cancellationToken = default)
    {
        var excludedNames = CopyBlocklist(blocklist);
        return Task.Run(() => EnumerateProcesses(excludedNames, cancellationToken), cancellationToken);
    }

    public static Task<IReadOnlyList<GpuProcessSample>> GetGpuProcessSamplesAsync(
        IReadOnlySet<string>? blocklist = null,
        CancellationToken cancellationToken = default)
    {
        var excludedNames = CopyBlocklist(blocklist);
        return Task.Run<IReadOnlyList<GpuProcessSample>>(async () =>
        {
            var candidates = EnumerateProcesses(excludedNames, cancellationToken, includeDisplayDetails: false)
                .ToDictionary(process => process.Pid);
            if (candidates.Count == 0)
            {
                return [];
            }

            ThrowIfPdhFailed(PdhOpenQueryW(null, 0, out var query), "Opening the GPU performance query");
            try
            {
                // English counter paths also work on localized Windows installations.
                ThrowIfPdhFailed(PdhAddEnglishCounterW(query, @"\GPU Engine(*)\Running time", 0, out var counter),
                    "Adding the GPU running-time counter");
                if (!CollectGpuData(query))
                {
                    return candidates.Keys.Select(pid => new GpuProcessSample(pid, 0)).ToArray();
                }

                // Compare the same 100 ms interval for every candidate.
                if (!CollectGpuData(query))
                {
                    return candidates.Keys.Select(pid => new GpuProcessSample(pid, 0)).ToArray();
                }
                var first = ReadGpuRunningTimes(counter, candidates, cancellationToken);
                await Task.Delay(100, cancellationToken).ConfigureAwait(false);
                if (!CollectGpuData(query))
                {
                    return candidates.Keys.Select(pid => new GpuProcessSample(pid, 0)).ToArray();
                }
                var second = ReadGpuRunningTimes(counter, candidates, cancellationToken);
                var totals = new Dictionary<int, double>();
                foreach (var entry in second)
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    // A new engine instance has no baseline; do not use its lifetime total.
                    if (first.TryGetValue(entry.Key, out var previous))
                    {
                        var delta = entry.Value.RunningTime - previous.RunningTime;
                        if (double.IsFinite(delta) && delta > 0)
                        {
                            totals[entry.Value.Pid] = totals.GetValueOrDefault(entry.Value.Pid) + delta;
                        }
                    }
                }

                return candidates.Keys.Select(pid => new GpuProcessSample(pid, totals.GetValueOrDefault(pid))).ToArray();
            }
            finally
            {
                PdhCloseQuery(query);
            }
        }, cancellationToken);
    }

    public static void SetWindowIdentity(nint hwnd, string mutexSuffix)
    {
        if (hwnd == 0)
        {
            throw new ArgumentException("A window handle is required.", nameof(hwnd));
        }
        ClearWindowIdentity(hwnd);
        var atom = GlobalAddAtomW(string.IsNullOrEmpty(mutexSuffix) ? StartupOptions.DefaultMutexSuffix : mutexSuffix);
        if (atom == 0)
        {
            throw new Win32Exception(Marshal.GetLastWin32Error(), "Could not create the PresentMon window identity.");
        }
        if (!SetPropW(hwnd, WindowIdentityProperty, (nint)atom))
        {
            var error = Marshal.GetLastWin32Error();
            GlobalDeleteAtom(atom);
            throw new Win32Exception(error, "Could not set the PresentMon window identity.");
        }
    }

    public static void ClearWindowIdentity(nint hwnd)
    {
        if (hwnd != 0)
        {
            var atom = RemovePropW(hwnd, WindowIdentityProperty);
            if (atom != 0)
            {
                GlobalDeleteAtom((ushort)atom);
            }
        }
    }

    private static HashSet<string> CopyBlocklist(IReadOnlySet<string>? blocklist) => blocklist is null
        ? new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        : new HashSet<string>(blocklist, StringComparer.OrdinalIgnoreCase);

    private static IReadOnlyList<ProcessEntry> EnumerateProcesses(
        HashSet<string> blocklist, CancellationToken cancellationToken, bool includeDisplayDetails = true)
    {
        cancellationToken.ThrowIfCancellationRequested();
        using var snapshot = CreateToolhelp32Snapshot(0x00000002, 0);
        if (snapshot.IsInvalid)
        {
            throw new Win32Exception(Marshal.GetLastWin32Error(), "Could not enumerate running processes.");
        }

        var processNames = new Dictionary<int, string>();
        var process = new NativeProcessEntry { Size = (uint)Marshal.SizeOf<NativeProcessEntry>() };
        if (!Process32FirstW(snapshot, ref process))
        {
            throw new Win32Exception(Marshal.GetLastWin32Error(), "Could not read the process snapshot.");
        }
        do
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (process.ProcessId != Environment.ProcessId && !blocklist.Contains(process.ExeFile))
            {
                processNames[(int)process.ProcessId] = process.ExeFile;
            }
        }
        while (Process32NextW(snapshot, ref process));
        var enumerationError = Marshal.GetLastWin32Error();
        if (enumerationError != 18)
        {
            throw new Win32Exception(enumerationError, "Could not finish reading the process snapshot.");
        }

        var windows = new Dictionary<int, nint>();
        EnumWindowsCallback callback = (hwnd, _) =>
        {
            if (GetWindow(hwnd, 4) == 0 && IsWindowVisible(hwnd))
            {
                GetWindowThreadProcessId(hwnd, out var pid);
                if (processNames.ContainsKey((int)pid))
                {
                    windows.TryAdd((int)pid, hwnd);
                }
            }
            return true;
        };
        if (!EnumWindows(callback, 0))
        {
            throw new Win32Exception(Marshal.GetLastWin32Error(), "Could not enumerate application windows.");
        }

        var results = new List<ProcessEntry>(windows.Count);
        foreach (var entry in windows)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var windowName = string.Empty;
            if (includeDisplayDetails)
            {
                var title = new StringBuilder(Math.Max(1, GetWindowTextLengthW(entry.Value) + 1));
                GetWindowTextW(entry.Value, title, title.Capacity);
                windowName = title.ToString();
            }
            results.Add(new ProcessEntry(entry.Key, processNames[entry.Key], windowName));
        }
        if (!includeDisplayDetails) return results;
        return results.OrderBy(entry => entry.Name, StringComparer.OrdinalIgnoreCase).ThenBy(entry => entry.Pid).ToArray();
    }

    private static bool CollectGpuData(nint query)
    {
        var status = PdhCollectQueryData(query);
        if (status == PdhNoData)
        {
            return false;
        }
        ThrowIfPdhFailed(status, "Collecting GPU performance data");
        return true;
    }

    private static Dictionary<string, GpuRunningTime> ReadGpuRunningTimes(
        nint counter, IReadOnlyDictionary<int, ProcessEntry> candidates, CancellationToken cancellationToken)
    {
        var results = new Dictionary<string, GpuRunningTime>(StringComparer.Ordinal);
        for (var attempt = 0; attempt < 3; attempt++)
        {
            cancellationToken.ThrowIfCancellationRequested();
            uint bufferSize = 0;
            var status = PdhGetFormattedCounterArrayW(counter, PdhFormatDouble, ref bufferSize, out _, 0);
            if (status == PdhNoData || status == PdhNoInstance || status == 0 && bufferSize == 0)
            {
                return results;
            }
            if (status != PdhMoreData)
            {
                ThrowIfPdhFailed(status, "Sizing GPU performance data");
            }
            var buffer = Marshal.AllocHGlobal(checked((int)bufferSize));
            try
            {
                status = PdhGetFormattedCounterArrayW(counter, PdhFormatDouble, ref bufferSize, out var count, buffer);
                if (status == PdhMoreData)
                {
                    continue;
                }
                if (status == PdhNoData || status == PdhNoInstance)
                {
                    return results;
                }
                ThrowIfPdhFailed(status, "Reading GPU performance data");
                var itemSize = Marshal.SizeOf<NativeCounterValueItem>();
                for (var index = 0; index < count; index++)
                {
                    var item = Marshal.PtrToStructure<NativeCounterValueItem>(buffer + checked(index * itemSize));
                    if (item.Value.Status > 1 || !double.IsFinite(item.Value.DoubleValue))
                    {
                        continue;
                    }
                    var name = Marshal.PtrToStringUni(item.Name);
                    if (name is not null && TryGet3DProcessId(name, out var pid) && candidates.ContainsKey(pid))
                    {
                        results[name] = new GpuRunningTime(pid, item.Value.DoubleValue);
                    }
                }
                return results;
            }
            finally
            {
                Marshal.FreeHGlobal(buffer);
            }
        }
        throw new InvalidOperationException("GPU performance counters changed repeatedly during enumeration. Please retry.");
    }

    private static bool TryGet3DProcessId(string instanceName, out int pid)
    {
        pid = 0;
        if (!instanceName.StartsWith("pid_", StringComparison.Ordinal)
            || !instanceName.EndsWith("_engtype_3D", StringComparison.Ordinal))
        {
            return false;
        }
        var end = instanceName.IndexOf('_', 4);
        return end > 4 && int.TryParse(instanceName.AsSpan(4, end - 4), out pid);
    }

    private static void ThrowIfPdhFailed(uint status, string operation)
    {
        if (status != 0)
        {
            throw new Win32Exception(unchecked((int)status), $"{operation} failed (PDH 0x{status:X8}).");
        }
    }

    private readonly record struct GpuRunningTime(int Pid, double RunningTime);

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct NativeProcessEntry
    {
        public uint Size;
        public uint Usage;
        public uint ProcessId;
        public nuint DefaultHeapId;
        public uint ModuleId;
        public uint Threads;
        public uint ParentProcessId;
        public int PriorityClassBase;
        public uint Flags;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
        public string ExeFile;
    }

    [StructLayout(LayoutKind.Explicit, Size = 16)]
    private struct NativeCounterValue
    {
        [FieldOffset(0)] public uint Status;
        [FieldOffset(8)] public double DoubleValue;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct NativeCounterValueItem
    {
        public nint Name;
        public NativeCounterValue Value;
    }

    private delegate bool EnumWindowsCallback(nint hwnd, nint parameter);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern SafeFileHandle CreateToolhelp32Snapshot(uint flags, uint processId);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool Process32FirstW(SafeFileHandle snapshot, ref NativeProcessEntry entry);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool Process32NextW(SafeFileHandle snapshot, ref NativeProcessEntry entry);
    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumWindows(EnumWindowsCallback callback, nint parameter);
    [DllImport("user32.dll")]
    private static extern nint GetWindow(nint hwnd, uint command);
    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWindowVisible(nint hwnd);
    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(nint hwnd, out uint processId);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowTextLengthW(nint hwnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetWindowTextW(nint hwnd, StringBuilder text, int maxCount);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern ushort GlobalAddAtomW(string value);
    [DllImport("kernel32.dll")]
    private static extern ushort GlobalDeleteAtom(ushort atom);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetPropW(nint hwnd, string name, nint value);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern nint RemovePropW(nint hwnd, string name);
    [DllImport("pdh.dll", CharSet = CharSet.Unicode)]
    private static extern uint PdhOpenQueryW(string? dataSource, nuint userData, out nint query);
    [DllImport("pdh.dll", CharSet = CharSet.Unicode)]
    private static extern uint PdhAddEnglishCounterW(nint query, string path, nuint userData, out nint counter);
    [DllImport("pdh.dll")]
    private static extern uint PdhCollectQueryData(nint query);
    [DllImport("pdh.dll", CharSet = CharSet.Unicode)]
    private static extern uint PdhGetFormattedCounterArrayW(nint counter, uint format, ref uint bufferSize, out uint itemCount, nint buffer);
    [DllImport("pdh.dll")]
    private static extern uint PdhCloseQuery(nint query);
}
