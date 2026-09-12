namespace PresentMon.UI.Core.Interop;

internal sealed record KernelPacketHeader(
    string Identifier, uint CommandToken, int TransportStatus, int ExecutionStatus,
    int PacketType, ushort HeaderVersion, ushort ActionVersion);

internal static partial class KernelProtocol
{
    public const int MaximumPacketBytes = 16 * 1024 * 1024;
    public const int MinimumPacketBytes = 28;

    public static byte[] Request(string identifier, uint commandToken, Action<CerealWriter> payload)
    {
        using var writer = new CerealWriter();
        writer.String(identifier);
        writer.UInt32(commandToken);
        writer.Int32(0);
        writer.Int32(0);
        writer.Int32(0);
        writer.UInt16(1);
        writer.UInt16(1);
        payload(writer);
        return writer.ToPacket();
    }

    public static KernelPacketHeader Header(CerealReader reader) => new(
        reader.String(), reader.UInt32(), reader.Int32(), reader.Int32(),
        reader.Int32(), reader.UInt16(), reader.UInt16());

    public static void ValidateResponse(KernelPacketHeader header, string identifier, uint token)
    {
        // Native MakeResponseHeader intentionally retains ActionRequest (0).
        if (header.Identifier != identifier || header.CommandToken != token || header.PacketType != 0 ||
            header.HeaderVersion != 1 || header.ActionVersion != 1)
            throw new InvalidDataException("Kernel response does not match the request or protocol version.");
        if (header.TransportStatus is < 0 or > 2 ||
            (header.TransportStatus == 0 && header.ExecutionStatus != 0))
            throw new InvalidDataException("Invalid status in kernel response.");
    }

    public static KernelSessionInfo Session(CerealReader reader) => new(
        reader.UInt32(), reader.String(), reader.String(), reader.String(), reader.String());

    public static KernelEvent Event(KernelPacketHeader header, CerealReader reader)
    {
        if (header.PacketType != 2 || header.HeaderVersion != 1 || header.ActionVersion != 1 ||
            header.TransportStatus != 0 || header.ExecutionStatus != 0)
            throw new InvalidDataException("Unsupported kernel event header.");
        var result = header.Identifier switch
        {
            "HotkeyFiredAction" => new KernelEvent(KernelEventKind.HotkeyFired, ActionId: reader.Int32()),
            "TargetLostAction" => new KernelEvent(KernelEventKind.TargetLost, ProcessId: reader.UInt32()),
            "PresentmonInitFailedAction" => new KernelEvent(KernelEventKind.PresentmonInitFailed),
            "OverlayDiedAction" => new KernelEvent(KernelEventKind.OverlayDied),
            "StalePidAction" => new KernelEvent(KernelEventKind.StalePid),
            _ => throw new InvalidDataException($"Unsupported kernel event: {header.Identifier}."),
        };
        reader.RequireEnd();
        return result;
    }
}
