using System.Buffers.Binary;
using System.IO.Pipes;
using System.Text;
using PresentMon.UI.Core;
using PresentMon.UI.Core.Interop;

public static class KernelProtocolTests
{
    public static async Task<int> RunAsync()
    {
        PrimitiveContracts();
        await PacketFramingAsync();
        await CommandsAndEventsAsync();
        await InvalidReplyClosesConnectionAsync();
        await CancellationClosesConnectionAsync();
        await DisposeInterruptsRequestAsync();
        await ExecutionFailureAllowsNextRequestAsync();
        await ConcurrentRequestsAreSerializedAsync();
        await IncompatibleIntrospectionClosesConnectionAsync();
        return 9 + await NativeProtocolFixtureTests.RunAsync();
    }

    private static void PrimitiveContracts()
    {
        using var writer = new CerealWriter();
        writer.OptionalUInt32(null);
        writer.OptionalUInt32(0x01020304);
        writer.OptionalInt32(-2);
        Equal("01000403020100FEFFFFFF", Convert.ToHexString(writer.ToArray()), "cereal optional uses nullopt marker");

        Throws<InvalidDataException>(() => new CerealReader(new byte[] { 2 }).Bool());
        Throws<InvalidDataException>(() => new CerealReader(new byte[3]).UInt32());
        Throws<InvalidDataException>(() => new CerealReader(Enumerable.Repeat((byte)255, 8).ToArray()).String());
        Throws<InvalidDataException>(() => new CerealReader(Enumerable.Repeat((byte)255, 8).ToArray()).List(() => 1));
        Throws<InvalidDataException>(() => new CerealReader(new byte[] { 0 }).RequireEnd());
        Throws<InvalidDataException>(() => KernelProtocol.ValidateResponse(
            new("SetCapture", 3, 0, 0, 1, 1, 1), "SetCapture", 3));
        Throws<InvalidDataException>(() => KernelProtocol.ValidateResponse(
            new("SetCapture", 4, 0, 0, 0, 1, 1), "SetCapture", 3));

        var invalidSpecification = new Specification { Preferences = new() { CaptureDelay = double.NaN } };
        Throws<ArgumentOutOfRangeException>(() => KernelProtocol.Specification(writer, invalidSpecification));

        using var fractionalWriter = new CerealWriter();
        KernelProtocol.Specification(fractionalWriter, new() { Preferences = new() { CaptureDelay = 12.5 } });
        var fractionalReader = new CerealReader(fractionalWriter.ToArray());
        Equal(true, fractionalReader.Bool(), "null target marker");
        Equal("", fractionalReader.String(), "capture path");
        Equal(12u, fractionalReader.UInt32(), "fractional integer setting matches the CEF bridge");
    }

    private static async Task PacketFramingAsync()
    {
        var packet = KernelProtocol.Request("SetCapture", 17, writer => writer.Bool(true));
        await using var fragmented = new FragmentedStream(packet);
        var body = await KernelClient.ReadPacketAsync(fragmented, default);
        var reader = new CerealReader(body);
        var header = KernelProtocol.Header(reader);
        Equal("SetCapture", header.Identifier, "fragmented packet identifier");
        Equal(17u, header.CommandToken, "fragmented packet token");
        Equal(true, reader.Bool(), "fragmented packet payload");
        reader.RequireEnd();

        await ThrowsAsync<InvalidDataException>(() => KernelClient.ReadPacketAsync(new MemoryStream(new byte[] { 255, 255, 255, 127 }), default));
        await ThrowsAsync<InvalidDataException>(() => KernelClient.ReadPacketAsync(new MemoryStream(new byte[] { 0, 0, 0, 0 }), default));
        await ThrowsAsync<EndOfStreamException>(() => KernelClient.ReadPacketAsync(new MemoryStream(packet[..^1]), default));
    }

