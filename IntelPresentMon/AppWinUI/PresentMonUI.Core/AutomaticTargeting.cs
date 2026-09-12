namespace PresentMon.UI.Core;

public sealed record GpuProcessSample(int Pid, double RunningTime);

public static class AutomaticTargeting
{
    public static readonly TimeSpan Interval = TimeSpan.FromSeconds(1);

    // The caller supplies a revision check to discard scans superseded by user input.
    public static async Task UpdateAsync(
        Func<Task<IReadOnlyList<GpuProcessSample>>> sample,
        Func<int[], Task<IReadOnlyList<int>>> probeFps,
        Func<bool> isCurrent,
        Func<int?, Task> select)
    {
        var samples = await sample();
        if (!isCurrent()) return;
        var measurable = (await probeFps(samples.Select(s => s.Pid).Distinct().ToArray())).ToHashSet();
        if (!isCurrent()) return;
        var top = samples.Where(s => measurable.Contains(s.Pid) && double.IsFinite(s.RunningTime) && s.RunningTime > 0)
            .OrderByDescending(s => s.RunningTime).ThenBy(s => s.Pid).FirstOrDefault();
        await select(top?.Pid);
    }
}
