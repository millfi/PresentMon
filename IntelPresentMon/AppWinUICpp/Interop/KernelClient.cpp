// Copyright (C) 2026 Intel Corporation
// SPDX-License-Identifier: MIT
#include "KernelClient.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <limits>
#include <system_error>

namespace pmon::ui::interop
{
    namespace
    {
        constexpr DWORD kConnectTimeoutMs = 10000;
        constexpr DWORD kRequestTimeoutMs = 30000;
        constexpr size_t kMaximumEvents = 256;

        class OperationCancelled : public std::runtime_error
        {
        public:
            OperationCancelled() : std::runtime_error("Kernel operation was canceled.") {}
        };

        class EventHandle
        {
        public:
            EventHandle()
            {
                handle_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                if (!handle_) {
                    throw std::system_error((int)GetLastError(), std::system_category(), "CreateEventW failed");
                }
            }
            ~EventHandle() { if (handle_) CloseHandle(handle_); }
            EventHandle(const EventHandle&) = delete;
            EventHandle& operator=(const EventHandle&) = delete;
            HANDLE Get() const noexcept { return handle_; }
        private:
            HANDLE handle_ = nullptr;
        };

        void ThrowWin32(const char* operation)
        {
            throw std::system_error((int)GetLastError(), std::system_category(), operation);
        }
    }

    std::unique_ptr<KernelClient> KernelClient::Connect(const std::string& pipeBaseName, std::stop_token cancellation)
    {
        const auto baseName = NormalizePipeBaseName(pipeBaseName);
        HANDLE requestPipe = INVALID_HANDLE_VALUE;
        HANDLE eventPipe = INVALID_HANDLE_VALUE;
        try {
            requestPipe = ConnectPipe(baseName + "-in", cancellation);
            eventPipe = ConnectPipe(baseName + "-out", cancellation);
            auto client = std::unique_ptr<KernelClient>(new KernelClient(requestPipe, eventPipe));
            requestPipe = INVALID_HANDLE_VALUE;
            eventPipe = INVALID_HANDLE_VALUE;
            client->session_ = client->OpenSession(GetCurrentProcessId(), cancellation);
            client->StartEventReader();
            return client;
        }
        catch (...) {
            if (requestPipe != INVALID_HANDLE_VALUE) CloseHandle(requestPipe);
            if (eventPipe != INVALID_HANDLE_VALUE) CloseHandle(eventPipe);
            throw;
        }
    }

    KernelClient::KernelClient(HANDLE requestPipe, HANDLE eventPipe) : requestPipe_(requestPipe), eventPipe_(eventPipe) {}
    KernelClient::~KernelClient() { Close(); }

    bool KernelClient::IsConnected() const noexcept { return !closed_.load(); }
    const KernelSessionInfo& KernelClient::Session() const noexcept { return session_; }

    KernelSessionInfo KernelClient::OpenSession(uint32_t processId, std::stop_token cancellation)
    {
        KernelSessionInfo result;
        Request("OpenSession", [processId](CerealWriter& writer) { writer.UInt32(processId); },
            [&result](CerealReader& reader) { result = KernelProtocol::Session(reader); }, cancellation);
        return result;
    }

    IntrospectionData KernelClient::Introspect(std::stop_token cancellation)
    {
        IntrospectionData result;
        Request("Introspect", [](CerealWriter&) {}, [&result](CerealReader& reader) {
            result = KernelProtocol::Introspection(reader);
        }, cancellation);
        return result;
    }

    void KernelClient::BindHotkey(const core::HotkeyBinding& binding, std::stop_token cancellation)
    {
        if (!binding.Combination) {
            ClearHotkey((int)binding.Action, cancellation);
            return;
        }
        const auto key = binding.Combination->Key;
        if (key < 0) {
            throw std::out_of_range("Hotkey code cannot be negative.");
        }
        const auto modifiers = binding.Combination->Modifiers;
        for (const auto modifier : modifiers) {
            if (modifier < 0) {
                throw std::out_of_range("Hotkey modifier cannot be negative.");
            }
        }
        Request("BindHotkey", [key, modifiers, action = (int)binding.Action](CerealWriter& writer) {
            writer.UInt32((uint32_t)key);
            writer.List(modifiers, [&writer](int modifier) { writer.UInt32((uint32_t)modifier); });
            writer.Int32(action);
        }, [](CerealReader&) {}, cancellation);
    }

