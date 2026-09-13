// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#include "CerealBinary.h"

#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <type_traits>

namespace pmon::ui::interop
{
    namespace
    {
        template<class T>
        void WriteLittleEndian(std::vector<uint8_t>& data, T value)
        {
            static_assert(std::is_integral_v<T>);
            for (size_t index = 0; index < sizeof(T); ++index) {
                data.push_back((uint8_t)((std::make_unsigned_t<T>)value >> (index * 8)));
            }
        }

        template<class T>
        T ReadLittleEndian(std::span<const uint8_t> data)
        {
            static_assert(std::is_integral_v<T>);
            std::make_unsigned_t<T> value = 0;
            for (size_t index = 0; index < sizeof(T); ++index) {
                value |= (std::make_unsigned_t<T>)data[index] << (index * 8);
            }
            return (T)value;
        }

        bool IsValidUtf8(std::span<const uint8_t> bytes)
        {
            for (size_t index = 0; index < bytes.size();) {
                const auto lead = bytes[index++];
                if (lead <= 0x7f) {
                    continue;
                }
                uint32_t codePoint = 0;
                size_t continuationCount = 0;
                if (lead >= 0xc2 && lead <= 0xdf) {
                    codePoint = lead & 0x1f;
                    continuationCount = 1;
                }
                else if (lead >= 0xe0 && lead <= 0xef) {
                    codePoint = lead & 0x0f;
                    continuationCount = 2;
                }
                else if (lead >= 0xf0 && lead <= 0xf4) {
                    codePoint = lead & 0x07;
                    continuationCount = 3;
                }
                else {
                    return false;
                }
                if (index + continuationCount > bytes.size()) {
                    return false;
                }
                for (size_t continuation = 0; continuation < continuationCount; ++continuation) {
                    const auto byte = bytes[index++];
                    if ((byte & 0xc0) != 0x80) {
                        return false;
                    }
                    codePoint = (codePoint << 6) | (byte & 0x3f);
                }
                if ((continuationCount == 2 && codePoint < 0x800) ||
                    (continuationCount == 3 && codePoint < 0x10000) ||
                    (codePoint >= 0xd800 && codePoint <= 0xdfff) || codePoint > 0x10ffff) {
                    return false;
                }
            }
            return true;
        }
    }

    void CerealWriter::Bool(bool value) { data_.push_back(value ? 1 : 0); }
    void CerealWriter::Int32(int value) { WriteLittleEndian(data_, value); }
    void CerealWriter::UInt16(uint16_t value) { WriteLittleEndian(data_, value); }
    void CerealWriter::UInt32(uint32_t value) { WriteLittleEndian(data_, value); }
    void CerealWriter::UInt64(uint64_t value) { WriteLittleEndian(data_, value); }

    void CerealWriter::Float(float value)
    {
        if (!std::isfinite(value)) {
            throw std::out_of_range("Protocol floats must be finite.");
        }
        UInt32(std::bit_cast<uint32_t>(value));
    }

    void CerealWriter::Bytes(std::span<const uint8_t> value)
    {
        data_.insert(data_.end(), value.begin(), value.end());
    }

    void CerealWriter::String(const std::string& value)
    {
        const auto bytes = std::span<const uint8_t>{ reinterpret_cast<const uint8_t*>(value.data()), value.size() };
        if (!IsValidUtf8(bytes)) {
            throw std::invalid_argument("Protocol strings must be valid UTF-8.");
        }
        UInt64((uint64_t)value.size());
        Bytes(bytes);
    }

    void CerealWriter::OptionalUInt32(const std::optional<int>& value)
    {
        Bool(!value.has_value());
        if (value) {
            if (*value < 0) {
                throw std::out_of_range("The optional unsigned value cannot be negative.");
            }
            UInt32((uint32_t)*value);
        }
    }

    void CerealWriter::OptionalInt32(const std::optional<int>& value)
    {
        Bool(!value.has_value());
        if (value) {
            Int32(*value);
        }
    }

    void CerealWriter::List(const std::vector<int>& values, const std::function<void(int)>& write)
    {
        UInt64((uint64_t)values.size());
        for (const auto value : values) {
            write(value);
        }
    }

    void CerealWriter::List(const std::vector<uint32_t>& values, const std::function<void(uint32_t)>& write)
    {
        UInt64((uint64_t)values.size());
        for (const auto value : values) {
            write(value);
        }
    }

    std::vector<uint8_t> CerealWriter::Data() const { return data_; }

    std::vector<uint8_t> CerealWriter::Packet() const
    {
        constexpr size_t maxPacketBytes = 16 * 1024 * 1024;
        if (data_.size() > maxPacketBytes) {
            throw ProtocolError("Kernel packet exceeds the configured limit.");
        }
        std::vector<uint8_t> packet;
        packet.reserve(sizeof(uint32_t) + data_.size());
        WriteLittleEndian(packet, (uint32_t)data_.size());
        packet.insert(packet.end(), data_.begin(), data_.end());
        return packet;
    }

    CerealReader::CerealReader(std::span<const uint8_t> data) : data_(data) {}
    size_t CerealReader::Remaining() const noexcept { return data_.size() - offset_; }

    std::span<const uint8_t> CerealReader::Take(size_t length)
    {
        if (length > Remaining()) {
            throw ProtocolError("Truncated kernel packet; its schema may be incompatible.");
        }
        const auto result = data_.subspan(offset_, length);
        offset_ += length;
        return result;
    }

    bool CerealReader::Bool()
    {
        const auto value = Take(1)[0];
        if (value > 1) {
            throw ProtocolError("Invalid cereal Boolean in kernel packet.");
        }
        return value != 0;
    }

    int CerealReader::Int32() { return ReadLittleEndian<int>(Take(sizeof(int))); }
    uint16_t CerealReader::UInt16() { return ReadLittleEndian<uint16_t>(Take(sizeof(uint16_t))); }
    uint32_t CerealReader::UInt32() { return ReadLittleEndian<uint32_t>(Take(sizeof(uint32_t))); }
    uint64_t CerealReader::UInt64() { return ReadLittleEndian<uint64_t>(Take(sizeof(uint64_t))); }
    float CerealReader::Float() { return std::bit_cast<float>(UInt32()); }

    std::string CerealReader::String()
    {
        const auto length = UInt64();
        if (length > Remaining()) {
            throw ProtocolError("Invalid string length in kernel packet.");
        }
        const auto bytes = Take((size_t)length);
        if (!IsValidUtf8(bytes)) {
            throw ProtocolError("Invalid UTF-8 in kernel packet.");
        }
        return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
    }

    std::optional<int> CerealReader::OptionalInt32()
    {
        if (Bool()) {
            return std::nullopt;
        }
        return Int32();
    }

    std::optional<int> CerealReader::OptionalUInt32()
    {
        if (Bool()) {
            return std::nullopt;
        }
        const auto value = UInt32();
        if (value > (uint32_t)std::numeric_limits<int>::max()) {
            throw ProtocolError("Optional unsigned value does not fit in an int.");
        }
        return (int)value;
    }

    void CerealReader::RequireEnd() const
    {
        if (Remaining() != 0) {
            throw ProtocolError("Unexpected kernel packet data; its schema is incompatible with this UI.");
        }
    }
}