    private static async Task CommandsAndEventsAsync()
    {
        await using var server = new TestServer();
        var worker = Task.Run(async () =>
        {
            await server.AcceptAsync();
            await server.HandshakeAsync();
            var capture = await server.RequestAsync("SetCapture");
            Equal(true, capture.Payload.Bool(), "capture request");
            capture.Payload.RequireEnd();
            await server.ReplyAsync(capture);

            var bind = await server.RequestAsync("BindHotkey");
            Equal(42u, bind.Payload.UInt32(), "hotkey uses project key code");
            var modifiers = bind.Payload.List(bind.Payload.UInt32, 4);
            Equal("2,4", string.Join(',', modifiers), "hotkey modifiers");
            Equal(0, bind.Payload.Int32(), "Vue hotkey action ID");
            bind.Payload.RequireEnd();
            await server.ReplyAsync(bind);

            var clear = await server.RequestAsync("ClearHotkey");
            Equal(2, clear.Payload.Int32(), "clear hotkey action");
            clear.Payload.RequireEnd();
            await server.ReplyAsync(clear);

            var etl = await server.RequestAsync("SetEtlLogging");
            Equal(false, etl.Payload.Bool(), "ETL request");
            etl.Payload.RequireEnd();
            await server.ReplyAsync(etl);
            await server.EventAsync("HotkeyFiredAction", writer => writer.Write(1));
            await server.EventAsync("TargetLostAction", writer => writer.Write(456u));
            await server.EventAsync("PresentmonInitFailedAction");
            await server.EventAsync("OverlayDiedAction");
            await server.EventAsync("StalePidAction");
        });

        await using var client = await KernelClient.ConnectAsync(server.Name);
        Equal(123u, client.Session.KernelPid, "session PID");
        Equal("service-build", client.Session.ServiceBuildId, "session build");
        await client.SetCaptureAsync(true);
        await client.BindHotkeyAsync(new() { Action = HotkeyAction.ToggleCapture, Combination = new() { Key = 42, Modifiers = [2, 4] } });
        await client.ClearHotkeyAsync(2);
        await client.SetEtlLoggingAsync(false);
        await worker.WaitAsync(TimeSpan.FromSeconds(5));

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        await using var enumerator = client.ReadEventsAsync(timeout.Token).GetAsyncEnumerator();
        foreach (var kind in Enum.GetValues<KernelEventKind>())
        {
            Equal(true, await enumerator.MoveNextAsync(), "event received");
            Equal(kind, enumerator.Current.Kind, "event kind");
            if (kind == KernelEventKind.HotkeyFired) Equal(1, enumerator.Current.ActionId, "hotkey event payload");
            if (kind == KernelEventKind.TargetLost) Equal(456u, enumerator.Current.ProcessId, "target event payload");
        }
    }

