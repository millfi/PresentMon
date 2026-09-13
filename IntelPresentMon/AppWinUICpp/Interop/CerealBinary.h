// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace pmon::ui::interop
{
    class ProtocolError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    class CerealWriter
    {
    public:
        void Bool(bool value);
        void Int32(int value);
        void UInt16(uint16_t value);
        void UInt32(uint32_t value);
        void UInt64(uint64_t value);
        void Float(float value);
        void Bytes(std::span<const uint8_t> value);
        void String(const std::string& value);
        void OptionalUInt32(const std::optional<int>& value);
        void OptionalInt32(const std::optional<int>& value);
        void List(const std::vector<int>& values, const std::function<void(int)>& write);
        void List(const std::vector<uint32_t>& values, const std::function<void(uint32_t)>& write);
        std::vector<uint8_t> Data() const;
        std::vector<uint8_t> Packet() const;

        template<class T, class F>
        void List(const std::vector<T>& values, F&& write)
        {
            UInt64((uint64_t)values.size());
            for (const auto& value : values) {
                write(value);
            }
        }

    private:
        std::vector<uint8_t> data_;
    };

    class CerealReader
    {
    public:
        explicit CerealReader(std::span<const uint8_t> data);
        size_t Remaining() const noexcept;
        bool Bool();
        int Int32();
        uint16_t UInt16();
        uint32_t UInt32();
        uint64_t UInt64();
        float Float();
        std::string String();
        std::optional<int> OptionalInt32();
        std::optional<int> OptionalUInt32();
        void RequireEnd() const;

        template<class T, class F>
        std::vector<T> List(F&& read, size_t minimumItemBytes = 1)
        {
            const auto count = UInt64();
            if (count > 100000 || (minimumItemBytes != 0 && count > Remaining() / minimumItemBytes)) {
                throw ProtocolError("Invalid collection length in kernel packet.");
            }
            std::vector<T> values;
            values.reserve((size_t)count);
            for (uint64_t index = 0; index < count; ++index) {
                values.emplace_back(read());
            }
            return values;
        }

    private:
        std::span<const uint8_t> Take(size_t length);
        std::span<const uint8_t> data_;
        size_t offset_ = 0;
    };
}
