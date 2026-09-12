using System.Buffers.Binary;
using System.Text;

namespace PresentMon.UI.Core.Interop;

// Matches the cereal Binary archives used by CommonUtilities/pipe/Pipe.h.
// The protocol is little endian on the supported Windows x64 platform.
internal sealed class CerealWriter : IDisposable
{
    private static readonly UTF8Encoding Utf8 = new(false, true);
    private readonly MemoryStream stream = new();
    private readonly BinaryWriter writer;

    public CerealWriter() => writer = new BinaryWriter(stream, Utf8, leaveOpen: true);

    public void Bool(bool value) => writer.Write(value);
    public void Int32(int value) => writer.Write(value);
    public void UInt16(ushort value) => writer.Write(value);
    public void UInt32(uint value) => writer.Write(value);
    public void UInt64(ulong value) => writer.Write(value);
    public void Bytes(byte[] value) => writer.Write(value);
    public void Float(float value)
    {
        if (!float.IsFinite(value))
            throw new ArgumentOutOfRangeException(nameof(value), "Protocol floats must be finite.");
        writer.Write(value);
    }

    public void String(string value)
    {
        var bytes = Utf8.GetBytes(value);
        UInt64((ulong)bytes.Length);
        writer.Write(bytes);
    }

    // cereal's optional tag is true for nullopt, not for has_value.
    public void OptionalUInt32(int? value)
    {
        Bool(!value.HasValue);
        if (value.HasValue)
            UInt32(checked((uint)value.Value));
    }

    public void OptionalInt32(int? value)
    {
        Bool(!value.HasValue);
        if (value.HasValue)
            Int32(value.Value);
    }

    public void List<T>(IReadOnlyCollection<T> values, Action<T> write)
    {
        UInt64((ulong)values.Count);
        foreach (var value in values)
            write(value);
    }

    public byte[] ToArray() => stream.ToArray();

    public byte[] ToPacket()
    {
        if (stream.Length > KernelProtocol.MaximumPacketBytes)
            throw new InvalidDataException("Kernel packet exceeds the configured limit.");
        var body = stream.ToArray();
        var packet = new byte[body.Length + sizeof(uint)];
        BinaryPrimitives.WriteUInt32LittleEndian(packet, (uint)body.Length);
        body.CopyTo(packet, sizeof(uint));
        return packet;
    }

    public void Dispose()
    {
        writer.Dispose();
        stream.Dispose();
    }
}

internal sealed class CerealReader
{
    private static readonly UTF8Encoding Utf8 = new(false, true);
    private readonly ReadOnlyMemory<byte> data;
    private int offset;

    public CerealReader(ReadOnlyMemory<byte> data) => this.data = data;
    public int Remaining => data.Length - offset;

    private ReadOnlySpan<byte> Take(int length)
    {
        if (length < 0 || length > Remaining)
            throw new InvalidDataException("Truncated kernel packet; its schema may be incompatible.");
        var value = data.Span.Slice(offset, length);
        offset += length;
        return value;
    }

    public bool Bool() => Take(1)[0] switch
    {
        0 => false,
        1 => true,
        _ => throw new InvalidDataException("Invalid cereal Boolean in kernel packet."),
    };
    public int Int32() => BinaryPrimitives.ReadInt32LittleEndian(Take(sizeof(int)));
    public ushort UInt16() => BinaryPrimitives.ReadUInt16LittleEndian(Take(sizeof(ushort)));
    public uint UInt32() => BinaryPrimitives.ReadUInt32LittleEndian(Take(sizeof(uint)));
    public ulong UInt64() => BinaryPrimitives.ReadUInt64LittleEndian(Take(sizeof(ulong)));
    public float Float() => BinaryPrimitives.ReadSingleLittleEndian(Take(sizeof(float)));

    public string String()
    {
        var length = UInt64();
        if (length > (ulong)Remaining)
            throw new InvalidDataException("Invalid string length in kernel packet.");
        try { return Utf8.GetString(Take((int)length)); }
        catch (DecoderFallbackException exception)
        {
            throw new InvalidDataException("Invalid UTF-8 in kernel packet.", exception);
        }
    }

    public List<T> List<T>(Func<T> read, int minimumItemBytes = 1)
    {
        var count = UInt64();
        if (count > 100_000 || (minimumItemBytes > 0 && count > (ulong)(Remaining / minimumItemBytes)))
            throw new InvalidDataException("Invalid collection length in kernel packet.");
        var values = new List<T>((int)count);
        for (var index = 0; index < (int)count; index++)
            values.Add(read());
        return values;
    }

    public void RequireEnd()
    {
        if (Remaining != 0)
            throw new InvalidDataException("Unexpected kernel packet data; its schema is incompatible with this UI.");
    }
}
