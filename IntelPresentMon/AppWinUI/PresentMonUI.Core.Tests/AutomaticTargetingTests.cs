using PresentMon.UI.Core;

public static class AutomaticTargetingTests
{
    public static async Task<int> RunAsync()
    {
        await FollowGamesAsync();
        await DiscardSupersededScansAsync();
        await RejectInvalidLoadsAsync();
        await ReleaseIdleCandidatesAsync();
        await PreserveSelectionWithoutEligibleCandidatesAsync();
        return 5;
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
        Assert(selected == gameA, "B with constant GPU Busy must not block an already running game.");
        // AppSession handles process exit independently of automatic candidate selection.
        selected = null;
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
        Assert(selected == gameC, "A must lose eligibility when its last ten GPU Busy samples become constant.");
        Assert(changes.Count == 5, "Only scans with an eligible candidate may request selection.");
    }

    private static async Task DiscardSupersededScansAsync()
    {
        foreach (var stage in new[] { "gpu", "raw" })
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
            if (stage == "raw") revision++;
            probe.SetResult([10]);
            await pending;
            Assert(selectionCalls == 0, "OFF, OFF/ON, or manual selection must invalidate an in-flight scan.");
            if (stage == "gpu") Assert(probeCalls == 0, "A stale GPU scan must not start raw GPU Busy probing.");
        }
    }

    private static async Task RejectInvalidLoadsAsync()
    {
        int? selected = null;
        await AutomaticTargeting.UpdateAsync(
            () => Task.FromResult<IReadOnlyList<GpuProcessSample>>(
                [new(1, double.NaN), new(2, double.PositiveInfinity), new(3, -1), new(4, 0), new(6, 50), new(5, 50)]),
            pids =>
            {
                Assert(pids.ToHashSet().SetEquals([5, 6]), "Invalid/inactive loads must not start frame tracking.");
                return Task.FromResult<IReadOnlyList<int>>(pids);
            }, () => true,
            pid => { selected = pid; return Task.CompletedTask; });
        Assert(selected == 5, "Reject invalid/inactive loads and break ties deterministically by PID.");
    }

    private static async Task ReleaseIdleCandidatesAsync()
    {
        int? selected = null;
        var samples = Enumerable.Range(100, 1000).Select(pid => new GpuProcessSample(pid, 0)).ToList();
        samples.AddRange([new(10, 50), new(20, 100)]);
        var requests = new List<int[]>();
        async Task ScanAsync()
        {
            await AutomaticTargeting.UpdateAsync(() => Task.FromResult<IReadOnlyList<GpuProcessSample>>(samples),
                pids => { requests.Add(pids); return Task.FromResult<IReadOnlyList<int>>(pids); }, () => true,
                pid => { selected = pid; return Task.CompletedTask; });
        }

        await ScanAsync();
        Assert(requests[0].ToHashSet().SetEquals([10, 20]) && selected == 20,
            "Idle applications must not increase the number of frame trackers.");
        samples = [new(10, 0), new(20, 0)];
        await ScanAsync();
        Assert(requests[1].Length == 0 && selected == 20,
            "An idle scan must release probe tracking with an empty request while retaining the target.");
        samples = [new(10, 50), new(20, 0)];
        await ScanAsync();
        Assert(requests[2].SequenceEqual([10]) && selected == 10,
            "A resumed application must be offered to the probe again.");
    }

    private static async Task PreserveSelectionWithoutEligibleCandidatesAsync()
    {
        foreach (int? initialTarget in new int?[] { 10, null })
        {
            var selected = initialTarget;
            var selectionCalls = 0;
            Task SelectAsync(int? pid)
            {
                selectionCalls++;
                selected = pid;
                return Task.CompletedTask;
            }
            IReadOnlyList<GpuProcessSample>[] scans =
            [
                [new(10, 50), new(20, 100)],
                [new(10, 50), new(20, 100)],
                [new(10, 0), new(20, 0)],
                [],
            ];
            foreach (var samples in scans)
            {
                await AutomaticTargeting.UpdateAsync(() => Task.FromResult(samples),
                    _ => Task.FromResult<IReadOnlyList<int>>([]), () => true, SelectAsync);
                Assert(selected == initialTarget && selectionCalls == 0,
                    "No eligible candidate must preserve selection without invoking the capture-stop path.");
            }

            await AutomaticTargeting.UpdateAsync(
                () => Task.FromResult<IReadOnlyList<GpuProcessSample>>([new(10, 100), new(20, 50)]),
                _ => Task.FromResult<IReadOnlyList<int>>([20]), () => true, SelectAsync);
            Assert(selected == 20 && selectionCalls == 1,
                "A newly eligible process must still replace an ineligible retained target or fill an empty selection.");
        }
    }

    private static void Assert(bool value, string message)
    {
        if (!value) throw new Exception(message);
    }
}
