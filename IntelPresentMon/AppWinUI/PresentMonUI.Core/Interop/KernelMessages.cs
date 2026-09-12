namespace PresentMon.UI.Core.Interop;

public sealed record KernelSessionInfo(
    uint KernelPid,
    string ServiceBuildId,
    string ServiceBuildTime,
    string ServiceVersion,
    string MiddlewareApiVersion);

public enum KernelEventKind
{
    HotkeyFired,
    TargetLost,
    PresentmonInitFailed,
    OverlayDied,
    StalePid,
}

public sealed record KernelEvent(KernelEventKind Kind, int? ActionId = null, uint? ProcessId = null);

public sealed class KernelRequestException(string identifier, int transportStatus, int executionStatus)
    : IOException($"Kernel request {identifier} failed (transport {transportStatus}, execution {executionStatus}).")
{
    public string Identifier { get; } = identifier;
    public int TransportStatus { get; } = transportStatus;
    public int ExecutionStatus { get; } = executionStatus;
}