    void KernelClient::ClearHotkey(int action, std::stop_token cancellation)
    {
        Request("ClearHotkey", [action](CerealWriter& writer) { writer.Int32(action); }, [](CerealReader&) {}, cancellation);
    }

    void KernelClient::PushSpecification(const core::Specification& specification, std::stop_token cancellation)
    {
        CerealWriter payload;
        KernelProtocol::Specification(payload, specification);
        const auto bytes = payload.Data();
        Request("PushSpecification", [&bytes](CerealWriter& writer) { writer.Bytes(bytes); }, [](CerealReader&) {}, cancellation);
    }

    void KernelClient::SetCapture(bool active, std::stop_token cancellation)
    {
        Request("SetCapture", [active](CerealWriter& writer) { writer.Bool(active); }, [](CerealReader&) {}, cancellation);
    }

    std::vector<int> KernelClient::ProbeGpuBusy(const std::vector<int>& pids, std::stop_token cancellation)
    {
        for (const auto pid : pids) {
            if (pid < 0) {
                throw std::out_of_range("Candidate process ID cannot be negative.");
            }
        }
        std::vector<int> result;
        Request("ProbeGpuBusy", [&pids](CerealWriter& writer) {
            writer.List(pids, [&writer](int pid) { writer.UInt32((uint32_t)pid); });
        }, [&result](CerealReader& reader) {
            result = reader.List<int>([&reader] {
                const auto pid = reader.UInt32();
                if (pid > (uint32_t)std::numeric_limits<int>::max()) {
                    throw ProtocolError("GPU Busy result process ID does not fit in an int.");
                }
                return (int)pid;
            }, sizeof(uint32_t));
        }, cancellation);
        return result;
    }

    void KernelClient::SetEtlLogging(bool active, std::stop_token cancellation)
    {
        Request("SetEtlLogging", [active](CerealWriter& writer) { writer.Bool(active); }, [](CerealReader&) {}, cancellation);
    }

    bool KernelClient::WaitEvent(KernelEvent& event, std::stop_token cancellation)
    {
        std::unique_lock lock(eventMutex_);
        std::stop_callback wake(cancellation, [this] { eventReady_.notify_all(); });
        eventReady_.wait(lock, cancellation, [this] { return closed_.load() || !events_.empty(); });
        if (!events_.empty()) {
            event = std::move(events_.front());
            events_.pop_front();
            eventReady_.notify_all();
            return true;
        }
        if (cancellation.stop_requested()) {
            throw OperationCancelled();
        }
        return false;
    }

    void KernelClient::Close() noexcept
    {
        if (closed_.exchange(true)) {
            return;
        }
        if (requestPipe_ != INVALID_HANDLE_VALUE) CancelIoEx(requestPipe_, nullptr);
        if (eventPipe_ != INVALID_HANDLE_VALUE) CancelIoEx(eventPipe_, nullptr);
        eventReady_.notify_all();
        if (eventReader_.joinable() && eventReader_.get_id() != std::this_thread::get_id()) {
            eventReader_.request_stop();
            eventReader_.join();
        }
        if (requestPipe_ != INVALID_HANDLE_VALUE) {
            CloseHandle(requestPipe_);
            requestPipe_ = INVALID_HANDLE_VALUE;
        }
        if (eventPipe_ != INVALID_HANDLE_VALUE) {
            CloseHandle(eventPipe_);
            eventPipe_ = INVALID_HANDLE_VALUE;
        }
    }

    std::string KernelClient::NormalizePipeBaseName(const std::string& pipeBaseName)
    {
        constexpr std::string_view prefix = "\\\\.\\pipe\\";
        std::string name = pipeBaseName.starts_with(prefix) ? pipeBaseName.substr(prefix.size()) : pipeBaseName;
        if (name.empty() || name.find('\\') != std::string::npos || name.find('/') != std::string::npos) {
            throw std::invalid_argument("Expected a local kernel pipe base name.");
        }
        return name;
    }

