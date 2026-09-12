namespace PresentMon.UI.Core;

public sealed record GpuProcessSample(int Pid, double RunningTime);

public static class AutomaticTargeting
{
    public static readonly TimeSpan Interval = TimeSpan.FromSeconds(1);

    // The caller supplies a revision check to discard scans superseded by user input.
    public static async Task UpdateAsync(
        Func<Task<IReadOnlyList<GpuProcessSample>>> sample,
        Func<int[], Task<IReadOnlyList<int>>> probeGpuBusy,
        Func<bool> isCurrent,
        Func<int?, Task> select)
    {
        var samples = await sample();
        if (!isCurrent()) return;
        // Idle/invalid loads cannot win; avoid starting frame tracking for them.
        var active = samples.Where(s => double.IsFinite(s.RunningTime) && s.RunningTime > 0).ToArray();
        // An empty request also releases trackers left over from the previous scan.
        var measurable = (await probeGpuBusy(active.Select(s => s.Pid).Distinct().ToArray())).ToHashSet();
        if (!isCurrent()) return;
        var top = active.Where(s => measurable.Contains(s.Pid))
            .OrderByDescending(s => s.RunningTime).ThenBy(s => s.Pid).FirstOrDefault();
        // Eligibility controls switching; an empty result preserves the current target and capture.
        if (top is not null) await select(top.Pid);
    }
}
