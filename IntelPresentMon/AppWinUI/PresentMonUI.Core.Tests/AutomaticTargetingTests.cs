using PresentMon.UI.Core;

public static class AutomaticTargetingTests
{
    public static async Task<int> RunAsync()
    {
        await FollowGamesAsync();
        await DiscardSupersededScansAsync();
        await RejectInvalidLoadsAsync();
        return 3;
    }

    private static async Task FollowGamesAsync()
    {
        const int gameA = 10, appB = 20, gameC = 30, restartedA = 40;
        int? selected = appB;
        var changes = new List<int?>();
        var samples = new List<GpuProcessSample>();
        var measurable = new List<int>();
        async Task ScanAsync()
        {
            await AutomaticTargeting.UpdateAsync(() => Task.FromResult<IReadOnlyList<GpuProcessSample>>(samples),
                pids =>
                {
                    Assert(pids.ToHashSet().SetEquals(samples.Select(s => s.Pid)), "Probe every candidate, not just the GPU winner.");
                    return Task.FromResult<IReadOnlyList<int>>(measurable);
                }, () => true, pid => { selected = pid; changes.Add(pid); return Task.CompletedTask; });
        }

        samples = [new(appB, 100), new(gameA, 50)];
        measurable = [gameA];
        await ScanAsync();
        Assert(selected == gameA, "Unmeasurable B must not block an already running game.");
        samples = [new(appB, 100)];
        measurable = [];
        await ScanAsync();
        Assert(selected is null, "After A exits, stay unselected when only unmeasurable B remains.");
        samples.Add(new(restartedA, 50));
        measurable.Add(restartedA);
        await ScanAsync();
        Assert(selected == restartedA, "A must be reacquired with its new PID.");
        samples.Add(new(gameC, 75));
        measurable.Add(gameC);
        await ScanAsync();
        Assert(selected == gameC, "Higher-load C must replace live A.");
        samples = [new(restartedA, 90), new(gameC, 75)];
        await ScanAsync();
        Assert(selected == restartedA, "Load changes must be reevaluated without restarting either game.");
        measurable = [gameC];
        await ScanAsync();
        Assert(selected == gameC, "A must lose eligibility when its FPS is no longer measurable.");
        Assert(changes.Count == 6, "Every interval must evaluate its candidates.");
    }

    private static async Task DiscardSupersededScansAsync()
    {
        foreach (var stage in new[] { "gpu", "fps" })
        {
            var revision = 1;
            var probeCalls = 0;
            var selectionCalls = 0;
            var scan = new TaskCompletionSource<IReadOnlyList<GpuProcessSample>>();
            var probe = new TaskCompletionSource<IReadOnlyList<int>>();
            var pending = AutomaticTargeting.UpdateAsync(() => scan.Task,
                _ => { probeCalls++; return probe.Task; }, () => revision == 1,
                _ => { selectionCalls++; return Task.CompletedTask; });
            if (stage == "gpu") revision++;
            scan.SetResult([new(10, 20)]);
            if (stage == "fps") revision++;
            probe.SetResult([10]);
            await pending;
            Assert(selectionCalls == 0, "OFF, OFF/ON, or manual selection must invalidate an in-flight scan.");
            if (stage == "gpu") Assert(probeCalls == 0, "A stale GPU scan must not start FPS probing.");
        }
    }

    private static async Task RejectInvalidLoadsAsync()
    {
        int? selected = null;
        await AutomaticTargeting.UpdateAsync(
            () => Task.FromResult<IReadOnlyList<GpuProcessSample>>(
                [new(1, double.NaN), new(2, double.PositiveInfinity), new(3, -1), new(4, 0), new(6, 50), new(5, 50)]),
            pids => Task.FromResult<IReadOnlyList<int>>(pids), () => true,
            pid => { selected = pid; return Task.CompletedTask; });
        Assert(selected == 5, "Reject invalid/inactive loads and break ties deterministically by PID.");
    }

    private static void Assert(bool value, string message)
    {
        if (!value) throw new Exception(message);
    }
}