    HANDLE KernelClient::ConnectPipe(const std::string& name, std::stop_token cancellation)
    {
        const auto path = "\\\\.\\pipe\\" + name;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kConnectTimeoutMs);
        while (!cancellation.stop_requested() && std::chrono::steady_clock::now() < deadline) {
            const auto handle = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
            if (handle != INVALID_HANDLE_VALUE) {
                return handle;
            }
            const auto error = GetLastError();
            if (error != ERROR_PIPE_BUSY && error != ERROR_FILE_NOT_FOUND) {
                ThrowWin32("CreateFileA named pipe failed");
            }
            WaitNamedPipeA(path.c_str(), 50);
        }
        if (cancellation.stop_requested()) {
            throw OperationCancelled();
        }
        throw std::runtime_error("Timed out connecting to the kernel named pipe.");
    }

    void KernelClient::WaitForOverlapped(HANDLE pipe, OVERLAPPED& operation, std::stop_token cancellation, DWORD timeoutMs)
    {
        std::stop_callback cancel(cancellation, [pipe, &operation] { CancelIoEx(pipe, &operation); });
        const auto waited = WaitForSingleObject(operation.hEvent, timeoutMs);
        if (waited == WAIT_TIMEOUT) {
            CancelIoEx(pipe, &operation);
            WaitForSingleObject(operation.hEvent, INFINITE);
            throw std::runtime_error("Timed out waiting for kernel pipe I/O.");
        }
        if (waited != WAIT_OBJECT_0) {
            ThrowWin32("WaitForSingleObject kernel pipe I/O failed");
        }
        DWORD transferred = 0;
        if (!GetOverlappedResult(pipe, &operation, &transferred, FALSE)) {
            const auto error = GetLastError();
            if (cancellation.stop_requested() || error == ERROR_OPERATION_ABORTED) {
                throw OperationCancelled();
            }
            ThrowWin32("Kernel pipe I/O failed");
        }
    }

    void KernelClient::ReadExactly(HANDLE pipe, std::span<uint8_t> bytes, std::stop_token cancellation, DWORD timeoutMs)
    {
        size_t offset = 0;
        while (offset < bytes.size()) {
            EventHandle event;
            OVERLAPPED operation{};
            operation.hEvent = event.Get();
            DWORD transferred = 0;
            const auto remaining = (DWORD)std::min<size_t>(bytes.size() - offset, (size_t)std::numeric_limits<DWORD>::max());
            if (!ReadFile(pipe, bytes.data() + offset, remaining, &transferred, &operation)) {
                const auto error = GetLastError();
                if (error != ERROR_IO_PENDING) {
                    if (cancellation.stop_requested() || error == ERROR_OPERATION_ABORTED) throw OperationCancelled();
                    ThrowWin32("ReadFile kernel pipe failed");
                }
                WaitForOverlapped(pipe, operation, cancellation, timeoutMs);
                if (!GetOverlappedResult(pipe, &operation, &transferred, FALSE)) ThrowWin32("GetOverlappedResult kernel pipe read failed");
            }
            if (transferred == 0) throw std::runtime_error("Kernel pipe disconnected during read.");
            offset += transferred;
        }
    }

    void KernelClient::WriteExactly(HANDLE pipe, std::span<const uint8_t> bytes, std::stop_token cancellation, DWORD timeoutMs)
    {
        size_t offset = 0;
        while (offset < bytes.size()) {
            EventHandle event;
            OVERLAPPED operation{};
            operation.hEvent = event.Get();
            DWORD transferred = 0;
            const auto remaining = (DWORD)std::min<size_t>(bytes.size() - offset, (size_t)std::numeric_limits<DWORD>::max());
            if (!WriteFile(pipe, bytes.data() + offset, remaining, &transferred, &operation)) {
                const auto error = GetLastError();
                if (error != ERROR_IO_PENDING) {
                    if (cancellation.stop_requested() || error == ERROR_OPERATION_ABORTED) throw OperationCancelled();
                    ThrowWin32("WriteFile kernel pipe failed");
                }
                WaitForOverlapped(pipe, operation, cancellation, timeoutMs);
                if (!GetOverlappedResult(pipe, &operation, &transferred, FALSE)) ThrowWin32("GetOverlappedResult kernel pipe write failed");
            }
            if (transferred == 0) throw std::runtime_error("Kernel pipe disconnected during write.");
            offset += transferred;
        }
    }

    std::vector<uint8_t> KernelClient::ReadPacket(HANDLE pipe, std::stop_token cancellation, DWORD timeoutMs)
    {
        std::array<uint8_t, sizeof(uint32_t)> prefix{};
        ReadExactly(pipe, prefix, cancellation, timeoutMs);
        const auto size = (uint32_t)prefix[0] | ((uint32_t)prefix[1] << 8) | ((uint32_t)prefix[2] << 16) | ((uint32_t)prefix[3] << 24);
        if (size < KernelProtocol::MinimumPacketBytes || size > KernelProtocol::MaximumPacketBytes) {
            throw ProtocolError("Invalid kernel packet length.");
        }
        std::vector<uint8_t> body(size);
        ReadExactly(pipe, body, cancellation, timeoutMs);
        return body;
    }

    void KernelClient::WritePacket(HANDLE pipe, std::span<const uint8_t> packet, std::stop_token cancellation, DWORD timeoutMs)
    {
        WriteExactly(pipe, packet, cancellation, timeoutMs);
    }

    void KernelClient::Request(const std::string& identifier, const std::function<void(CerealWriter&)>& write,
        const std::function<void(CerealReader&)>& read, std::stop_token cancellation)
    {
        std::scoped_lock lock(requestMutex_);
        ThrowIfClosed();
        const auto token = nextToken_.fetch_add(1);
        const auto packet = KernelProtocol::Request(identifier, token, write);
        try {
            WritePacket(requestPipe_, packet, cancellation, kRequestTimeoutMs);
            const auto response = ReadPacket(requestPipe_, cancellation, kRequestTimeoutMs);
            CerealReader reader(response);
            const auto header = KernelProtocol::Header(reader);
            KernelProtocol::ValidateResponse(header, identifier, token);
            if (header.TransportStatus != 0) {
                reader.RequireEnd();
                throw KernelRequestException(identifier, header.TransportStatus, header.ExecutionStatus);
            }
            read(reader);
            reader.RequireEnd();
        }
        catch (const KernelRequestException&) {
            throw;
        }
        catch (...) {
            FailConnection(std::current_exception());
            throw;
        }
    }

    void KernelClient::StartEventReader()
    {
        eventReader_ = std::jthread([this](std::stop_token cancellation) {
            ReadEventsLoop(cancellation);
        });
    }

    void KernelClient::ReadEventsLoop(std::stop_token cancellation) noexcept
    {
        try {
            while (!closed_.load()) {
                const auto packet = ReadPacket(eventPipe_, cancellation, INFINITE);
                CerealReader reader(packet);
                EnqueueEvent(KernelProtocol::Event(KernelProtocol::Header(reader), reader));
            }
        }
        catch (...) {
            if (!closed_.load()) FailConnection(std::current_exception());
        }
    }

    void KernelClient::EnqueueEvent(KernelEvent event)
    {
        std::unique_lock lock(eventMutex_);
        eventReady_.wait(lock, [this] { return closed_.load() || events_.size() < kMaximumEvents; });
        if (closed_.load()) return;
        events_.push_back(std::move(event));
        eventReady_.notify_all();
    }

    void KernelClient::FailConnection(std::exception_ptr error) noexcept
    {
        if (!closed_.exchange(true)) {
            terminalError_ = std::move(error);
            if (requestPipe_ != INVALID_HANDLE_VALUE) CancelIoEx(requestPipe_, nullptr);
            if (eventPipe_ != INVALID_HANDLE_VALUE) CancelIoEx(eventPipe_, nullptr);
            eventReady_.notify_all();
        }
    }

    void KernelClient::ThrowIfClosed() const
    {
        if (closed_.load()) {
            throw std::runtime_error("The kernel connection is closed.");
        }
    }
}