    private static async Task InvalidReplyClosesConnectionAsync()
    {
        await using var server = new TestServer();
        var worker = Task.Run(async () =>
        {
            await server.AcceptAsync();
            await server.HandshakeAsync();
            var request = await server.RequestAsync("SetCapture");
            await server.ReplyAsync(request, mutateHeader: bytes =>
                BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(8 + "SetCapture".Length), request.Header.CommandToken + 1));
        });
        await using var client = await KernelClient.ConnectAsync(server.Name);
        await ThrowsAsync<InvalidDataException>(() => client.SetCaptureAsync(true));
        Equal(false, client.IsConnected, "mismatched reply closes connection");
        await ThrowsAsync<IOException>(() => client.SetCaptureAsync(false));
        await worker;
    }

    private static async Task CancellationClosesConnectionAsync()
    {
        await using var server = new TestServer();
        var requested = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var worker = Task.Run(async () =>
        {
            await server.AcceptAsync();
            await server.HandshakeAsync();
            await server.RequestAsync("SetCapture");
            requested.SetResult();
        });
        await using var client = await KernelClient.ConnectAsync(server.Name);
        using var cancellation = new CancellationTokenSource();
        var request = client.SetCaptureAsync(true, cancellation.Token);
        await requested.Task.WaitAsync(TimeSpan.FromSeconds(5));
        cancellation.Cancel();
        await ThrowsAsync<OperationCanceledException>(() => request);
        Equal(false, client.IsConnected, "canceled request cannot leave a reusable reply stream");
        await worker;
    }

    private static async Task DisposeInterruptsRequestAsync()
    {
        await using var server = new TestServer();
        var requested = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var worker = Task.Run(async () =>
        {
            await server.AcceptAsync();
            await server.HandshakeAsync();
            await server.RequestAsync("SetCapture");
            requested.SetResult();
        });
        var client = await KernelClient.ConnectAsync(server.Name);
        var request = client.SetCaptureAsync(true);
        await requested.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await client.DisposeAsync().AsTask().WaitAsync(TimeSpan.FromSeconds(5));
        try { await request; throw new Exception("Disposed request unexpectedly succeeded."); }
        catch (Exception exception) when (exception is OperationCanceledException or IOException or ObjectDisposedException) { }
        await client.DisposeAsync();
        await worker;
    }

    private static async Task ExecutionFailureAllowsNextRequestAsync()
    {
        await using var server = new TestServer();
        var worker = Task.Run(async () =>
        {
            await server.AcceptAsync();
            await server.HandshakeAsync();
            var request = await server.RequestAsync("SetCapture");
            await server.ReplyAsync(request, mutateHeader: bytes =>
            {
                var offset = 8 + "SetCapture".Length + sizeof(uint);
                BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(offset), 1);
                BinaryPrimitives.WriteInt32LittleEndian(bytes.AsSpan(offset + 4), 7);
            });
            await server.ReplyAsync(await server.RequestAsync("SetCapture"));
        });
        await using var client = await KernelClient.ConnectAsync(server.Name);
        await ThrowsAsync<KernelRequestException>(() => client.SetCaptureAsync(true));
        await client.SetCaptureAsync(false);
        Equal(true, client.IsConnected, "valid execution error preserves framing");
        await worker;
    }

    private static async Task ConcurrentRequestsAreSerializedAsync()
    {
        await using var server = new TestServer();
        var worker = Task.Run(async () =>
        {
            await server.AcceptAsync();
            await server.HandshakeAsync();
            var first = await server.RequestAsync("SetCapture");
            Equal(true, first.Payload.Bool(), "first concurrent request");
            await server.ReplyAsync(first);
            var second = await server.RequestAsync("SetCapture");
            Equal(false, second.Payload.Bool(), "second concurrent request");
            Equal(first.Header.CommandToken + 1, second.Header.CommandToken, "sequential request tokens");
            await server.ReplyAsync(second);
        });
        await using var client = await KernelClient.ConnectAsync(server.Name);
        var first = client.SetCaptureAsync(true);
        var second = client.SetCaptureAsync(false);
        await Task.WhenAll(first, second).WaitAsync(TimeSpan.FromSeconds(5));
        await worker;
    }

    private static async Task IncompatibleIntrospectionClosesConnectionAsync()
    {
        await using var server = new TestServer();
        var worker = Task.Run(async () =>
        {
            await server.AcceptAsync();
            await server.HandshakeAsync();
            var request = await server.RequestAsync("Introspect");
            await server.ReplyAsync(request, writer =>
            {
                writer.Write(0UL);
                writer.Write(0UL);
                writer.Write(0UL);
                writer.Write(0UL);
                writer.Write(65536u);
                // A payload missing defaultAdapterId must fail, even though its
                // actionVersion still says 1. It must never be guessed or retried.
                writer.Write(0UL);
            });
        });
        await using var client = await KernelClient.ConnectAsync(server.Name);
        await ThrowsAsync<InvalidDataException>(() => client.IntrospectAsync());
        Equal(false, client.IsConnected, "incompatible introspection closes the session");
        await worker;
    }

    private static void Equal<T>(T expected, T actual, string description)
    {
        if (!EqualityComparer<T>.Default.Equals(expected, actual))
            throw new Exception($"{description}: expected {expected}, got {actual}.");
    }

    private static void Throws<T>(Action action) where T : Exception
    {
        try { action(); }
        catch (T) { return; }
        throw new Exception($"Expected {typeof(T).Name}.");
    }

    private static async Task ThrowsAsync<T>(Func<Task> action) where T : Exception
    {
        try { await action().WaitAsync(TimeSpan.FromSeconds(5)); }
        catch (T) { return; }
        throw new Exception($"Expected {typeof(T).Name}.");
    }

    private sealed class FragmentedStream(byte[] bytes) : MemoryStream(bytes)
    {
        public override ValueTask<int> ReadAsync(Memory<byte> buffer, CancellationToken cancellationToken = default) =>
            base.ReadAsync(buffer[..Math.Min(3, buffer.Length)], cancellationToken);
    }

    private sealed record ReceivedRequest(byte[] Body, KernelPacketHeader Header, CerealReader Payload);

    private sealed class TestServer : IAsyncDisposable
    {
        public string Name { get; } = "pm-winui-test-" + Guid.NewGuid().ToString("N");
        private readonly NamedPipeServerStream requests;
        private readonly NamedPipeServerStream eventOutput;

        public TestServer()
        {
            requests = new(Name + "-in", PipeDirection.InOut, 1, PipeTransmissionMode.Byte, PipeOptions.Asynchronous);
            eventOutput = new(Name + "-out", PipeDirection.InOut, 1, PipeTransmissionMode.Byte, PipeOptions.Asynchronous);
        }

        public Task AcceptAsync() => Task.WhenAll(requests.WaitForConnectionAsync(), eventOutput.WaitForConnectionAsync());

        public async Task<ReceivedRequest> RequestAsync(string identifier)
        {
            var body = await KernelClient.ReadPacketAsync(requests, default);
            var reader = new CerealReader(body);
            var header = KernelProtocol.Header(reader);
            Equal(identifier, header.Identifier, "server request name");
            return new(body, header, reader);
        }

        public async Task HandshakeAsync()
        {
            var request = await RequestAsync("OpenSession");
            Equal((uint)Environment.ProcessId, request.Payload.UInt32(), "UI session process");
            request.Payload.RequireEnd();
            await ReplyAsync(request, writer =>
            {
                writer.Write(123u);
                WriteString(writer, "service-build");
                WriteString(writer, "service-time");
                WriteString(writer, "service-version");
                WriteString(writer, "api-version");
            });
        }

        public async Task ReplyAsync(ReceivedRequest request, Action<BinaryWriter>? payload = null, Action<byte[]>? mutateHeader = null)
        {
            // Echo exactly the native response header semantics, without the production encoder.
            var headerLength = 28 + Encoding.UTF8.GetByteCount(request.Header.Identifier);
            var header = request.Body[..headerLength];
            mutateHeader?.Invoke(header);
            using var body = new MemoryStream();
            body.Write(header);
            using (var writer = new BinaryWriter(body, Encoding.UTF8, leaveOpen: true)) payload?.Invoke(writer);
            await SendAsync(requests, body.ToArray());
        }

        public async Task EventAsync(string identifier, Action<BinaryWriter>? payload = null)
        {
            using var body = new MemoryStream();
            using (var writer = new BinaryWriter(body, Encoding.UTF8, leaveOpen: true))
            {
                WriteString(writer, identifier);
                writer.Write(0u);
                writer.Write(0);
                writer.Write(0);
                writer.Write(2);
                writer.Write((ushort)1);
                writer.Write((ushort)1);
                payload?.Invoke(writer);
            }
            await SendAsync(eventOutput, body.ToArray());
        }

        private static void WriteString(BinaryWriter writer, string value)
        {
            var bytes = Encoding.UTF8.GetBytes(value);
            writer.Write((ulong)bytes.Length);
            writer.Write(bytes);
        }

        private static async Task SendAsync(Stream stream, byte[] body)
        {
            var prefix = new byte[4];
            BinaryPrimitives.WriteUInt32LittleEndian(prefix, (uint)body.Length);
            await stream.WriteAsync(prefix);
            await stream.WriteAsync(body);
        }

        public async ValueTask DisposeAsync()
        {
            await requests.DisposeAsync();
            await eventOutput.DisposeAsync();
        }
    }
}
