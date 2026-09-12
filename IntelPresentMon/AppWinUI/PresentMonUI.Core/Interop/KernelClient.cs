using System.Buffers.Binary;
using System.IO.Pipes;
using System.Threading.Channels;

namespace PresentMon.UI.Core.Interop;

public sealed class KernelClient : IAsyncDisposable
{
    private readonly NamedPipeClientStream requestPipe;
    private readonly NamedPipeClientStream eventPipe;
    private readonly SemaphoreSlim requestGate = new(1);
    private readonly CancellationTokenSource lifetime = new();
    private readonly Channel<KernelEvent> events = Channel.CreateBounded<KernelEvent>(new BoundedChannelOptions(256)
    {
        SingleReader = true,
        SingleWriter = true,
        FullMode = BoundedChannelFullMode.Wait,
    });
    private Task eventReaderTask = Task.CompletedTask;
    private uint nextToken;
    private int disposed;
    private Exception? terminalError;

    private KernelClient(string pipeBaseName)
    {
        const string prefix = @"\\.\pipe\";
        var name = pipeBaseName.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)
            ? pipeBaseName[prefix.Length..] : pipeBaseName;
        if (string.IsNullOrWhiteSpace(name) || name.Contains('\\') || name.Contains('/'))
            throw new ArgumentException("Expected a local kernel pipe base name.", nameof(pipeBaseName));
        requestPipe = new NamedPipeClientStream(".", name + "-in", PipeDirection.InOut, PipeOptions.Asynchronous);
        eventPipe = new NamedPipeClientStream(".", name + "-out", PipeDirection.InOut, PipeOptions.Asynchronous);
    }

    public KernelSessionInfo Session { get; private set; } = null!;
    public bool IsConnected => disposed == 0 && !lifetime.IsCancellationRequested && requestPipe.IsConnected;

    public static async Task<KernelClient> ConnectAsync(string pipeBaseName, CancellationToken cancellationToken = default)
    {
        var client = new KernelClient(pipeBaseName);
        try
        {
            using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            timeout.CancelAfter(TimeSpan.FromSeconds(10));
            await Task.WhenAll(client.requestPipe.ConnectAsync(timeout.Token), client.eventPipe.ConnectAsync(timeout.Token))
                .ConfigureAwait(false);
            client.Session = await client.OpenSessionAsync((uint)Environment.ProcessId, timeout.Token).ConfigureAwait(false);
            client.eventReaderTask = client.ReadEventsLoopAsync();
            return client;
        }
        catch
        {
            await client.DisposeAsync().ConfigureAwait(false);
            throw;
        }
    }

    // Events are buffered from startup so callers cannot miss initialization errors.
    // Enumerate once from the application session and marshal updates to the UI thread.
    public IAsyncEnumerable<KernelEvent> ReadEventsAsync(CancellationToken cancellationToken = default) =>
        events.Reader.ReadAllAsync(cancellationToken);

    public Task<KernelSessionInfo> OpenSessionAsync(uint processId, CancellationToken cancellationToken = default) =>
        RequestAsync("OpenSession", writer => writer.UInt32(processId), KernelProtocol.Session, cancellationToken);

    public Task<IntrospectionData> IntrospectAsync(CancellationToken cancellationToken = default) =>
        RequestAsync("Introspect", _ => { }, KernelProtocol.Introspection, cancellationToken);

    public Task BindHotkeyAsync(HotkeyBinding binding, CancellationToken cancellationToken = default)
    {
        if (binding.Combination is null)
            return ClearHotkeyAsync((int)binding.Action, cancellationToken);
        var key = checked((uint)binding.Combination.Key);
        var modifiers = binding.Combination.Modifiers.Select(value => checked((uint)value)).ToArray();
        var action = (int)binding.Action;
        return EmptyRequestAsync("BindHotkey", writer =>
        {
            writer.UInt32(key);
            writer.List(modifiers, writer.UInt32);
            writer.Int32(action);
        }, cancellationToken);
    }

    public Task ClearHotkeyAsync(int action, CancellationToken cancellationToken = default) =>
        EmptyRequestAsync("ClearHotkey", writer => writer.Int32(action), cancellationToken);

    public Task PushSpecificationAsync(Specification specification, CancellationToken cancellationToken = default)
    {
        // Snapshot mutable UI models before the first await.
        using var writer = new CerealWriter();
        KernelProtocol.Specification(writer, specification);
        var payload = writer.ToArray();
        return EmptyRequestAsync("PushSpecification", writer => writer.Bytes(payload), cancellationToken);
    }

    public Task SetCaptureAsync(bool active, CancellationToken cancellationToken = default) =>
        EmptyRequestAsync("SetCapture", writer => writer.Bool(active), cancellationToken);

    public async Task<IReadOnlyList<int>> ProbeGpuBusyAsync(int[] pids, CancellationToken cancellationToken = default) =>
        await RequestAsync("ProbeGpuBusy", writer => writer.List(pids, pid => writer.UInt32(checked((uint)pid))),
            reader => reader.List(() => checked((int)reader.UInt32()), sizeof(uint)), cancellationToken).ConfigureAwait(false);

    public Task SetEtlLoggingAsync(bool active, CancellationToken cancellationToken = default) =>
        EmptyRequestAsync("SetEtlLogging", writer => writer.Bool(active), cancellationToken);

    private async Task EmptyRequestAsync(string identifier, Action<CerealWriter> write, CancellationToken cancellationToken) =>
        _ = await RequestAsync(identifier, write, _ => true, cancellationToken).ConfigureAwait(false);

    private async Task<T> RequestAsync<T>(string identifier, Action<CerealWriter> write, Func<CerealReader, T> read,
        CancellationToken cancellationToken)
    {
        ObjectDisposedException.ThrowIf(disposed != 0, this);
        await requestGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            ObjectDisposedException.ThrowIf(disposed != 0, this);
            if (lifetime.IsCancellationRequested)
                throw new IOException("The kernel connection is closed.", terminalError);
            var token = nextToken++;
            var packet = KernelProtocol.Request(identifier, token, write);
            using var requestCancellation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, lifetime.Token);
            requestCancellation.CancelAfter(TimeSpan.FromSeconds(30));
            try
            {
                await requestPipe.WriteAsync(packet, requestCancellation.Token).ConfigureAwait(false);
                var reader = new CerealReader(await ReadPacketAsync(requestPipe, requestCancellation.Token).ConfigureAwait(false));
                var header = KernelProtocol.Header(reader);
                KernelProtocol.ValidateResponse(header, identifier, token);
                if (header.TransportStatus != 0)
                {
                    reader.RequireEnd();
                    throw new KernelRequestException(identifier, header.TransportStatus, header.ExecutionStatus);
                }
                var result = read(reader);
                reader.RequireEnd();
                return result;
            }
            catch (KernelRequestException) { throw; }
            catch (Exception exception)
            {
                // A canceled or malformed exchange cannot be safely reused: its reply
                // could otherwise be consumed by the next request.
                FailConnection(exception);
                throw;
            }
        }
        finally { requestGate.Release(); }
    }

    internal static async Task<byte[]> ReadPacketAsync(Stream stream, CancellationToken cancellationToken)
    {
        var prefix = new byte[sizeof(uint)];
        await stream.ReadExactlyAsync(prefix, cancellationToken).ConfigureAwait(false);
        var size = BinaryPrimitives.ReadUInt32LittleEndian(prefix);
        if (size < KernelProtocol.MinimumPacketBytes || size > KernelProtocol.MaximumPacketBytes)
            throw new InvalidDataException($"Invalid kernel packet length: {size}.");
        var body = new byte[(int)size];
        await stream.ReadExactlyAsync(body, cancellationToken).ConfigureAwait(false);
        return body;
    }

    private async Task ReadEventsLoopAsync()
    {
        try
        {
            while (!lifetime.IsCancellationRequested)
            {
                var reader = new CerealReader(await ReadPacketAsync(eventPipe, lifetime.Token).ConfigureAwait(false));
                var message = KernelProtocol.Event(KernelProtocol.Header(reader), reader);
                await events.Writer.WriteAsync(message, lifetime.Token).ConfigureAwait(false);
            }
        }
        catch (OperationCanceledException) when (lifetime.IsCancellationRequested) { }
        catch (Exception) when (lifetime.IsCancellationRequested) { }
        catch (Exception exception) { FailConnection(exception); }
        finally { events.Writer.TryComplete(terminalError); }
    }

    private void FailConnection(Exception exception)
    {
        Interlocked.CompareExchange(ref terminalError, exception, null);
        lifetime.Cancel();
        requestPipe.Dispose();
        eventPipe.Dispose();
        events.Writer.TryComplete(terminalError);
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref disposed, 1) != 0)
            return;
        lifetime.Cancel();
        await requestPipe.DisposeAsync().ConfigureAwait(false);
        await eventPipe.DisposeAsync().ConfigureAwait(false);
        await eventReaderTask.ConfigureAwait(false);
        events.Writer.TryComplete();
        await requestGate.WaitAsync().ConfigureAwait(false);
        requestGate.Release();
        lifetime.Dispose();
    }
}
